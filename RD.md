# RHI中Immediate和Context的作用及与RDG的联动分析

基于对 `engine/function/render/rhi` 和 `engine/function/render/graph`（RDG）的源码分析，以下是关于 `Immediate` 和 `Context` 的功能定位以及它们如何与渲染依赖图（RDG）协作的详细总结。

## 1. Context (`RHICommandContext` & `RHICommandList`) 的作用
* **功能定位**：`Context` 是硬件底层命令缓冲区的抽象封装（对应于 Vulkan 的 `VkCommandBuffer` 或 DirectX 12 的 `ID3D12GraphicsCommandList`）。它提供了所有基础渲染命令的录制接口，例如设置管线（`set_graphics_pipeline`）、绑定资源（`bind_descriptor_set`）、绘制（`draw`）、计算调度（`dispatch`）等。
* **机制与管理**：`RHICommandContext` 对象由底层的 `RHICommandPool` 进行分配、池化和回收管理。它通常与 `RHICommandList` 结合使用，由 `RHICommandList` 负责收集/代理命令，随后统一提交给 GPU 执行，支持高效的多线程命令录制。

## 2. Immediate (`RHICommandContextImmediate` & `RHICommandListImmediate`) 的作用
* **功能定位**：这是一种专用的**即时执行命令上下文**。它绕过了复杂的渲染流程，用于脱离帧渲染循环（Frame Graph/RDG）的同步或一次性 GPU 操作。
* **主要使用场景**：
  * **资源初始化与数据传输**：CPU与GPU之间的数据拷贝（如 `CopyBufferToTexture`、`CopyTextureToBuffer`、`CopyBuffer`）。
  * **贴图处理**：即时的 Mipmap 生成（`GenerateMips`）。
  * **即时状态转换**：强制的资源屏障（`TextureBarrier`、`BufferBarrier`）。
* **机制**：这些命令不需要被缓冲到复杂的依赖图中，而是在资源加载阶段（如加载模型、纹理时）以最直接、同步的方式向硬件提交，确保在渲染循环开始前资源已经完全准备就绪（就绪态）。

## 3. 与 RDG（Render Dependency Graph）的联动机制
RDG 和 RHI（Context / Immediate）共同构成了引擎的渲染基建，它们通过明确的职责划分进行协同联动：

* **RHI 层（机制）与 RDG 层（策略）的分离**：
  * **RHI** 提供执行渲染指令的工具（即 `Context`）和直接操作硬件内存的通道（即 `Immediate`）。
  * **RDG** 负责高层策略，通过分析资源（Buffer/Texture）的读写图，自动推导并插入正确的同步屏障（Barriers），进而安排 Pass 的执行顺序。
* **Context 在 RDG 中的流转 (`RDGPassContext`)**：
  * 在 RDG 的执行编译阶段（`RDGBuilder::execute`），引擎会为每个 `RDGPassNode` 生成一个执行环境上下文 —— `RDGPassContext`。
  * `RDGPassContext` 内部包含了一个标准的 `RHICommandListRef`。当调用开发者为该 Pass 编写的 Lambda 执行函数（`RDGPassExecuteFunc`）时，开发者便是通过这个传入的 `Context` 来录制具体的 `Draw` 或 `Dispatch` 命令。
  * RDG 在调用各个 Pass 的执行回调前，会利用 `Context` 自动下发前置的 Barrier 指令，然后在回调中收集 Draw Call，最后结束录制并提交。
* **Immediate 与 RDG 的协同分工**：
  * **Immediate 是 RDG 的前置保障**：在 RDG 构建每帧画面之前，`Immediate` 接口被用来把静态网格体数据、纹理图片等“搬运”到显存中，并生成好 Mipmap。
  * **RDG 聚焦于每帧渲染拓扑**：当资源通过 `Immediate` 准备完毕后，每帧的动态渲染（如 Deferred Lighting、Depth Pre-pass 等）则全部交由 RDG 接管，RDG 将复杂的拓扑关系编译为一系列高效的、基于标准 `Context` 的底层渲染指令进行异步绘制。

### 总结
* **Immediate** 用于“帧外”的同步资源加载与数据准备。
* **Context** 是“帧内”绘制指令的核心载体。
* **RDG** 作为上层调度器，在帧内利用 **Context** 编排和录制基于依赖图的渲染指令；而 **Immediate** 负责在系统初始化或资源加载阶段为 RDG 提供准备完毕的数据基座。