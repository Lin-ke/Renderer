# RDG 编译、排序与剔除

`RDGBuilder::execute()` 先编译依赖图，再执行保留的 Pass。`compile()` 可单独调用，不分配 GPU 资源、不调用执行回调。两者都返回 `bool`；错误详情在 `get_compile_info().error`。编译失败不记录任何 Pass 的 GPU 命令。

在线程化帧循环里，Game Thread 在更新场景后生成 `RenderPacket`。Render Thread 对快照剔除、组批和构图，再运行 `compile()`。编译会记录保留 Pass 的顺序以及资源的首次和末次使用。Device Thread 从 swapchain 获取本帧纹理，绑定图中用规格声明的 `BackBuffer`，通过 `execute_compiled()` 录制已有计划，并提交和 Present。`RDGPassContext::frame_slot` 标识 Device Thread 取得的帧资源槽。

`game` 和 `sun_temple` 默认走这条路径。`EngineContext::render_frame()` 入队后返回，满队列提供背压。Device 异步提交帧，复用帧资源前等待对应 GPU fence。`EngineContext::wait_for_render()`、截图和退出等待 CPU 队列与 GPU 都完成，并收集末尾 GPU 查询。单线程模式使用同样的快照、构图和执行入口；直接调用 `RenderSystem::tick()` 或 `execute_frame()` 后需要 GPU 结果时，也须显式等待或调用截图入口。

## Bloom 构图示例

`render::BloomPass` 在初始化时创建持久的 Shader、Pipeline、RootSignature 和 Sampler。Render Thread 调用 `build()` 声明三张半分辨率 RGBA16F 纹理，以及 `BloomBright → BloomBlurX → BloomBlurY → BloomComposite` 四个 Pass。回调按值持有 Pipeline、参数和 Sampler，实际纹理、视图和 DescriptorSet 在 Device Thread 执行时由 RDG 准备。

```cpp
auto scene_color = graph.create_texture("SceneColor").extent({width, height, 1})
    .format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
// 场景 Pass 向 scene_color 写入，并声明自己的资源访问。
if (!bloom.build(graph, scene_color, back_buffer, {width, height}, settings)) return false;
if (!graph.compile()) return false;
// 将图交给 Device Thread，绑定实际 back_buffer 后调用 execute_compiled()。
```

`BloomSettings` 随 `RenderPacket` 拷贝。当前光照输出已经经过 tone mapping 和 gamma，Bloom 将输入近似转换回线性空间后提取亮部、模糊并叠加，再编码回 gamma；threshold 在该线性空间比较。合成完成后才绘制 UI。开启 Bloom 时，场景写入同格式的暂态 `SceneColor`，`BackBuffer` 保持为最终输出；自定义场景 Pass 应写入 `graph.get_texture("SceneColor")`。

`[bloom]` 测试覆盖两条线程上的构图和录制、延迟绑定外部输出、参数快照、Pass 对象提前释放、奇数尺寸和 1×1 输入，以及高阈值、零强度和亮部扩散的像素验证。

## 依赖与执行顺序

- 根据 Shader 读写、颜色/深度附件、copy 和 present 声明建立数据依赖。
- 对重叠范围保留 RAW、WAR、WAW 约束。纹理按 mip/layer/aspect 范围，Buffer 按字节范围分析。
- 同一 RHI 对象导入为多个名称时，共享依赖分析，避免别名漏掉读写冲突。
- 使用稳定 Kahn 拓扑排序：可执行的 Pass 中优先选择最早创建的 Pass。
- 屏障和资源最后使用点按编译后的执行序号计算，忽略剔除的 Pass。

现有句柄表示可变资源：重叠写入按创建顺序定义版本。读取消费最近的前序写入；暂态资源的纯读取如果创建在生产者前面，会绑定第一个后序生产者并将其排到读取之前。导入资源的首次读取使用外部初始内容，不会绑定之后的写入。附件 `LOAD` 需要先前内容；首次 UAV Pass 可以初始化自己的资源，后续 `read_write` 则依赖前一次写入。若要使用资源的不同版本，应使用不同句柄。

声明的资源访问必须覆盖回调实际使用的资源。仅在回调中调用 `resolve()` 不会自动声明依赖。

```cpp
RDGBuilder graph(command);
auto intermediate = graph.create_texture("Intermediate")
    .extent({width, height, 1}).format(FORMAT_R8G8B8A8_UNORM)
    .allow_render_target().finish();
auto external = graph.create_texture("External")
    .import(output_texture, RESOURCE_STATE_COMMON).finish();

// 消费者可以先创建，编译后执行顺序为 Producer -> Copy。
graph.create_copy_pass("Copy").from(intermediate).to(external);
graph.create_render_pass("Producer")
    .color(0, intermediate, ATTACHMENT_LOAD_OP_CLEAR,
           ATTACHMENT_STORE_OP_STORE, {0.25f, 0.5f, 0.75f, 1.0f});

if (!graph.execute()) ERR(LogRenderSystem, "{}", graph.get_compile_info().error);
```

## Pass 剔除

从以下可见结果反向保留数据依赖：

- 对导入资源的写入，保守视为外部可见副作用。
- Present Pass。
- `extract_texture()` / `extract_buffer()` 请求提取的输出。
- `.never_cull()` 标记的 Pass。

没有通向这些结果的数据依赖的 Pass 不执行，也不为其分配纹理、Buffer、视图或描述符。暂态附件被后续 `CLEAR` 完全覆盖时，早期写入可以剔除；`LOAD` 会保留所需的早期写入。纯排序约束不会把已经无用的 Pass 重新变成活跃 Pass。

查询、CPU 回调等图外副作用必须显式声明：

```cpp
auto upload = graph.create_compute_pass("Upload")
    .never_cull().execute(upload_callback).finish();
graph.create_compute_pass("Query")
    .never_cull().depends_on(upload).execute(query_callback);
```

`.depends_on()` 同时保留被依赖的 Pass。循环依赖会报告涉及的 Pass 名称；不可达的死分支中的循环可以整体剔除。活跃 Pass 读取没有生产者的暂态资源会编译失败。

## 提取与诊断

```cpp
RHITextureRef result;
graph.extract_texture(intermediate, result);
if (graph.execute()) {
    // result 持有本次图的输出，接着提交 command。
}
```

输出引用必须存活到 `execute()` 返回。提取的资源不返回暂态资源池，避免之后的图覆盖外部保留的输出；普通暂态资源在最后一次实际使用后返回池。

`get_compile_info()` 保留执行名称列表和剔除名称列表，执行后仍可查看。`get_execution_passes()` 仅在编译后、执行清空图之前有效。Graphviz 导出会标出执行序号及被剔除的 Pass；编辑器图视图按实际执行顺序显示活跃 Pass。

构建和验证：`xmake build -a`、`xmake run utest "[rdg_compile]"`、`xmake run utest`。测试保持隐藏窗口运行，通过乱序 copy 链、外部资源延迟绑定、资源导出和实际 GPU 像素验证依赖与生命周期；不再单独测试编译器中间容器状态。

## Profile 数据

启用 `ProfileSession` 时，Device 执行保存带 `frame_id` 的图快照，包含编译依赖、访问、剔除和 lifetime，并在实际 resolve 时补充物理 `allocation_id`。GPU Timestamp 的 `pass_id` 与该图一致；Profiler 消费值快照。详见 [profile.md](profile.md)。
