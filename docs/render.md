# 渲染系统架构 (Render System Architecture)

本文档概述了引擎的核心渲染架构，包含了 Render Dependency Graph (RDG) 的语法说明、RHI Context 与 Immediate 的语义区分、DX11 后端实现细节以及 Shader 的自动编译机制。

---

## 1. RDG 语法 (Render Dependency Graph Syntax)

RDG 是建立在 RHI 之上的声明式渲染拓扑结构，利用 Builder 模式提供了一种优雅且极其安全的资源绑定和屏障插入方式。

### 1.1 资源声明与导入
在构建 Pass 之前，开发者需要声明用到的虚拟资源。RDG 采用延迟分配策略，这些资源只有在实际需要的时候才会从对象池 (`RDGPool`) 里借出。

```cpp
RDGBuilder builder(command_list);

// 创建瞬态纹理（延迟分配），声明其用途
auto color_tex = builder.create_texture("MainColor")
    .extent({width, height, 1})
    .format(FORMAT_R8G8B8A8_UNORM)
    .allow_render_target()
    .finish();

// 导入外部物理资源（如 Swapchain 提供的 BackBuffer）
auto back_buffer = builder.import_texture("BackBuffer", swapchain_texture);
```

### 1.2 Pass 声明与执行回调
声明一个渲染阶段时，需要通过链式调用指定 Shader 对这批资源读写的具体语义，并在 `execute` 闭包里编写底层的 Draw Call。

```cpp
builder.create_render_pass("DeferredLighting")
    .root_signature(lighting_root_sig)
    
    // 将 color_tex 绑定为 RenderTarget
    .color(0, color_tex, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0,0,0,1})
    
    // 声明为 Shader SRV，绑定到 set 0, binding 1
    .read(0, 1, 0, gbuffer_albedo)
    
    // 用户自定义的底层 RHI 绘制闭包
    .execute([](RDGPassContext ctx) {
        ctx.command->set_graphics_pipeline(lighting_pipeline);
        ctx.command->bind_descriptor_set(ctx.descriptors[0], 0); // 描述符由 RDG 自动完成配置
        ctx.command->draw(3, 1, 0, 0); // 全屏三角形
    })
    .finish();
```

通过这些声明，RDG 可以自动计算并隐式插入跨 Pass 的 `texture_barrier` 和 `buffer_barrier`。

---

## 2. RHI 语义：Immediate vs Deferred

渲染硬件接口 (RHI) 为命令上下文 (`CommandContext`) 提供两种设计模式：延迟记录和即时执行。

### 2.1 延迟执行语义 (`RHICommandContext` & `RHICommandList`)
- **生命周期**：每帧动态分配。
- **工作机制**：在执行 `begin_command()` 到 `end_command()` 之间，所有的图形管线绑定（`set_graphics_pipeline`）、绘制 (`draw`) 以及计算调度都会被放入 CPU 队列，并不会立刻对 GPU 产生影响。
- **提交方式**：最终由上游的 `execute` 统一打包投递到 Graphics Queue 中（多用于 RDG 框架内部）。
- **职责**：维护复杂的图形状态，负责主渲染循环内高并发帧渲染任务。

### 2.2 即时执行语义 (`RHICommandContextImmediate`)
- **生命周期**：与后端设备绑定，生命周期通常长于单帧。
- **工作机制**：当调用内存搬运和状态转移接口（如 `copy_buffer_to_texture`、`generate_mips` 或强制 `texture_barrier`）时，命令被理解为“脱离当前主渲染帧图”的独立操作。
- **提交方式**：通过调用 `.flush()` 可以直接将积累的即时命令同步派发到 GPU 并阻塞等待执行完毕。
- **职责**：主要用于**资源加载线程**中纹理和模型的异步/同步上传阶段。这就保证了被送入 RDG 时，底层 VRAM 数据和状态已经是就绪（Ready）的。

---

## 3. DX11 Backend 抽象实现

本项目提供了现代图形 API 统一抽象的实现，其中就包括基于 DirectX 11 的平台封装（`platform/dx11/platform_rhi`）。

### 3.1 `ComPtr` 资源管理
DX11 后端的实现高度利用了 `Microsoft::WRL::ComPtr` 来管理 COM 接口。
- `RHITexture` -> `ComPtr<ID3D11Texture2D>` 
- `RHITextureView` -> `ComPtr<ID3D11ShaderResourceView>` 等视图
- `RHIBuffer` -> `ComPtr<ID3D11Buffer>` 
- `RHICommandContext` -> `ComPtr<ID3D11DeviceContext>`

这些类型通过 C++ CRTP 及模板元特性 (`DX11ResourceTraits`) 提供向基类的隐式抽象，以及向下转型的能力。

### 3.2 资源池化与别名
DX11 的实现内部对于 RHI 抽象进行了精准映射：
- DX11 没有原生 Command Buffer 池和记录的概念，因此 `DX11CommandContext` 是通过代理/封装 `ID3D11DeviceContext` 并自己维持内部 `std::vector` 的虚拟指令流，最终在 CPU `execute` 时将指令映射回 DX11 设备的立即执行。
- `DX11CommandContextImmediate` 会直接借用 `ID3D11DeviceContext` 即刻执行并使用 CPU-GPU Query 等待。

---

## 4. 自动 Shader 编译 (Automatic Shader Compilation)

为了最大化开发效率和保证跨平台能力，RHI 支持了在运行时从字符串代码即时生成 Shader Bytecode 的能力。

### 4.1 接口定义
```cpp
// RHIBackend 虚函数：
virtual std::vector<uint8_t> compile_shader(const char* source, const char* entry, const char* profile) = 0;
```

### 4.2 DX11 的底层实现
在 DX11 中，通过调用 `D3DCompile` 将 HLSL 文本转换为 `ID3DBlob`，并提取出 Bytecode 数组供后续调用 `CreateVertexShader` 等接口使用。

```cpp
std::vector<uint8_t> DX11Backend::compile_shader(const char* source, const char* entry, const char* profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;

    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                            &blob, &error_blob);
                            
    if (FAILED(hr)) {
        if (error_blob) {
            ERR(LogRHI, "Shader compilation failed: {}", (char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return {};
    }
    // 返回 bytecode 供上游构造 DX11Shader...
}
```

### 4.3 管线设计哲学
- **离线与在线两用**：既可以方便地编写即时修改即时生效的引擎内部测试材质，也可以配合脱机编译器在 Release 版本中直接加载 `.cso`。
- **跨后端平滑支持**：当接入 Vulkan 后端时，`compile_shader` 会调用 `shaderc` 或是 `glslang` 最终产出并返回 SPIR-V Bytecode，而上游逻辑对此完全无感。