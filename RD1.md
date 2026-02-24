# ToyRenderer RHI & RDG 架构分析

## 一、RHI 架构 - Immediate vs Context

### 1.1 文件结构

```
E:\ToyRenderer\renderer\src\Runtime\Function\Render\RHI\
├── RHI.h                     # 核心抽象：Backend + Context + Immediate
├── RHICommandList.h          # 命令列表（延迟录制 vs 立即执行）
├── RHIResource.h             # RHI资源定义
├── RHIStructs.h              # 数据结构
└── VulkanRHI\                # Vulkan后端实现
```

### 1.2 双上下文设计

| 特性 | **RHICommandContext** | **RHICommandContextImmediate** |
|------|----------------------|-------------------------------|
| **执行方式** | 延迟执行（Begin → 录制 → End → Execute） | 立即执行（Flush） |
| **使用场景** | 渲染循环内的绘制命令 | 资源上传、初始化、拷贝 |
| **支持命令** | 完整渲染管线（Draw/Dispatch/RenderPass） | 仅资源操作（Copy/Barrier/GenerateMips） |
| **状态管理** | 维护完整渲染状态上下文 | 无渲染状态 |
| **获取方式** | `CommandPool->CreateCommand()` | `Backend->GetImmediateCommand()` |

### 1.3 核心代码对比

**RHICommandContext - 延迟录制上下文**
```cpp
class RHICommandContext : public RHIResource {
public:
    virtual void BeginCommand() = 0;
    virtual void EndCommand() = 0;
    virtual void Execute(RHIFenceRef fence, ...) = 0;
    
    // 完整渲染管线API
    virtual void SetGraphicsPipeline(RHIGraphicsPipelineRef pipeline) = 0;
    virtual void BindDescriptorSet(RHIDescriptorSetRef descriptor, uint32_t set) = 0;
    virtual void DrawIndexed(...) = 0;
    virtual void Dispatch(...) = 0;
    virtual void BeginRenderPass(RHIRenderPassRef renderPass) = 0;
    virtual void EndRenderPass() = 0;
};
```

**RHICommandContextImmediate - 立即执行上下文**
```cpp
class RHICommandContextImmediate : public RHIResource {
public:
    virtual void Flush() = 0;  // 立即提交执行
    
    // 仅资源操作（无渲染状态API）
    virtual void TextureBarrier(const RHITextureBarrier& barrier) = 0;
    virtual void BufferBarrier(const RHIBufferBarrier& barrier) = 0;
    virtual void CopyBuffer(...) = 0;
    virtual void CopyTexture(...) = 0;
    virtual void GenerateMips(RHITextureRef src) = 0;
};
```

### 1.4 设计意图

```
┌─────────────────────────────────────────────────────────────┐
│                     渲染循环 (Render Thread)                   │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  RHICommandContext (延迟录制)                          │  │
│  │  - 每帧创建新的CommandList                             │  │
│  │  - 批量录制所有Pass的绘制命令                           │  │
│  │  - 最后统一Execute提交给GPU                            │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              
┌─────────────────────────────────────────────────────────────┐
│                    资源加载 (Load Thread)                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  RHICommandContextImmediate (立即执行)                  │  │
│  │  - 纹理上传 (stbi_load → UpdateSubresource)            │  │
│  │  - Buffer创建和初始化                                   │  │
│  │  - Mipmap生成                                          │  │
│  │  - 每次操作后立即Flush()                                │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、RDG 架构设计

### 2.1 文件结构

```
E:\ToyRenderer\renderer\src\Runtime\Function\Render\RDG\
├── RDGBuilder.h        # 构建器主类
├── RDGBuilder.cpp      # 执行流程实现
├── RDGNode.h           # Pass节点 + 资源节点
├── RDGEdge.h           # 资源依赖边
├── RDGPool.h           # 对象池（Texture/Buffer/View/DescriptorSet）
└── RDGHandle.h         # 句柄类型定义
```

### 2.2 核心架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        RDGBuilder                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              RDGDependencyGraph                          │   │
│  │  - 管理 Pass 节点和资源节点的依赖关系                      │   │
│  │  - 自动构建依赖图，通过 Edge 连接                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              RDGBlackBoard (名称注册表)                   │   │
│  │  - passes:   name → RDGPassNodeRef                      │   │
│  │  - textures: name → RDGTextureNodeRef                   │   │
│  │  - buffers:  name → RDGBufferNodeRef                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              RDGResourcePool (对象池)                    │   │
│  │  - RDGTexturePool     : Key=RHITextureInfo              │   │
│  │  - RDGBufferPool      : Key=RHIBufferInfo               │   │
│  │  - RDGTextureViewPool : Key=RHITextureViewInfo          │   │
│  │  - RDGDescriptorPool  : Key=RootSignature+Set           │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              ↓                                  │
│                     RHICommandListRef (来自RHI层)               │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 节点类型层次

```
RDGNode (基类)
├── RDGResourceNode (资源节点)
│   ├── RDGTextureNode     # 纹理资源
│   │   ├── RHITextureInfo info      // 描述信息
│   │   ├── RHITextureRef texture    // 实际RHI资源（延迟分配）
│   │   └── RHIResourceState initState
│   └── RDGBufferNode      # 缓冲资源
│       ├── RHIBufferInfo info
│       ├── RHIBufferRef buffer
│       └── RHIResourceState initState
└── RDGPassNode (Pass节点)
    ├── RDGRenderPassNode      # 渲染Pass
    ├── RDGComputePassNode     # 计算Pass
    ├── RDGRayTracingPassNode  # 光追Pass
    ├── RDGPresentPassNode     # 呈现Pass
    └── RDGCopyPassNode        # 拷贝Pass
