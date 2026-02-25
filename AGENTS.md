# Renderer Project - Agent Guide

## 项目概述

基于C++20的实时渲染引擎，DirectX 11后端，ECS架构，支持反射和资源管理。

---

## 项目结构

```text
engine/
├── core/            # 基础模块（log、math、reflect、window）
├── function/        # 功能模块
│   ├── asset/       # 资源管理（UID-based）
│   ├── framework/   # ECS框架
│   └── render/      # 渲染系统（RDG->RHI->DX11）
└── platform/dx11/   # DX11 RHI实现
```

---

## 构建命令

```bash
xmake f -c --vs=2022 --mode=debug    # 配置
xmake build engine game utest        # 构建
xmake run utest "[tag]"              # 测试
```

---

## 代码风格

| 类型 | 规范 | 示例 |
|------|------|------|
| 类名 | 大驼峰 | `class RenderSystem` |
| 函数/变量 | 下划线命名 | `void init_window()` / `int frame_count` |
| 类成员 | 下划线后缀 | `int frame_count_` |
| 宏 | 全大写下划线 | `DEFINE_LOG_TAG` |

**关键约定**：禁止占位符、TODO用`//####TODO####`、不用异常、用`std::optional`

---

## Core Systems

### Engine Context (引擎全局上下文)

`EngineContext` 提供了访问各个核心子系统的静态API：

```cpp
// 初始化与退出
EngineContext::init(mode);              
EngineContext::exit();                  

// 常用子系统 API 获取：
auto* world         = EngineContext::world();           // 实体与场景管理
auto* asset_manager = EngineContext::asset();           // 资源管理器 (AssetManager)
auto  rhi_backend   = EngineContext::rhi();             // 渲染后端接口 (RHIBackendRef)
auto* render_system = EngineContext::render_system();   // 高层渲染系统
auto* render_res    = EngineContext::render_resource(); // 渲染资源管理
```

### Tick System

分层传播：EngineContext -> World -> Scene -> Entity -> Component，使用 `delta_time` 保证帧率无关。

### Asset System

**UID-based资源引用**：使用128位UID唯一标识资源，必须用asset/binasset保存，加载（如`/Engine/models/bunny.asset`）。Model::load()可以使用fbx，obj（再保存就能得到asset）。

**资源加载 API 提示**：目前统一通过 `EngineContext::asset()` 进行资源的加载与获取，不要再使用独立的 `Asset::Load` 静态方法。

```cpp
// 1. 通过虚拟路径加载资源 (返回 std::shared_ptr<T>)
auto model = EngineContext::asset()->load_asset<Model>("/Engine/models/bunny.asset");

// 2. 通过 UID 加载资源
auto texture = EngineContext::asset()->load_asset<Texture>(uid);

// 3. 异步加载任务 (返回 std::vector<std::shared_future<AssetRef>>)
auto load_tasks = EngineContext::asset()->enqueue_load_task(uid);
```

**资源层级关系**：
```text
Model (场景对象容器)
├── Mesh[] (几何数据)
│   ├── VertexBuffer (顶点数据：位置/法线/切线/UV)
│   ├── IndexBuffer (索引数据)
│   └── BoundingBox (包围盒)
├── Material[] (渲染材质)
│   ├── Shader (VS/GS/PS)
│   ├── Texture[] (纹理贴图)
│   │   ├── Diffuse (漫反射)
│   │   ├── Normal (法线)
│   │   └── ARM (AO/Roughness/Metallic)
│   ├── MaterialParams (参数：roughness/metallic/color)
└── MaterialSlot[] (Mesh-Material绑定关系)
```

**渲染流程中的资源使用**：
1. **Culling阶段**：使用`Mesh.bounding_box`进行视锥剔除
2. **RenderBatch构建**：遍历`Model.material_slots_`，每个Slot生成一个DrawCall
3. **渲染阶段**：
   - `Mesh.vertex_buffer` / `index_buffer` -> 绑定到GPU输入
   - `Material.shader` -> 绑定Pipeline
   - `Material.texture_*` -> 绑定到Shader Resource
   - `Material.params` -> 更新Constant Buffer

### Reflection System

**实现**：基于cereal的序列化 + 自定义Registry运行时反射。

```cpp
// 1. 宏定义类元信息
class MyComponent : public Component {
    CLASS_DEF(MyComponent, Component)  // 生成serialize()和GetTypeName()
public:
    // 2. 注册成员到Registry（用于编辑器属性面板）
    static void register_class() {
        Registry::add<MyComponent>("MyComponent")
            .member("value", &MyComponent::value_)
            .member("name", &MyComponent::name_);
    }
private:
    int value_ = 0;
    std::string name_;
};

// 3. 注册到cereal多态体系（支持基类指针序列化）
CEREAL_REGISTER_TYPE(MyComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, MyComponent)
```

**序列化**：支持JSON（编辑用）和二进制（运行时）格式，通过`cereal::JSONOutputArchive` / `cereal::BinaryOutputArchive`切换。

### Render Architecture

三层架构：**RDG -> RHI -> DX11**

- **RDG**: Render Dependency Graph，管理Pass依赖和资源生命周期
- **RHI**: Render Hardware Interface，抽象渲染API（DX11/Vulkan）
- **DX11**: 平台具体实现

```cpp
// RDG层
RDGBuilder builder;
RDGTextureHandle color = builder.create_texture("Color").extent({w,h}).finish();       
builder.create_render_pass("Forward")
    .color(0, color, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE)
    .execute([](RDGPassContext ctx) { /* 渲染命令 */ })
    .finish();

// RHI层
RHITextureRef texture = backend->create_texture(info);
RHIBufferRef buffer = backend->create_buffer(info);
RHIGraphicsPipelineRef pipeline = backend->create_graphics_pipeline(info);
```

### Math System (DXMath)

基于DirectXMath的数学库，自定义封装提供Eigen兼容的API。

#### 坐标系定义

**右手坐标系**：
- **X轴**: 前向 (front) - 相机/物体的前向方向
- **Y轴**: 上 (up) - 世界空间的上方向
- **Z轴**: 右 (right) - 水平右方向

#### 旋转定义

欧拉角存储顺序为 `(pitch, yaw, roll)`，但旋转轴与传统定义不同：    

| 分量 | 含义 | 旋转轴 | 鼠标映射 |
|------|------|--------|----------|
| `pitch` (x) | 垂直俯仰 | **Z轴** (right) | 上下滑动 |
| `yaw` (y) | 水平偏航 | **Y轴** (up) | 左右滑动 |
| `roll` (z) | 侧倾 | **X轴** (front) | 通常不用 |

#### 矩阵存储格式

**C++端**: `float m[4][4]` 使用**列优先**存储（为了HLSL兼容）

**HLSL端**: `float4x4` 默认列优先，与C++端格式一致


---

## Agent工作流

0. 按需读取./docs
1. **编码**：遵循代码风格，禁止占位符
2. **测试**：在 `test/` 编写测试用例
3. **验证**：`xmake run utest` 直到通过
4. **总结**：
   - "I have": 做了什么
   - "I omitted": 省略了什么及理由
   - 更新AGENTS.md（如必要）

## Docs
Test格式（写Test前必看）
Asset 系统（写Asset前必看）
