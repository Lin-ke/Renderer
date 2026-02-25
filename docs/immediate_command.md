# 渲染后端的命令执行范式：立即执行与延迟执行

在现代图形渲染引擎中，GPU命令的提交与执行通常可以归结为两种范式：**立即命令（Immediate Command）** 和 **延迟命令（Deferred Command）**。

1. **立即命令 (Immediate Command)**：通常用于资源初始化、数据上传（如纹理加载、顶点数据拷贝）或需要 CPU 立即获取 GPU 计算结果（如 Readback）的场景。要求提交后 CPU 阻塞等待 GPU 执行完毕，确保后续操作可以直接使用这些资源。
2. **延迟命令 (Deferred Command)**：通常用于每帧的常规渲染循环（Draw Calls, 状态切换等）。CPU 负责将命令记录（Record）到命令缓冲区中，在帧的特定时刻统一提交给 GPU 执行。CPU 不需要立即等待 GPU 执行完成，而是通过多缓冲（Double/Triple Buffering）和同步原语（Fences, Semaphores）与 GPU 异步并行工作。

下面我们将分别针对 **DirectX 11 (DX11)** 和 **Vulkan** 后端，详细讨论这两种范式的实现写法。

---

## 1. DirectX 11 (DX11) 后端

DirectX 11 的 API 设计本身相对偏上层，驱动程序在底层做了大量隐藏的队列和同步管理。在 DX11 中，命令的执行依赖于 `ID3D11DeviceContext`。

### 1.1 立即命令（Immediate Command）写法
在 DX11 中，主渲染线程总是拥有一个**立即上下文 (Immediate Context)**。大部分资源上传操作看起来是同步的，实际上驱动会将其放入队列，但从 API 层面可以直接调用并假设其完成。

**资源上传示例（使用 `UpdateSubresource` 或 `Map/Unmap`）：**
```cpp
// 假设 device 和 immediateContext 已经初始化
// 1. 使用 UpdateSubresource (适合较小的一次性上传)
immediateContext->UpdateSubresource(pTexture, 0, nullptr, pData, rowPitch, depthPitch);

// 2. 使用 Map/Unmap (适合动态资源，如 Dynamic Buffer)
D3D11_MAPPED_SUBRESOURCE mappedResource;
immediateContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
memcpy(mappedResource.pData, pData, dataSize);
immediateContext->Unmap(pBuffer, 0);
```
*同步保证：* DX11 的驱动会自动管理依赖。当你上传数据后紧接着发起 Draw Call，驱动会确保上传完成再绘制。
如果确实需要强制 CPU **等待** GPU 执行完毕（例如 Readback 回传数据到 CPU），需要使用事件查询（Event Query）：
```cpp
// 创建查询
D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
ID3D11Query* pQuery = nullptr;
device->CreateQuery(&queryDesc, &pQuery);

// 发出命令后，插入一个 End
immediateContext->End(pQuery);

// CPU 阻塞轮询，直到 GPU 执行到 Query 的位置
BOOL queryData;
while (immediateContext->GetData(pQuery, &queryData, sizeof(BOOL), 0) == S_FALSE) {
    // 阻塞等待，或者可以放弃 CPU 时间片
}
```

### 1.2 延迟命令（Deferred Command）写法
在 DX11 中，延迟命令通常有两种体现：
1. **隐式延迟**：在 Immediate Context 中调用的所有 Draw、Bind 命令实际上都是被驱动缓存并在 Present 或显式 Flush 时才真正提交给硬件。
2. **显式延迟（多线程渲染）**：使用 **延迟上下文 (Deferred Context)**。允许在工作线程中记录命令，生成命令列表，然后在主线程统一提交，以此提升多核 CPU 的利用率。

**多线程延迟上下文示例：**
```cpp
// 1. 在工作线程中：使用 Deferred Context 记录命令
ID3D11DeviceContext* deferredContext;
device->CreateDeferredContext(0, &deferredContext);

// 记录各种渲染命令
deferredContext->VSSetShader(...);
deferredContext->Draw(...);

// 2. 结束记录，生成 Command List
ID3D11CommandList* pCommandList = nullptr;
deferredContext->FinishCommandList(FALSE, &pCommandList);

// 3. 在主线程中：使用 Immediate Context 统一执行
immediateContext->ExecuteCommandList(pCommandList, FALSE);

pCommandList->Release();
```

---

## 2. Vulkan 后端

Vulkan 是一种极其底层的显式 API。没有“默认的隐式上下文”，所有的命令（包括上传、渲染）**必须**由开发者显式记录到命令缓冲区（`VkCommandBuffer`）中，然后提交给队列（`VkQueue`）。因此，无论立即命令还是延迟命令，生命周期都是：**分配 -> 记录 -> 提交 -> 同步**。

### 2.1 立即命令（Immediate Command）写法
在 Vulkan 中，立即命令通常被称为“单次提交（One Time Submit）”。为了上传纹理或缓冲区，我们需要创建一个临时 Command Buffer，记录拷贝命令，提交到传输队列或通用队列，并 **强制 CPU 阻塞等待** 操作完成。