```

### 2.4 RDG Edge - 依赖关系定义

```cpp
// RDGEdge 存储资源在Pass中的使用方式和状态
struct RDGTextureEdge {
    RHIResourceState state;           // 要求的状态
    TextureSubresourceLayers subresource; // 子资源范围
    
    // 使用方式标志
    bool asColor = false;             // 作为Color Attachment
    bool asDepthStencil = false;      // 作为Depth/Stencil Attachment
    bool asShaderRead = false;        // 作为Shader只读资源
    bool asShaderReadWrite = false;   // 作为Shader读写资源
    bool asTransferSrc = false;       // 作为传输源
    bool asTransferDst = false;       // 作为传输目标
    
    // 描述符绑定信息
    uint32_t set = 0;                 // DescriptorSet索引
    uint32_t binding = 0;             // Binding点
    uint32_t index = 0;               // Array索引
    
    // Attachment操作
    AttachmentLoadOp loadOp = ATTACHMENT_LOAD_OP_DONT_CARE;
    AttachmentStoreOp storeOp = ATTACHMENT_STORE_OP_DONT_CARE;
    Color4 clearColor = {0, 0, 0, 0}; // Clear颜色
};
```

---

## 三、RDG 与 RHI 的联动机制

### 3.1 架构分层

```
┌─────────────────────────────────────────────────────────────────┐
│  User Layer (用户代码)                                           │
│  builder.CreateRenderPass(...).Execute([](RDGPassContext ctx) {  │
│      ctx.command->DrawIndexed(...);  // ← 使用RHICommandList      │
│  });                                                              │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  RDG Layer (Render Dependency Graph)                             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  1. 声明式资源管理 (CreateTexture/CreateBuffer)          │   │
│  │  2. 自动屏障推导 (CreateInputBarriers/CreateOutput)      │   │
│  │  3. 延迟资源分配 (Resolve() 从Pool获取RHI资源)           │   │
│  │  4. 描述符自动绑定 (PrepareDescriptorSet)                │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              ↓ 调用RHI接口
┌─────────────────────────────────────────────────────────────────┐
│  RHI Layer                                                       │
│  ┌──────────────────────┐  ┌──────────────────────────────┐    │
│  │  RHICommandList      │  │  RHICommandListImmediate      │    │
│  │  - 延迟录制模式       │  │  - 立即执行模式                │    │
│  │  - 用于RDG帧内渲染    │  │  - 用于RDG外资源上传           │    │
│  └──────────────────────┘  └──────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              ↓ Execute/Flush
┌─────────────────────────────────────────────────────────────────┐
│  RHIContext Layer (平台实现)                                     │
│  - VulkanRHI / DX11RHI / MetalRHI                                │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 关键桥梁 - RDGPassContext

```cpp
// RDGNode.h
struct RDGPassContext {
    RHICommandListRef command;                                    // RHI命令列表
    RDGBuilder* builder;                                          // Builder引用
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptors; // 预绑定描述符
    uint32_t passIndex[3] = {0, 0, 0};                           // 用户自定义索引
};

// 使用示例
d_builder.CreateRenderPass("Forward")
    .Execute([](RDGPassContext ctx) {
        // 直接通过ctx.command调用RHI接口
        ctx.command->SetGraphicsPipeline(pipeline);
        ctx.command->BindDescriptorSet(ctx.descriptors[0], 0);
        ctx.command->DrawIndexed(indexCount, 1, 0, 0, 0);
    });
```

