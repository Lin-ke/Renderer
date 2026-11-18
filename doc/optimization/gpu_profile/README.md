# Sun Temple GPU / Device Profile

2026-11-18，在原始渲染路径上采集，未保留 GPU 优化改动。结论是 **GBuffer 和 Depth Prepass 合计占 GPU 时间约 83%；Device 资源准备和命令录制也接近 GPU 执行耗时，而且当前后端逐帧等待 GPU 完成，两部分无法跨帧重叠。**

后续已实现 View/描述符复用与异步提交，前后实测和验证见 [Device 优化记录](../device_submission/README.md)。本文数据保留为优化前调查。

## 配置与采样

- Windows ARM64，Qualcomm Adreno X1-85 GPU，驱动 31.0.170.0。
- Release，RHI Debug 关闭，1280×720，Bloom 开启，Game / Render / Device 三线程。
- 原生资源加载，固定初始相机，隐藏窗口，无并行构建或测试；未控制系统频率与 OS 文件缓存。
- 稳态采样运行 330 帧，不启用逐绘制细测；统计帧 30～329，共 300 帧。
- 另运行 120 帧，启用 `--profile-probes`，只使用第 30 帧定位每绘制开销与 Pipeline Statistics。

```powershell
xmake f -y -m release
xmake build sun_temple
xmake run sun_temple game/assets/sun_temple/source --benchmark --bloom --frames 330 --profile-output build/test-results/gpu-profile-long.json --capture build/test-results/gpu-profile-long.png
xmake run sun_temple game/assets/sun_temple/source --benchmark --profile-probes --bloom --frames 120 --profile-output build/test-results/gpu-profile-current.json --capture build/test-results/gpu-profile-current.png
```

本机产物：[稳态原始报告](../../../build/test-results/gpu-profile-long.json)、[汇总数据](../../../build/test-results/gpu-profile-summary.json)、[细测报告](../../../build/test-results/gpu-profile-current.json)、[截图](../../../build/test-results/gpu-profile-long.png)。这些文件位于忽略的构建目录，重新运行会覆盖同名文件。

稳态报告有 330 条 GPU 帧记录、330 个图，`dropped_cpu_events=0`。两个采集的截图 RGB 均与原始路径基准逐像素一致。另一次开启细测的 330 帧报告 `gpu-profile-steady.json` 丢失了 980 个 CPU 事件，不用于 CPU 均值；长采样应关闭细测。

## GPU 时间

| 阶段 | 均值 ms | 占整帧 |
|---|---:|---:|
| GBuffer | 7.158 | 62.6% |
| Depth Prepass | 2.329 | 20.4% |
| Deferred Lighting | 1.088 | 9.5% |
| Bloom 四个 Pass 合计 | 0.826 | 7.2% |
| Skybox | 0.025 | 0.2% |
| GPU 整帧 | **11.433** | 100% |

GPU 整帧中位数 11.673 ms，P95 12.993 ms。Pass 时间不包括全部 Pass 间开销，不能要求其和与整帧完全相等。

绝对耗时存在运行间与运行内波动：短采样排除第 30 帧后，帧 31～119 的 GPU 均值为 9.804 ms，其中 GBuffer 6.171 ms、Depth 1.999 ms。长采样三个连续窗口的 GPU 均值为 11.662、10.523、11.944 ms，热点占比一致。目前不能将这些差异归因于某个代码修改，也不作为优化收益。

## CPU 与等待

下列均为稳态 300 帧的墙钟均值，包含线程被调度暂停的时间。

| 区间 | 均值 ms | 含义 |
|---|---:|---|
| `make_render_packet` | 0.987 | Game 提取场景快照 |
| `build_frame` | 1.123 | Render 更新管理器、构图与编译 |
| `SceneRender` | **10.162** | Device 准备图资源、录制 RHI 命令 |
| `Submit` | **13.120** | 提交、GPU 完成等待及提交后回收 |
| `Present` | 0.181 | Present 调用 |
| `ResourceTick` | 1.478 | Device 扫描与回收 RHI 对象 |
| `execute_frame` | **25.540** | Device 整帧，包含以上 Device 区间和其余开销 |
| `DeviceQueue` | 50.301 | 图进入 Device 后的排队延迟 |
| `FrameBackpressure` | 24.230 | Game 等待可用在途帧名额 |

