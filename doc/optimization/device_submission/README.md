# Device 录制与异步提交优化

基于 [GPU Profile](../gpu_profile/README.md) 中确认的重复 View 创建、描述符准备和逐帧等待，2026-11-18 实现。本轮保留工作区已有的独立 CommandContext 和另一任务加入的提交同步保护，未修改 GBuffer、Depth 或 Bloom 的 Shader 与渲染算法。

## 主要改动

1. **复用 View 与纹理描述符表。** DX12Texture 按格式、View 类型、mip/layer/aspect 保存 TextureView 弱缓存，避免每个 Draw 新建包装对象。同一提交区间内，单元素纹理和 Sampler 表复用 shader-visible 描述符。访问时仍检查状态和 UAV barrier；数组继续走连续表分配。
2. **保留快照，减少重复拷贝。** DescriptorSet 绑定仍捕获常量和资源快照。后端使用快照字节的只读 span，移除第二次临时 vector 拷贝；相同 RootSignature 不再重复比较整个布局，每个集合只保留一次提交引用。
3. **异步提交与按 fence 复用。** `execute(fence, ...)` 入队后返回，复用 allocator 前等待其上次提交。三个提交区间分别保护 shader-visible 描述符范围、动态上传范围以及提交引用。GPU 完成后才能回收；跨线程退休的原生分配也要等保守关联的 fence 完成。
4. **收尾与读回语义。** 不传 fence 的 `execute()`、Immediate `flush()`、读回和 `wait_idle()` 继续等待。每个上下文的读回整理回调跟随其实际提交 fence，在队列完成顺序中执行，不被另一个尚在录制的上下文阻塞。`wait_for_render()` 排空 CPU 队列并等待 GPU；截图、退出与 Profile 收尾都使用这条路径。
5. **修正 Present 与 Profile。** 已完成 Present 状态转换的帧不再创建空 Immediate 提交并等待。必要的 Immediate 命令异步入队，保留 `Present(1,0)`。Profiler 在对应帧 fence 完成后读取查询，关联原始 frame_id，并收集末尾在途帧。

Device 仍通过 RHI 录制和提交；原生 DX12 逻辑保留在后端。Render Thread 继续负责构图、编译。没有新增异步多队列或并行原生命令录制。

## 采样结果

Windows ARM64、Adreno X1-85、Release、1280×720、Bloom 开启、隐藏窗口、固定初始相机。每次运行 330 帧，统计帧 30～329，不启用逐绘制细测；基准前后独占运行，无同时构建或测试。基准二进制在修改前保存，前后使用相同 Shader 和资源。未锁定系统频率。

| 运行 | 修改前 FPS | 修改后 FPS | 修改前 Submit ms | 修改后 Submit ms |
|---|---:|---:|---:|---:|
| 1 | 31.4 | 60.1 | 13.598 | 0.743 |
| 2 | 51.6 | 61.5 | 8.436 | 0.577 |
| 3 | 42.7 | 60.1 | 12.047 | 0.519 |

修改后 Present 的均值为 5.00～8.14 ms，当前同步间隔下仍可能节流，不能将其全部视为引擎 CPU 计算。完成时间是 Device 观察到 fence 完成的时间，少量批量收集和调度会影响有限窗口的吞吐率，结果应理解为约 60 FPS。

三次修改前 GPU 均值为 11.73、7.08、10.50 ms，修改后为 7.46、6.84、7.24 ms。存在明显运行间波动，本轮不将 GPU 时间变化归因于 Shader 优化，也不据此宣布解决了 GBuffer 的全部 GPU 成本。

第 30 帧的独立细测：

| GBuffer 指标 | 修改前 | 修改后 |
|---|---:|---:|
| TextureView 实际新建次数 | 7,140 | **0** |
| Descriptor Snapshot 次数 | 1,020 | 1,020 |
| Flush Bindings 累计 | 2.324 ms | **1.403 ms** |
| Pass 录制墙钟 | 6.607 ms | **4.531 ms** |

细测只用于定位，每次诊断帧有额外开销，不能把这些值直接当成稳态收益。

前后六次采样各有 330 个不同 frame_id 的 GPU 记录、330 个图，CPU 事件零丢失。六张截图 RGB 逐像素一致。随后在读回完成队列修正后的最终版本又运行 330 帧，得到 60.1 FPS、Device 16.35 ms、GPU 7.00 ms。

本机报告位于忽略的构建目录：[重复运行汇总](../../../build/test-results/device-optimization-runs.json)、[最终 Profile](../../../build/test-results/device-after-final.json)、[最终截图](../../../build/test-results/device-after-final.png)。

## 验证与边界

新增真实 GPU 功能测试覆盖 18 次异步提交、三组帧资源循环复用、即时/延迟命令列表、同一 CPU 常量缓冲的逐次更新、纹理绑定、强制资源 Tick，以及独立和连续覆盖同一目标的读回。另一个用例验证有上下文继续录制时，已提交的纹理读回仍能正确访问。

Release 与启用 DX12 调试层的 Debug 全量测试均通过：各 41 个用例、621 条断言。日志为 `build/device-optimization-utest-release-final.log` 和 `build/device-optimization-utest-debug.log`，Debug 配置已恢复。

三个区间共用原有 64 MiB 上传缓冲和描述符堆，临时上传容量约为每区间 21.3 MiB。超限时返回分配失败，不覆盖在途资源。仍由 Device 串行使用；本轮没有扩展为通用并行 RHI 录制器。GBuffer 的 MRT、几何处理和着色成本仍需后续单独分析。
