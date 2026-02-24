# ToyRenderer 与 本项目 RDG TODOs 实现状态对比

根据对本项目（模仿）的 RHI/RDG 源码，以及原版 `ToyRenderer` (`E:\ToyRendererenderer\src\Runtime\Function\Render\RDG`) 的源码调查比对，以下是各项 TODO 特性的实现状态总结。

## 1. 原版 ToyRenderer 的 TODO 清单
在原版 `ToyRenderer` 的 `RDGBuilder.h` 开头注释中，明确列出了当时尚未完成的特性（由于中文字符编码问题，这里结合上下文还原了语意）：
> 目前的RDG只实现了最基本的功能，相当多特性还未完成，例如：
> pass排序，pass剔除，多线程录制，multi queue，资源池GC，细粒度的资源处理（内存对齐，subresource屏障等），……

对比我们本地（`D:\Renderer\engine\functionender\graphdg_builder.h`）列出的 TODOs，两者是高度一致的：
- Pass Culling (stripping unused passes)
- Async Compute / Multi-queue support
- Fine-grained subresource barriers
- Multi-threaded command recording
- Topological Sort

---

## 2. 各项特性的实现状态调查

通过代码搜索，我们对 `ToyRenderer` 原代码仓库进行了考证，以确认它在后续版本中是否补全了这些 TODO，以及本项目的同步情况：

### 2.1 Pass Culling (Pass 剔除)
* **ToyRenderer**: **已部分实现 / 尝试实现**。在 `ToyRenderer` 的 `RDGBuilder.cpp` 的 `Execute()` 循环中，存在代码：`if(pass->isCulled || !pass) continue;`，并且上一行注释了 `// TODO 还没做剔除`。这意味着原作者设计了 `isCulled` 的标志位和基础的跳过逻辑，但真正的图遍历剔除算法可能尚未彻底实装。
* **本项目**: **未实现**。本地实现中没有看到 `isCulled` 标志位或相关的剔除图遍历逻辑。

### 2.2 Pass 排序 (Topological Sort / 拓扑排序)
* **ToyRenderer**: **未实现**。代码中没有引入拓扑排序的图遍历逻辑。目前的执行顺序依然是依赖于用户 `CreateRenderPass` 调用的先后顺序。注释中提及“假定所有pass的添加顺序就是执行顺序，方便处理排序和依赖关系等”。
* **本项目**: **未实现**。与 ToyRenderer 保持一致，严格按照声明顺序执行。

### 2.3 多线程命令录制 (Multi-threaded command recording)
* **ToyRenderer**: **未完全实现 (在 RHI/RDG 层面上)**。虽然查到类似 `EngineContext::ThreadPool()->ThreadFrameIndex()` 的代码，这表明引擎层引入了线程池，并利用它来拉取并分配每帧、每线程的 `DescriptorSet`，但这主要用于多帧同步。真正的“多线程并发调用 Lambda 录制 CommandList 并最后拼装”尚未实装。
* **本项目**: **未实现**。同样受限于现有的串行执行逻辑。

### 2.4 Multi-queue / Async Compute (多队列异步计算)
* **ToyRenderer**: **未实现**。未发现跨队列（Graphics / Compute / Transfer）同步信号量调度机制。
* **本项目**: **未实现**。

### 2.5 Subresource Barriers (细粒度子资源屏障)
* **ToyRenderer**: **已支持 (数据结构层面)**。在原项目的 `RDGBuilder.cpp` 中存在提取 `edge->subresource` 并生成具体 Barrier 的逻辑（如传递 `layerCount` 等），说明它在资源的 mip/layer 粒度屏障上有了一定的实现进度。
* **本项目**: **尚未完善**。本项目中对应的 Barrier 更多是针对整个资源的粗粒度状态转换。

---

## 3. 结论

1. **项目状态对齐**：目前的这个（模仿的）渲染基础设施在**骨架和核心逻辑上**基本与原版 `ToyRenderer` 对齐。原作者遗留的核心技术债务（TODOs），本项目同样继承了这些待办事项。
2. **细微差异**：`ToyRenderer` 源码在后期可能经历了一些零散的迭代（比如在数据结构上增加了 `isCulled` 字段，开始处理 `subresource` 粒度的状态等），而本项目的代码似乎定格在了一个更为整洁、初始的版本架构上。

这表明如果要在本项目上继续深耕渲染架构，这些 TODOs 仍是接下来极具挑战性和价值的优化方向。