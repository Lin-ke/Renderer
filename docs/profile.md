# 性能采集与内存统计

`ProfileSession` 是独立于编辑器 300 帧历史的按需采集。报告包含 CPU Chrome Trace 事件、GPU 帧与 Pass 时间、RDG 快照、进程内存和 Device 资源分配快照。当前以文件报告为入口，现有 CPU/GPU 调试窗口继续可用。

## 使用

```powershell
xmake build sun_temple
xmake run sun_temple game/assets/sun_temple/source --profile --frames 6 --bloom --capture build/test-results/profile-sun-temple.png
xmake run sun_temple game/assets/sun_temple/source --benchmark --frames 150 --profile-output build/test-results/sun-temple-profile.json
```

`--profile` 默认采集 330 帧，可用 `--frames` 覆盖；默认报告为 `build/test-results/sun-temple-profile.json`。`--benchmark` 同时启用采集，跳过前 30 个完成帧，输出完成帧吞吐率、Device 墙钟时间和 GPU 整帧时间的均值/P95。截图在第 3 帧读取。

`--profile-probes` 额外细测第 30 帧的 TextureView 创建、Descriptor 快照和绑定刷新，并采集各 Pass 的 GPU Pipeline Statistics，默认运行 40 帧。加载阶段会记录依赖扫描、资源读取/反序列化、等待及初始化。细测带来额外采集开销，稳态比较应排除第 30 帧。长帧采样应关闭 `--profile-probes`，逐绘制事件可能提前用尽 CPU 事件缓冲区；始终检查 `dropped_cpu_events`。加载调查见 [sun_temple_profile_investigation.md](sun_temple_profile_investigation.md)，后续 GPU 调查见 [GPU Profile 记录](../doc/optimization/gpu_profile/README.md)。

Sun Temple 标记 `EngineInit`、`LoadScene`、`FirstFrameReady`。首帧时间从开始加载场景计到首帧 GPU 完成并 Present 返回；只在首帧里程碑等待一次，不给后续每帧增加等待。`LoadScene` 是加载调用的墙钟时间，可能包含同步上传；其后显式 flush 剩余上传，再采集 `SceneLoaded`。不把重叠的阶段相加当总时间。

报告记录构建模式、Bloom、分辨率、线程模式、来源路径和实际加载方式。首次导入与原生资源加载必须分开比较；OS 文件缓存不由工具清理。Debug 结果可用于定位阶段，性能比较应固定构建与运行配置。

## CPU 和任务关联

现有 `CPU_PROFILE_SCOPE` / `PROFILE_SCOPE` 在采集期间同时写入会话，不受 UI 按帧裁剪影响。每线程缓冲区最多保存 16,384 个会话事件，超出后停止接收该线程的新事件，并在报告 `dropped_cpu_events` 中计数。Scope 名称在记录时拷贝，最长 95 字节。写报告前先等待生产者完成，再 `end()`。

```cpp
auto& profile = ProfileSession::instance();
profile.begin("LoadScene");
auto loading = profile.begin_operation("LoadScene");
{
    ProfileContextScope operation(loading.context);
    // ThreadPool::enqueue / EngineContext::run_device_task 自动携带当前关联信息。
    // 自建线程或其他任务系统应显式传递 ProfileContext。
}
profile.end_operation(loading); // 允许由另一线程结束。
profile.end();
profile.write("build/test-results/load-profile.json");
```

`frame_id` 关联 Packet、CompiledFrame、CPU 事件、GPU 查询和 RDG；`operation_id` 关联一次加载；图内 `pass_id` 关联 RDG Pass 的 CPU 录制和 GPU 执行。`RenderQueue`、`DeviceQueue` 记录排队区间，`FrameBackpressure` 记录 Game 侧背压。Scope 是墙钟耗时，包含调度暂停和其中的等待，不是线程实际占用处理器的时间。

文件可导入 Perfetto 查看 `traceEvents` 中的 CPU 时间线。GPU 时间以 `gpu_frames` 单独输出，Pass 的 `offset_ms` 相对该 GPU 帧开始；尚未进行 CPU/GPU 时钟校准，不能直接叠加到 CPU 时间轴。

帧提交现为异步：`Submit` 是提交调用的墙钟区间，`RenderSystem_FrameReuseWait` 单列帧槽复用等待，`Present` 仍可能包含同步间隔节流。GPU 查询只在该帧 fence 完成后读取；`FrameStats` 记录观察到完成的时间，Device 耗时不包含返回后 GPU 继续执行的尾部，也不包含随后导出查询结果的时间。末尾 `EngineContext::wait_for_render()` 会排空 CPU 队列、等待 GPU 并收集剩余查询，必须在 `ProfileSession::end()` 前调用。

