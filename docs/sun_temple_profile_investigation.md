# Sun Temple 加载与 Device 耗时调查

后续加载优化已经实施，过程与最终低于 10 秒的实测见 [加载优化记录](../doc/optimization/load_optimization/README.md)。本文保留优化前的调查数据。

2026-11-18，Windows ARM64，原生资源加载，1280×720，开启 Bloom，隐藏窗口运行 40 帧。Debug 和 Release 分别独占运行，没有并行构建或测试。没有清理 OS 文件缓存。每次采集都包含第 30 帧的 RHI 细测；稳态均值使用第 31～39 帧，避开细测帧。样本用于定位问题，不作为正式性能基线。

## 结果

| 指标 | Debug | Release |
|---|---:|---:|
| 场景加载 | 88.429 s | 85.758 s |
| 等待资源读取/反序列化 | 84.585 s | 83.014 s |
| 依赖解析 | 0.457 s | 0.183 s |
| 初始化区间累计 | 2.842 s | 2.066 s |
| Device 整帧墙钟均值，帧 31～39 | 70.676 ms | 18.197 ms |
| Device 场景资源准备/录制均值 | 45.071 ms | 9.154 ms |
| 提交和完成等待均值 | 18.468 ms | 7.024 ms |
| GPU 整帧均值 | 11.567 ms | 5.789 ms |
| 完成帧吞吐，帧 30～39 | 14.1 FPS | 54.9 FPS |

初始化/读取的区间可能嵌套或在不同线程重叠，不能将表中各阶段直接相加当总时间。CPU 区间是墙钟时间，不是处理器执行时间。Release 同时关闭了当前配置下的 RHI Debug 和 Shader Debug 编译标志，因此帧差异不能仅归因于 C++ 优化。

## 加载：二进制 Mesh 仍按字符串解析向量

`math_reflect.h` 的向量 `save_minimal/load_minimal` 对所有 Archive 使用同一条字符串路径。检查现有 Mesh `.binasset`，其中保存了诸如 `947.584961 416.000000 -189.129379` 的文本，以及每个字符串的长度字段。`from_compact_string` 为每个向量构造 `stringstream` 并逐分量执行 `operator>>`。

读取现有文件、按原格式跳过字段得到：

- 1,056 个 Mesh，1,819,128 个顶点。
- 7,276,512 个属性向量。
- 向量文本 202.12 MiB，逐向量长度字段 55.52 MiB。
- 原始属性浮点数据约 83.27 MiB，索引 6.94 MiB。
- 当前 Mesh 归档总计约 264.93 MiB。

独立 Release 微测使用与这些文件相同形状的 position/normal/tangent/UV 文本，两个解析器得到相同校验和：

| 解析器 | 线程数 | 向量数 | 墙钟耗时 |
|---|---:|---:|---:|
| stringstream | 1 | 80,000 | 97.883 ms |
| from_chars | 1 | 80,000 | 13.482 ms |
| stringstream | 10 | 800,000 | 9,200.423 ms |
| from_chars | 10 | 800,000 | 23.457 ms |

微测表明，当前字符串流解析在并发情况下吞吐显著下降。这与主场景 83～85 秒的读取/反序列化等待一致；不是单靠提高优化级别就能解决的问题。没有进行锁栈采样，尚不把争用归因于某一把具体的 locale/堆锁。微测倍率也不能直接当成整个场景的加速承诺。

优先改进可以分为两步：先用无需字符串流的解析器读取旧资源，保留归档兼容；随后为二进制 Mesh 设计带版本兼容的连续数值数组格式。JSON 编辑格式可以继续保持可读性。调查阶段未修改现有序列化格式或资源文件。

## Device：每 Draw 重复创建 View 和复制绑定

第 30 帧有 1,020 个 GBuffer Draw 和 1,004 个 Depth Draw。GBuffer 每 Draw 写 7 张纹理，因此创建 7,140 个 RHI TextureView。虽然 DX12 原生 SRV 存在纹理内部缓存，RHI View 对象仍重新分配并登记到资源表。

| 第 30 帧区间 | Debug | Release |
|---|---:|---:|
| GBuffer 全部 CPU 录制 | 37.616 ms | 7.019 ms |
| GBuffer TextureView 创建，7,140 次 | 7.231 ms | 1.002 ms |
| GBuffer DescriptorSet 快照，1,020 次 | 5.150 ms | 0.368 ms |
| GBuffer flush_bindings，1,020 次 | 9.730 ms | 2.270 ms |
| Depth 全部 CPU 录制 | 11.333 ms | 2.123 ms |