**实现范式：**
```cpp
// 1. 分配临时 Command Buffer
VkCommandBufferAllocateInfo allocInfo{};
allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
allocInfo.commandPool = commandPool; // 建议为临时命令设置单独的 Pool，并使用 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
allocInfo.commandBufferCount = 1;

VkCommandBuffer commandBuffer;
vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

// 2. 开始记录 (使用 ONE_TIME_SUBMIT_BIT 提示驱动优化)
VkCommandBufferBeginInfo beginInfo{};
beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
vkBeginCommandBuffer(commandBuffer, &beginInfo);

// ---- 记录具体的命令 ----
// 例如：将 Staging Buffer 的数据拷贝到 GPU Local Buffer
VkBufferCopy copyRegion{};
copyRegion.size = size;
vkCmdCopyBuffer(commandBuffer, stagingBuffer, dstBuffer, 1, &copyRegion);

// (实际中往往还需要配合 vkCmdPipelineBarrier 做 Layout Transition 等)

// 3. 结束记录
vkEndCommandBuffer(commandBuffer);

// 4. 提交到队列
VkSubmitInfo submitInfo{};
submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
submitInfo.commandBufferCount = 1;
submitInfo.pCommandBuffers = &commandBuffer;

vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

// 5. 立即同步：强迫 CPU 阻塞，直到队列空闲 (GPU 执行完毕)
vkQueueWaitIdle(graphicsQueue); 
// 或者使用 Fence 同步 (更推荐，粒度更细，可以避免阻塞整个队列的其他操作):
// vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

// 6. 清理临时 Command Buffer
vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
```
*工程优化注：对于大量资源的加载，频繁分配 CommandBuffer 并调用 `vkQueueWaitIdle` 会导致极大的管线气泡（性能损耗）。现代引擎通常会将多个 Immediate Commands 打包成一个大的 Command Buffer 统一上传，甚至把资源上传作为独立于主渲染队列的异步任务（使其具备 Deferred 的特性）。*

### 2.2 延迟命令（Deferred Command）写法
这是 Vulkan 每帧主渲染循环的标准写法。每一帧对应的 Command Buffer 在渲染开始前重置，记录所有的 Pass 和 Draw Call，在帧末尾提交。此时 **CPU 绝不等待其立即完成**，而是利用 Fence 确保若干帧后（Double/Triple Buffering 下）重用该 Buffer 前 GPU 已经执行完毕。

**实现范式（每帧循环中）：**
```cpp
// 假设有并发帧机制 (例如 MAX_FRAMES_IN_FLIGHT = 2)
// 1. CPU 等待上一轮该帧的 Fence，确保前一次该帧对应的 Command Buffer 已经执行完，可以被安全重写
vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
vkResetFences(device, 1, &inFlightFences[currentFrame]);

// 2. 重置并重新开始记录当前帧的 Command Buffer
vkResetCommandBuffer(commandBuffers[currentFrame], 0);

VkCommandBufferBeginInfo beginInfo{};
beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

// ---- 记录每帧的渲染命令 ----
// vkCmdBeginRenderPass(...)
// vkCmdBindPipeline(...)
// vkCmdDraw(...)
// vkCmdEndRenderPass(...)

// 3. 结束记录
vkEndCommandBuffer(commandBuffers[currentFrame]);

// 4. 提交到队列（带有 Semaphore 和 Fence 的异步提交）
VkSubmitInfo submitInfo{};
submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

// 依赖配置：等待交换链图像准备好 (ImageAvailableSemaphore) 才开始写颜色
VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
submitInfo.waitSemaphoreCount = 1;
submitInfo.pWaitSemaphores = waitSemaphores;
submitInfo.pWaitDstStageMask = waitStages;

// 提交当前记录好的 Command Buffer
submitInfo.commandBufferCount = 1;
submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

// 信号配置：渲染完成信号 (RenderFinishedSemaphore)，通知后续的 Present 引擎可以拿去显示了
VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
submitInfo.signalSemaphoreCount = 1;
submitInfo.pSignalSemaphores = signalSemaphores;

// 提交！传入 InFlightFence。GPU 在后台接管执行，CPU 立刻返回继续做下一帧的逻辑
vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

// 5. 将结果提交显示屏幕 (Present)
// vkQueuePresentKHR(...)
```

---

## 总结比较

| 特性 | 立即命令 (Immediate Command) | 延迟命令 (Deferred Command) |
| :--- | :--- | :--- |
| **应用场景** | 资源加载、Staging Buffer 上传、计算结果回读、一次性初始化操作。 | 场景渲染、每帧 Draw Call 提交、动态计算着色器派发。 |
| **CPU执行行为** | 命令提交后 CPU **强制阻塞**，等待 GPU 确认执行完成才继续往下走。 | 录制后异步提交，CPU **立即返回**继续执行游戏逻辑，通过围栏(Fence)延迟同步。 |
| **DX11 实现思路** | 直接调用 `Immediate Context` 上传方法，驱动自动排队。必要时配合 `Event Query` 阻塞 CPU 实现同步等待。 | 使用 `Deferred Context` 在工作线程录制 Command List，主线程统一下发。帧内 Draw Call 由驱动隐式管理延迟执行。 |
| **Vulkan 实现思路**| 分配临时 `CommandBuffer` -> 录制上传/拷贝命令 -> 提交 -> 调用 `vkQueueWaitIdle` 或 `vkWaitForFences` 阻塞 CPU -> 销毁。 | 预分配循环使用的 `CommandBuffer` 数组 -> 每帧录制渲染命令 -> 提交并附带 `Semaphores` 进行 GPU 内部依赖排序 -> 传入 `Fences` 防止下一次循环过快覆盖数据。 |
