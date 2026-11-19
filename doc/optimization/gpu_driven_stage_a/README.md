# GPU Driven 第一阶段性能复测

2026-11-18。当前第一阶段完成了功能闭环，**尚未测到端到端帧率提升**。关闭会话采集后，原 PBR 路径两次约 60 FPS，GPU Driven 纯色路径为 56.3 / 59.8 FPS。开启 Profile 后降到约 32 FPS 的主要原因是每帧生成大规模 RDG JSON，不能将这个采集结果直接当作日常运行帧率。

## 配置和口径

- Windows ARM64、Adreno X1-85，Release、RHI Debug 关闭、1280×720、固定初始相机、隐藏窗口；两条路径都关闭 Bloom。
- 当前源码构建同一二进制，只切换 `--gpu-driven`；构建使用独立 `XMAKE_CONFIGDIR=build/gpu-driven-profile-config` 和 `build/gpu-driven-profile-release`，工作区默认 Debug 配置未改变。
- Profile 各运行 150 帧，预热 30 帧，统计 frame_id 30～149 的 120 帧；逐帧 Pipeline Statistics 开启，逐绘制 probes 关闭，CPU 事件零丢失。每份报告均有完整的 150 帧 GPU 记录和 150 张图。
- 不采集运行使用 `--benchmark-lite --frames 330`。先排空 30 帧，再计时剩余 300 帧，包含末尾 GPU 排空；加载、截图、退出和报告文件导出不在计时区间。按 PBR / GPU / GPU / PBR 顺序串行重复，没有同时构建、测试或解析大报告。
- 保留 `Present(1,0)`，约 60 FPS 的上限会掩盖更高吞吐能力；没有锁定系统频率，少量差异不能当成稳定收益。
- 两条路径画质和工作量不同：原路径是 Depth + 贴图 PBR GBuffer + Deferred Lighting；新路径是双面纯色、没有独立 Depth Prepass，也未实现 Alpha Mask。因此下述是当前两条实际路径的对照，**不能把差值归为 Indirect API 本身的加速比**。

## 吞吐

| 模式 | 原 PBR | GPU Driven 纯色 |
|---|---:|---:|
| 无会话采集，第 1 次 | 60.0 FPS / 16.660 ms | 56.3 FPS / 17.750 ms |
| 无会话采集，第 2 次 | 60.1 FPS / 16.638 ms | 59.8 FPS / 16.734 ms |
| 开启 Profile，墙钟 | 60.5 FPS / 16.530 ms | 32.2 FPS / 31.012 ms |

非采集状态为大致持平到单次约 6% 的 FPS 下降，目前没有数据支持“第一阶段已经带来帧率提升”。

## CPU 热点

单位为每帧 inclusive 墙钟均值 ms。不同线程或嵌套 Scope 不能相加。

| Scope / 指标 | 原 PBR | GPU Driven |
|---|---:|---:|
| Render `build_frame` | 1.791 | 3.807 |
| 其中 RDG `compile` | 0.060 | 1.850 |
| 其中 `GpuSceneFrame::create` | — | 0.580 |
| Device `GpuScene::prepare` | — | 0.144 |
| Device `SceneRender` | 12.307 | 29.044 |
| 其中 RDG `begin_profile` | 0.026 | 0.686 |
| 其中 RDG `end_profile` | 0.338 | **23.405** |
| 上传 Pass CPU | — | **3.442** |
| Depth Pass CPU | 3.258 | — |
| GBuffer Pass CPU | 8.221 | — |
| GPU Driven Raster Pass CPU | — | **0.090** |
| 每帧图资源数量 | 6 | **2,122** |

一次间接调用的 Raster 回调确实很便宜，但现有第一阶段每帧仍导入 1,056 对源几何 Buffer。即使公共池不再需要上传几何，图里仍保留这些源资源及读取边；它们参与构建、编译、barrier/resource 遍历和完整快照。

`RDGBuilder::end_profile()` 在 Device 热路径内用 cereal 将整张图转成 JSON，平均 23.4 ms，约占当前 `SceneRender` 的 81%。这部分发生在进程内的帧录制期间，与退出时写 JSON/HTML 文件的耗时不同。`profile_resource()` 每次 resolve 还线性扫描资源快照，资源多时可能产生额外的二次复杂度；本次没有继续细分它在上传 Pass 内的独占耗时。

