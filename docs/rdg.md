# RDG 编译、排序与剔除

`RDGBuilder::execute()` 先编译依赖图，再执行保留的 Pass。`compile()` 可单独调用，不分配 GPU 资源、不调用执行回调。两者都返回 `bool`；错误详情在 `get_compile_info().error`。编译失败不记录任何 Pass 的 GPU 命令。

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

构建和验证：`xmake build -a`、`xmake run utest "[rdg_compile]"`、`xmake run utest`。测试保持隐藏窗口运行，覆盖乱序创建、覆盖写入、LOAD、资源别名、子资源/Buffer 范围、显式依赖、循环、无分配剔除，以及 DX12 输出像素和导出生命周期。
