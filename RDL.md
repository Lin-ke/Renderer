# RHI 与 RDG 模块实现分析与功能评估

基于对当前项目下 `engine/function/render/rhi` 与 `engine/function/render/graph` (RDG) 源码的结构梳理与接口审查，对该（模仿的）渲染基础设施是否达到类似 ToyRenderer 的现代渲染器标准进行了详细的拆解分析。

## 1. RHI (Render Hardware Interface) 模块分析

从 `rhi.h` 和 `rhi_structs.h` 的定义来看，本项目的 RHI 层已经实现了对现代图形 API (如 Vulkan / DirectX 12) 的底层抽象，接口设计具备相当的完备性。

### 1.1 基础资源 (Texture, Buffer, Sampler, Shader)
* **Texture (纹理)**: 实现了完整的纹理系统，支持 2D/3D、Cube 纹理创建，支持丰富的像素格式 (`RHIFormat`) 映射。包含 `RHITextureView` 以支持多视图机制，并且提供了 Mipmap 的即时生成接口 (`generate_mips`)。
* **Buffer (缓冲)**: 支持各种用途的缓冲创建，例如 Vertex、Index、Uniform、Storage (RW Buffer) 等等。支持多种 CPU/GPU 内存交互语义 (`MemoryUsage` 标志位映射可见性)。
* **Shader (着色器)**: 提供 `compile_shader` 在线/离线编译支持，并支持通过 `ShaderBindingTable` 管理光线追踪着色器组。

### 1.2 资源绑定与管线 (Pipelines & Descriptors)
* **Root Signature / Descriptor Sets**: 采用类似 DX12/Vulkan 的现代绑定模型。引入了 `RHIDescriptorSet` 抽象并配合 `RHIRootSignature` 来解耦着色器资源绑定。
* **Pipelines**: 管线抽象非常全面。分离出了三种独立管线：
  * **Graphics Pipeline** (`RHIGraphicsPipeline`)
  * **Compute Pipeline** (`RHIComputePipeline`)
  * **Ray Tracing Pipeline** (`RHIRayTracingPipeline`)

### 1.3 命令与上下文 (Command List & Context)
* **指令录制**: 支持通过池化管理器 (`RHICommandPool`) 获取执行上下文 (`RHICommandContext`)，支持标准绘制 (`draw`, `draw_indexed` 及 indirect 变种)、计算调度 (`dispatch`) 和光追调度 (`trace_rays`)。
* **Immediate Context**: 针对初始化与非帧周期的静态资源操作，实现了独立的 `RHICommandContextImmediate` 进行快速的内存拷贝、数据搬运与状态转移（详情见 `RD.md` 分析）。

### 1.4 现代渲染特性 (Ray Tracing)
* **底层抽象**: 原生内置了对硬件光线追踪 (DXR/Vulkan RT) 的 RHI 抽象，包含了 `RHITopLevelAccelerationStructure` 和 `RHIBottomLevelAccelerationStructure` 的创建接口，这表明引擎的设计起点较高，完全瞄准了次世代渲染标准。

---

## 2. RDG (Render Dependency Graph) 模块分析

从 `rdg_builder.h` 和相关节点的实现来看，项目引入了基于虚幻引擎 (UE) RDG 理念设计的单帧渲染图架构。

### 2.1 节点系统与图构建
* **资源抽象 (BlackBoard)**: 实现了 `RDGBlackBoard` 字典通过字符串名称索引资源，方便各种 Pass 解耦调用。将所有的图实体抽象为节点（Node）：
  * **资源节点**: `RDGTextureNode`, `RDGBufferNode`
  * **Pass 节点**: `Render`, `Compute`, `RayTracing`, `Present`, `Copy` 
* **Builder 模式**: 提供了语义清晰的链式/构建器模式（如 `RDGRenderPassBuilder`），用户可以在 Setup 阶段声明资源读写（Read/Write/RenderTarget），并在 Execute 阶段编写执行 Lambda。

### 2.2 自动推导与资源管理 (核心价值)
* **资源状态跟踪与自动屏障 (Automatic Barriers)**: 实现了 RDG 最核心的功能之一——根据 Pass 的注册依赖（读/写），RDG Builder 能够在底层 RHI 上下文中自动插入同步屏障 (`buffer_barrier`, `texture_barrier`)，解决了多线程和复杂图结构下手写 Barrier 极易出错的问题。
* **瞬态资源池化 (Transient Resource Pooling)**: 实现了帧内资源的自动分配与别名重用（基于 `RDGPool` 机制），大幅度优化了显存占用开销。

---

## 3. 功能完成度评估与总结 (vs ToyRenderer)

综合来看，该项目不仅达到了现代引擎架构的标准，在设计抽象上也基本与业界公认优秀的 FrameGraph/RDG 框架（如 ToyRenderer 所使用的标准架构）保持了高度一致和对齐。

### 3.1 已实现的核心能力 (Implemented)
1. **多范式管线支持**：全面囊括了光栅化、计算着色与硬件光线追踪的 RHI 抽象支持。
2. **自动化同步机制**：RDG 成功实现了自动资源屏障的推导与注入，简化了渲染流程的开发。
3. **显存生命周期优化**：利用 RDG 的瞬态内存池避免了显存碎片的产生，提高了显存带宽利用率。
4. **Immediate 旁路系统**：优雅分离了帧内渲染与静态资源传输之间的指令上下文。

### 3.2 仍需完善的领域 (TODOs)
根据 `rdg_builder.h` 的内部注释以及实现现状，该 RDG 虽然基础功能已经跑通，但在高阶优化特性上仍有待发掘，主要包括：
1. **Pass Culling (节点剔除)**：目前暂未实现无用/无副作用 Pass 的自动剔除（死代码消除）。
2. **Topological Sort (拓扑排序)**：目前的 Pass 似乎还是按照声明顺序执行，后续需演进为基于依赖的拓扑排序以达到最优并发。
3. **Async Compute (异步计算)**：暂未打通 RDG 支持多队列（Multi-queue）并行调度 Compute Shader 和 Graphics 绘制。
4. **Multi-threaded Command Recording**：暂时还不支持在 RDG 层对 Pass 的 execute lambda 进行多线程分发录制。

**结论**：本项目在 RHI 和 RDG 的基础架构搭建上十分扎实，整体 API 设计干净利落且贴合现代硬件逻辑，已经实现了类似 ToyRenderer 的主流渲染图能力，只需后续补齐图编译器级别的管线拓扑优化即可达到工业级标准。