原来的逐对象 Depth/GBuffer 录制已被压缩，但新路径同时省去了逐对象纹理和材质工作，不能把 11.5 ms → 0.09 ms 直接宣称为等画质 GPU Driven 加速。当前还要支付 3.44 ms 上传 Pass、3.81 ms 构图及采集成本。

## GPU

| 阶段 | 原 PBR ms | GPU Driven ms |
|---|---:|---:|
| GPU 整帧 | **7.480** | **10.674** |
| GPU 整帧 P95 | 8.826 | 14.067 |
| Depth Prepass | 2.060 | — |
| GBuffer | 4.770 | — |
| Deferred Lighting | 0.612 | — |
| 稳态对象上传 | — | 0.012 |
| Reset Draw Count | — | 0.069 |
| Cull / Build Args | — | **0.033** |
| GPU Driven Raster | — | **10.554** |

GPU 剔除与计数重置合计约 0.103 ms，不是当前热点。GPU 时间几乎都在光栅 Pass；纯色路径在该次采样中仍比完整原路径慢，不能认为换成 Indirect 就已经降低 GPU 耗时。

Pipeline Statistics 显示新 Raster 的 VS 约 540 万次、PS 约 406 万次；原 Depth 的 VS 为 146 万次，GBuffer 的 VS 约 10 万次、PS 为 114 万次。查询说明新路径的 Shader 工作没有随 CPU 调用数同步减少，但不能直接将这些计数当作 CPU 提交索引总数或精确 overdraw。双面绘制、缺少 Depth Prepass、原子压缩后未经深度排序，以及实例索引输入方式都是后续单变量实验的候选；本次没有证明哪个因素主导 GPU 差异。

稳态自建 GPU 分配为 PBR 1,081.56 MiB、GPU Driven 1,086.19 MiB。净增加约 4.63 MiB 不等于公共几何池本身的大小：PBR 一侧还保留了约 24.81 MiB 的 RDG 缓存，两条路径的附件集合不同。

## 后续顺序

1. 将静态几何注册/上传变为持久或增量操作，每帧图只引用公共池，消除稳定帧里两千多个源资源节点和无效遍历。
2. 将资源快照 ID 查找改为索引；缓存不可变图结构，将 JSON 序列化移出 Device 的逐帧热路径。采集前后都继续用轻量吞吐验证。
3. 增加相同纯色 Shader、几何、剔除/深度状态下的 CPU Direct 对照，隔离 GPU cull / compact / indirect 的真实收益；用真实输出验证前后候选集合和画面一致。
4. 单独实验背面状态分组、深度预处理/排序及实例索引传递，减少几何和像素工作，再接入 PBR。不要把材质删减或频率变化计为架构收益。

## 产物和复现

构建后的隔离二进制为 `build/gpu-driven-profile-release/windows/arm64/release/sun_temple.exe`。

```powershell
& .\build\gpu-driven-profile-release\windows\arm64\release\sun_temple.exe game/assets/sun_temple/source --benchmark --benchmark-lite --frames 150 --capture build/test-results/gpu-driven-pbr-release.png --profile-output build/test-results/gpu-driven-pbr-release.json
& .\build\gpu-driven-profile-release\windows\arm64\release\sun_temple.exe game/assets/sun_temple/source --gpu-driven --benchmark --benchmark-lite --frames 150 --capture build/test-results/gpu-driven-solid-release.png --profile-output build/test-results/gpu-driven-solid-release.json
& .\build\gpu-driven-profile-release\windows\arm64\release\sun_temple.exe game/assets/sun_temple/source --gpu-driven --benchmark-lite --frames 330 --capture build/test-results/gpu-driven-solid-lite-1.png
```

本机产物：[PBR 报告](../../../build/test-results/gpu-driven-pbr-release.html)、[GPU Driven 报告](../../../build/test-results/gpu-driven-solid-release.html)、[结构化汇总](../../../build/test-results/gpu-driven-performance-summary.json)。原始运行日志在 `build/gpu-driven-{pbr,solid}-{release,lite-1,lite-2}.log`，采样脚本和汇总脚本分别为 `build/run_gpu_driven_profiles.ps1`、`build/analyze_gpu_driven_profile.py`，均位于忽略的构建目录。

本次仅增加测量入口和 CPU Scope，未修改渲染算法或自动应用上述优化。

验证：隔离 Release 样例构建成功，上述六次运行均正常退出；工作区默认 Debug 的 `xmake run utest` 通过 45 个用例、734 个断言，`git diff --check` 通过。
