# RDG 编译、排序与剔除

`compile()` 只编译依赖图，不分配 GPU 资源、不调用执行回调。`RDGBuilder` 不持有 Command List；独立调用方可使用 `execute(command)` 编译并记录到调用方提供的 List。错误详情在 `get_compile_info().error`。

在线程化帧循环里，Game Thread 生成 `RenderPacket`，Render Thread 构图并编译。Device Thread 绑定 BackBuffer 等资源，调用 `prepare_pipeline()`，按拓扑顺序正向规划纹理子资源和 Buffer 状态，并串行展开回调、捕获描述符快照。随后 Device 和录制 worker 各用独立 CommandContext 并行录制，Device 按计划顺序一次提交到 graphics queue，最后 Present。`RDGPassContext::frame_slot` 标识帧资源槽。

帧录制上下文不执行绑定或拷贝的隐式转换。Pass 必须声明它实际使用的纹理、顶点、索引、Storage 和间接 Buffer；绘制时动态选择的纹理可以用 `.sample()` 声明，不占用固定描述符槽。CPU 可见常量由描述符快照捕获，不是图内 GPU 写入。未声明的访问以及手工 barrier 会在提交前报错。图内 mip 生成需要逐 mip 声明，不能调用 Immediate mip 生成路径。

资产上传由 Device 先提交，纹理以可读状态交给帧图，上传 staging 由提交 fence 保活。不需要为首次采样等待 GPU 完成；上传与采样由同一 GPU queue 排序。`import()` 本身不做同步，声明的初始状态必须和交接状态一致。用 `set_texture_final_state()` 明确约定图末交接，例如 BackBuffer 返回 `PRESENT`。

`game` 和 `sun_temple` 默认走这条路径。`EngineContext::render_frame()` 入队后返回，满队列提供背压。Device 异步提交帧，复用帧资源前等待对应 GPU fence。`EngineContext::wait_for_render()`、截图和退出等待 CPU 队列与 GPU 都完成，并收集末尾 GPU 查询。单线程模式使用同样的快照、构图和执行入口；直接调用 `RenderSystem::tick()` 或 `execute_frame()` 后需要 GPU 结果时，也须显式等待或调用截图入口。

## Bloom 构图示例

`render::BloomPass` 在初始化时创建持久的 Shader、Pipeline、RootSignature 和 Sampler。Render Thread 调用 `build()` 声明三张半分辨率 RGBA16F 纹理，以及 `BloomBright → BloomBlurX → BloomBlurY → BloomComposite` 四个 Pass。回调按值持有 Pipeline、参数和 Sampler，实际纹理、视图和 DescriptorSet 在 Device Thread 执行时由 RDG 准备。

```cpp
auto scene_color = graph.create_texture("SceneColor").extent({width, height, 1})
    .format(FORMAT_R8G8B8A8_UNORM).allow_render_target().finish();
// 场景 Pass 向 scene_color 写入，并声明自己的资源访问。
if (!bloom.build(graph, scene_color, back_buffer, {width, height}, settings)) return false;
if (!graph.compile()) return false;
// 将图交给 Device Thread，绑定实际 back_buffer 后调用 prepare_pipeline()。
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
RDGBuilder graph;
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

if (!graph.execute(command)) ERR(LogRenderSystem, "{}", graph.get_compile_info().error);
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
if (graph.execute(command)) {
    // result 持有本次图的输出，接着提交 command。
}
```

输出引用必须存活到计划准备完成。提取的资源不返回暂态资源池；普通暂态资源在图末返回池，本帧不进行帧内复用。池中的纹理以 `COMMON` 状态交接。

`get_compile_info()` 保留执行名称列表和剔除名称列表，执行后仍可查看。`get_execution_passes()` 仅在编译后、执行清空图之前有效。Graphviz 导出会标出执行序号及被剔除的 Pass；编辑器图视图按实际执行顺序显示活跃 Pass。

构建和验证：`xmake build -a`、`xmake run utest "[rdg_compile]"`、`xmake run utest`。测试保持隐藏窗口运行，通过乱序 copy 链、外部资源延迟绑定、资源导出和实际 GPU 像素验证依赖与生命周期；不再单独测试编译器中间容器状态。

## Buffer 输入与 Device 延迟绑定

Render 可以用 `create_buffer(name).import(info, initial_state)` 只声明外部 Buffer，编译后由 Device 用 `bind_external_buffer(handle, buffer)` 绑定。物理容量允许大于逻辑需求；stride、memory usage 和资源用途必须兼容。未知的外部别名不能在编译后绑定到两个独立逻辑资源，避免破坏已经编译的依赖；同一物理资源应在图中复用同一 handle。

Render Pass 用 `.indirect_read(args)` / `.indirect_read(count)` 声明 ExecuteIndirect 参数，用 `.vertex_read(vb)` / `.index_read(ib)` 声明输入；Compute Pass 也支持 `.indirect_read()`。这些边参与依赖、剔除、lifetime 和 barrier，不创建 Shader 描述符。

自定义 Buffer 上传/回读通过 `create_copy_pass(name).read_buffer(src).write_buffer(dst).execute(callback).finish()` 声明。回调只通过 `RHICommandList::copy_buffer()` 录制；声明的范围须覆盖实际访问。上传到外部对象表属于图内资源写入，不需要 `never_cull()`。GPU Driven 的完整用例见 [gpu_driven.md](gpu_driven.md)。

## Profile 数据

启用 `ProfileSession` 时，Device 执行保存带 `frame_id` 的图快照，包含编译依赖、访问、剔除和 lifetime，并在实际 resolve 时补充物理 `allocation_id`。GPU Timestamp 的 `pass_id` 与该图一致；Profiler 消费值快照。详见 [profile.md](profile.md)。