细测会带来额外开销；这些是采样帧内的实测区间。其余 GBuffer 录制还包含参数更新、Pipeline/几何绑定、原生调用等，未全部细分，不把剩余时间武断归到某一函数。

可直接对应到的代码路径：

- `GBufferPass::build` 的回调逐 Draw 调用 `write_texture` 七次。
- `RHIDescriptorSet::write_texture` 每次创建 RHI View。
- `snapshot()` 每次复制 Descriptor 向量及 Uniform Buffer 数据。
- `DX12CommandContext::flush_bindings` 每 Draw 遍历布局、刷新 Descriptor，并重复检查布局兼容和保留集合引用。
- `RHIBackend::tick` 每帧遍历强引用资源表；不断生成的短命 View 也进入该表。
- 后端每次提交都 `submit_and_wait`，CPU 准备与 GPU 执行无法充分重叠。DeviceQueue 等待是该背压的表现，不能当成额外的 GPU 执行时间。

优先在 Device 缓存 View 和稳定材质绑定，按 frame/material/object 区分数据更新频率；然后考虑帧内上传 Arena 和异步提交。不能删除为延迟命令录制提供正确数据所有权的快照语义。

## 内存：小 Buffer 对齐和上传临时对象滞留

两种构建的 Device 字节统计相同。稳定后 Buffer 分类共有 5,284 个分配，其中引擎基线为 4 个，几何部分为 1,056×5=5,280 个。总 Buffer 分配 381.5 MiB，4,899 个分配恰好为 64 KiB；原始几何属性与索引合计约 90.21 MiB。实际分配约为有效几何数据的 4.2 倍。

主要原因是每个 Mesh 的各顶点流和索引各自创建 Committed Resource。需要大 Buffer 子分配来减少小资源对齐浪费，单纯把资源放进 RDG 池不能解决长期几何数据的这一问题。

加载完成时 Upload 分类占 644.31 MiB，稳定后为 82.56 MiB，减少 561.75 MiB。`Texture::upload_bc` 创建的 `MEMORY_USAGE_CPU_ONLY` 源 Buffer 在 DX12 中实际使用 Upload Heap，并保留 CPU shadow；随后每 mip 还会创建用于 footprint 重排的原生上传暂存。

这些 RHI 源 Buffer 被 `resource_map_` 强引用，提交完成后仍要等 `last_use_tick_ > 6` 的清理条件。加载阶段没有帧 tick，因此临时资源一直留到开始渲染后的若干帧。改进方向是消除重复暂存，并按上传提交完成回收，而不是让上传临时对象参与普通资源的帧龄清理。

当前稳定期引擎自建资源分配约 1,088.56 MiB，历史峰值 2,620.12 MiB，外部 BackBuffer 另计 10.69 MiB。这些是后端资源分配需求字节，包含 Upload/Readback，不能等同于独立物理显存或与进程内存直接相加。

## 本次改动与复现

本次只补了分项采集和诊断入口，没有实施上述性能优化。加载采集新增 `AssetReadUID`、`AssetReadDependencies`、`AssetReadPayload`、`AssetResolveDependencies`、`AssetWaitReads` 和 `AssetInitialize`。`--profile-probes` 开启第 30 帧的 DescriptorSnapshot、TextureView 创建和 FlushBindings 细测，未指定帧数时默认运行 40 帧；普通 profile 不开启这些 per-draw 探针。

```powershell
xmake f -y -m debug
xmake build sun_temple
xmake run sun_temple game/assets/sun_temple/source --benchmark --profile-probes --bloom --frames 40 --capture build/test-results/sun-temple-investigation-debug.png --profile-output build/test-results/sun-temple-investigation-debug.json
```

Release 使用相同场景和帧数，先配置 `xmake f -y -m release` 并构建。调查结束已恢复 Debug 配置。

报告与日志保存在 `build/test-results/sun-temple-investigation-{debug,release}.json`、对应 PNG，以及 `build/sun-temple-investigation-{debug,release}.log`。解析微测源码位于 `build/profile-math/main.cpp`，输出为 `build/investigation-math.log`。