### 3.3 RenderPass 执行流程

```cpp
void RDGBuilder::ExecutePass(RDGRenderPassNodeRef pass)
{
    // Step 1: 准备描述符集
    //   - 从RDGDescriptorPool分配DescriptorSet
    //   - 从RDGTextureViewPool分配TextureView
    //   - UpdateDescriptorSet绑定资源
    PrepareDescriptorSet(pass);
    
    // Step 2: 准备渲染目标
    RHIRenderPassInfo renderPassInfo = {};
    PrepareRenderTarget(pass, renderPassInfo);
    RHIRenderPassRef renderPass = rhi->CreateRenderPass(renderPassInfo);
    
    // Step 3: GPU调试标记
    command->PushEvent(pass->Name(), color);
    
    // Step 4: 输入资源屏障
    //   - 计算每个输入资源的PreviousState
    //   - 自动插入TextureBarrier/BufferBarrier
    CreateInputBarriers(pass);
    
    // Step 5: 开始RenderPass
    command->BeginRenderPass(renderPass);
    
    // Step 6: 执行用户回调 ← RHI调用发生在这里
    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptorSets
    };
    pass->execute(context);
    
    // Step 7: 结束RenderPass
    command->EndRenderPass();
    
    // Step 8: 输出资源屏障
    CreateOutputBarriers(pass);
    
    // Step 9: 释放资源回Pool
    ReleaseResource(pass);
    
    // Step 10: GPU调试标记
    command->PopEvent();
}
```

### 3.4 自动屏障推导

```cpp
// 输入屏障：转换资源到Pass要求的状态
void RDGBuilder::CreateInputBarriers(RDGPassNodeRef pass)
{
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture){
        if (edge->IsOutput()) return;
        
        // 计算前序状态
        RHIResourceState previousState = PreviousState(texture, pass, edge->subresource, false);
        
        // 自动插入Barrier
        RHITextureBarrier barrier = {
            .texture = Resolve(texture),      // 获取实际RHI资源
            .srcState = previousState,        // 前序状态
            .dstState = edge->state,          // 目标状态
            .subresource = edge->subresource
        };
        command->TextureBarrier(barrier);
    });
}

// 前序状态追踪算法
RHIResourceState PreviousState(textureNode, passNode, subresource, output)
{
    // 1. 遍历该资源的所有关联Pass
    // 2. 找到在当前Pass之前的最后一次使用
    // 3. 考虑子资源范围匹配
    // 4. 返回对应的状态
}
```

### 3.5 延迟资源分配机制

```cpp
// 首次Resolve时从Pool分配实际RHI资源
RHITextureRef RDGBuilder::Resolve(RDGTextureNodeRef textureNode)
{   
    if (textureNode->texture == nullptr)
    {
        // 从对象池分配
        auto pooled = RDGTexturePool::Get()->Allocate(textureNode->info);
        textureNode->texture = pooled.texture;
        textureNode->initState = pooled.state;
    }
    return textureNode->texture;
}

// 使用完毕后归还Pool
void RDGBuilder::Release(RDGTextureNodeRef textureNode, RHIResourceState state)
{
    if (textureNode->IsImported()) return;  // 外部资源不归RDG管理
    
    RDGTexturePool::Get()->Release({textureNode->texture, state});
    textureNode->texture = nullptr;
}
```

---

## 四、完整使用示例

### 4.1 声明式资源管理

```cpp
RDGBuilder builder(commandList);

// 1. 创建纹理资源（仅声明，延迟分配）
auto colorTex = builder.CreateTexture("Color")
    .Extent({width, height, 1})
    .Format(FORMAT_R8G8B8A8_UNORM)
    .AllowRenderTarget()          // 标记可作为RenderTarget
    .Finish();

auto depthTex = builder.CreateTexture("Depth")
    .Extent({width, height, 1})
    .Format(FORMAT_D32_SFLOAT)
    .AllowDepthStencil()
    .Finish();

// 2. 导入外部纹理（如Swapchain BackBuffer）
auto backBuffer = builder.ImportTexture("BackBuffer", swapchainTexture);
```