样例完成帧吞吐为 **38.7 FPS**。不能用 GPU 的 11.43 ms 直接推算最终帧率，也不能将 GPU 时间再次加到 Submit 上；Submit 已包含 GPU 完成等待。排队和背压是延迟，不能当作额外 CPU 执行量累计。

源码确认 `DX12Backend::submit_and_wait()` 在 `ExecuteCommandLists` 后 Signal fence，并调用 `WaitForSingleObject` 等待。`RenderSystem::execute_frame()` 等这个调用返回后再 Present、ResourceTick，随后 Device 才能录制下一帧。Render 线程已经能提前构图，但当前 Device 录制与 GPU 执行仍逐帧串行。

## 第 30 帧细测

这些是独立诊断帧的结果，包含细测开销，不能与上面的稳态均值直接相减。

| CPU 指标 | Depth Prepass | GBuffer |
|---|---:|---:|
| Descriptor Snapshot 次数 | 1,004 | 1,020 |
| Snapshot 累计 ms | 0.257 | 0.389 |
| Flush Bindings 次数 | 1,004 | 1,020 |
| Flush Bindings 累计 ms | 0.803 | 2.481 |
| TextureView 创建次数 | 0 | **7,140** |
| TextureView 创建累计 ms | 0 | 1.061 |
| Pass 录制墙钟 ms | 2.349 | 7.467 |

每次 Draw 会刷新绑定。GBuffer 每个绘制写入 7 个纹理槽，`RHIDescriptorSet::write_texture()` 每次创建 View，形成 1,020 × 7 次创建，随后还要快照 DescriptorSet 并在 DX12 写入/拷贝描述符。**这里已有直接观测到的 CPU 重复工作**。表中不同 Scope 的区间可能嵌套，不能机械求和。

GPU Pipeline Statistics 在同一个诊断帧报告：

| Pass | VS 调用 | PS 调用 | CS 调用 |
|---|---:|---:|---:|
| Depth Prepass | 1,461,546 | 0 | 0 |
| GBuffer | 102,144 | 1,144,796 | 0 |
| Deferred Lighting | 3 | 921,600 | 0 |
| 每个 Bloom Compute Pass | 0 | 0 | 230,400 |
| Bloom Composite | 3 | 921,600 | 0 |

这些值是 [D3D12 查询计数](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics)。本驱动报告的 GBuffer IA/VS 计数显著低于 Depth；目前没有对应的 CPU 提交索引计数来交叉验证，不能据此断言两遍提交了多少重复几何。PS 调用数也不是精确的 overdraw 比例。计数尚不能将时间归因到 ALU、纹理采样、带宽或驱动调度。

## 后续优先级

1. **Device 命令准备与提交重叠**：先减少 GBuffer 每绘制 View 创建、Descriptor 快照和描述符刷新；设计带 fence 的帧资源回收、allocator/descriptor/upload 分帧使用，再将逐帧等待改为达到在途上限或复用帧资源时等待。只删等待会破坏资源生命周期。
2. **GPU GBuffer**：先用硬件计数器或保持画面正确的单变量实验，区分着色、带宽和前端成本。当前四个 MRT 为 3 × RGBA8 + RGBA32F，共 28 字节/像素，其中世界位置 RT 占 16 字节；这是值得验证的带宽候选，尚未证实为主要瓶颈。Shader 中也存在每顶点 determinant 与带 discard 的通用材质分支，需要分别测量。
3. **Depth Prepass**：虽然自身占约 20%，仍不能仅凭这个时间将其删除；应比较它减少的 GBuffer 工作量与自身成本，并交叉检查提交计数、覆盖率和遮挡情况。

本轮仅增加按需 GPU Pipeline Statistics 采集与对应功能验证，未修改渲染算法。已恢复 Debug 配置，`xmake build -a` 成功；`xmake run utest` 通过 **38 个用例、494 条断言**，包括真实三角形绘制、像素回读、查询计数、JSON 导出和 DX12 调试信息检查。