每个 GPU Pass 的 `pipeline_statistics` 保存 IA 顶点/图元、VS 调用、送入光栅化的图元、PS 调用、CS 调用计数。当前只在显式启用细测的第 30 帧采集，其他帧或查询/回读失败时 `available=false`，此时零值不代表没有工作。DX12 在 Device 侧延迟创建查询堆和带显存统计的 Readback Buffer，围住每个平铺的 RDG Pass 查询，在已有提交 fence 完成后回读，不额外插入 CPU 等待。

这些计数来自 [D3D12 Pipeline Statistics](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics)，不是提交的 Draw 索引总数、显存流量或执行周期。PS 调用数不能直接当作精确 overdraw；判断 ALU、纹理或带宽瓶颈仍需硬件性能计数器。

## CPU 内存

`memory::allocate/deallocate` 提供显式、带分类和对齐的分配入口；失败返回空指针。`memory::resource(Category)` 可以作为 `std::pmr` 容器或 Pool/Arena 的 upstream。PMR 无法返回分配失败，耗尽时终止进程，符合当前引擎不用异常的约定。

统计包含当前请求字节、进程内本分配器的历史峰值、累计分配次数。池使用它作为底层入口时，统计池向系统申请的容量，不再将池内子分配重复计入容量。当前分类为 General、Asset、RenderPacket、RenderGraph、Profiler。会话缓冲区已接入 Profiler 分类。

当前未重载全局 `new/delete`，引擎其他容器和第三方分配尚未全部迁移。报告明确标注覆盖范围；`tracked` 只包含显式入口，`process_private_bytes` 和 `working_set_bytes` 来自进程采样，不能彼此相加。进程内存目前在阶段边界采样，不声称这些采样值覆盖所有加载瞬时峰值。

## Device 显存

运行时原生分配信息仅由 Device 维护。DX12 的 Buffer、Texture、动态上传缓冲、静态上传暂存、纹理暂存和 Timestamp Readback 经过同一个分配入口。启动和退出时由串行初始化/销毁阶段接管。跨线程释放只将分配 ID 和原生所有权移入回收队列，不修改分配表。

帧提交可与 GPU 执行重叠。提交引用与 Upload 暂存按提交区间的 fence 保留；跨线程退休的原生分配在 Device 上关联保守的提交 fence，只有其完成后才移出分配表并物理释放。不能按帧号或 CPU 提交返回时间判断 GPU 已不再使用资源。

`allocated_bytes` 为引擎自建资源的分配需求字节，依据后端的分配大小查询，含对齐；`peak_bytes` 为该 Backend 生命周期内峰值。`cached_bytes` 是 RDG 空闲缓存的子集；`pending_release_bytes` 是等待物理释放的子集。Upload/Readback 也作为子集单列，均不能再次累加到总量。

Swapchain BackBuffer 记录为 `external`，单列 `external_bytes`，不计入引擎自建分配。资源视图不重复计纹理数据。Descriptor Heap、Pipeline、驱动内部开销尚未归入资源分配字节，操作系统的 local/nonlocal usage/budget 单列，查询失败时 `budget_available=false`。UMA 上这些维度不能直接理解为独立物理显存。没有 Device 数据的内存采样使用 `gpu_available=false`。

`RHIBackend::get_memory_snapshot()`、`memory_id()` 和 `set_memory_cached()` 都是 Device 侧接口。其他线程通过 `run_device_task` 请求数据副本，Profiler/UI 不访问对象表或原生指针。

## RenderGraph

每个实际执行的图保存 Pass ID、类型、顺序、剔除标志、编译后的依赖，以及资源访问和 lifetime。Device 解析资源时补充 `allocation_id`，对应显存快照中的分配记录；不保存原生指针。

多个逻辑资源可以映射到同一个分配 ID。当前是资源对象池复用，尚未实现 Heap 区间 aliasing。图、GPU 记录和分配明细会增加采集内存及 CPU 成本，适合有限帧数的诊断采集。

## 验证

`xmake run utest "[profile]"` 执行工作线程上的带分类分配、真实 GPU 绘制与回读，并验证导出的 JSON 能关联任务、GPU 帧、RenderGraph 和内存采样。报告保存在 `build/test-results/functional-profile.json`。性能指标只报告，不用固定毫秒阈值断言。