### 4.2 RenderPass 创建与执行

```cpp
// 3. 创建RenderPass
builder.CreateRenderPass("ForwardLighting")
    .RootSignature(rootSignature)
    // Color Attachment
    .Color(0, colorTex, 
           ATTACHMENT_LOAD_OP_CLEAR,      // Load操作
           ATTACHMENT_STORE_OP_STORE,     // Store操作
           {0.1f, 0.1f, 0.1f, 1.0f})      // Clear颜色
    // Depth Attachment
    .DepthStencil(depthTex, 
                  ATTACHMENT_LOAD_OP_CLEAR,
                  ATTACHMENT_STORE_OP_DONT_CARE,
                  1.0f, 0)
    // Shader资源绑定 (set, binding, index, texture)
    .Read(0, 0, 0, albedoTex)    // t0: Albedo
    .Read(0, 1, 0, normalTex)    // t1: Normal
    .Read(0, 2, 0, armTex)       // t2: ARM
    // 执行回调
    .Execute([](RDGPassContext ctx) {
        // 直接调用RHICommandList接口
        ctx.command->SetGraphicsPipeline(pipeline);
        ctx.command->BindDescriptorSet(ctx.descriptors[0], 0);
        ctx.command->SetViewport(...);
        ctx.command->DrawIndexed(indexCount, instanceCount, ...);
    })
    .Finish();
```

### 4.3 ComputePass（无RenderPass）

```cpp
builder.CreateComputePass("PostProcess")
    .RootSignature(computeRootSig)
    .ReadWrite(0, 0, 0, colorTex)     // UAV读写
    .Read(0, 1, 0, bloomTex)          // SRV只读
    .Execute([](RDGPassContext ctx) {
        ctx.command->SetComputePipeline(computePipeline);
        ctx.command->BindDescriptorSet(ctx.descriptors[0], 0);
        ctx.command->Dispatch(groupCountX, groupCountY, groupCountZ);
    })
    .Finish();
```

### 4.4 PresentPass

```cpp
builder.CreatePresentPass("Present")
    .Present(colorTex, backBuffer)
    .Finish();

// 执行整帧RDG
builder.Execute();
```

---

## 五、架构设计要点

### 5.1 延迟录制 vs 立即执行的分工

| 场景 | 使用方式 | 原因 |
|------|---------|------|
| **帧内渲染** | `RDGBuilder` + `RHICommandList` | 需要延迟录制，批量提交，GPU并行 |
| **纹理上传** | `RHICommandListImmediate` | 资源加载是异步的，需要立即可见 |
| **Buffer更新** | `RHICommandListImmediate` | 初始化数据需要立即生效 |
| **Mipmap生成** | `RHICommandListImmediate` | 纹理上传后立即生成mipmap |

### 5.2 RDG 核心价值

1. **声明式资源管理**：用户只声明资源用途，RDG自动处理分配和复用
2. **自动屏障推导**：基于依赖图自动计算资源状态转换
3. **延迟分配**：实际RHI资源在执行时才分配，支持跨帧复用
4. **对象池化**：Texture/Buffer/View/DescriptorSet都有Pool，减少分配开销
5. **描述符自动绑定**：根据`.Read()`/`.ReadWrite()`自动准备DescriptorSet

### 5.3 当前限制（根据代码注释）

```cpp
// RDGBuilder.h 注释说明
// - 当前假定Pass创建顺序 = 执行顺序（无自动排序）
// - 无Pass剔除（Culling）
// - 无多线程录制
// - 无Multi-Queue支持
// - 资源池无GC（冗余资源不会自动删除）
// - 无细粒度的Subresource屏障
```

---

## 六、总结

**RHI双上下文设计**：
- `RHICommandContext` 面向**渲染帧**，延迟录制，维护渲染状态
- `RHICommandContextImmediate` 面向**资源操作**，立即执行，无渲染状态

**RDG与RHI联动**：
- RDGBuilder 持有 `RHICommandListRef`
- 通过 `RDGPassContext` 将 `RHICommandList` 暴露给 Pass 执行回调
- 用户只需在 `Execute()` 回调中使用 `ctx.command` 调用RHI命令
- RDG自动完成资源分配、屏障插入、描述符绑定

**设计模式**：
- 声明式API：Fluent Interface 定义资源和Pass
- 延迟求值：实际RHI资源在执行时才分配
- 对象池化：减少运行时资源创建开销
- 依赖图：自动推导资源依赖和状态转换
