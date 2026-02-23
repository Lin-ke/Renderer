# Renderer Project - Agent Guide

## 项目概述

基于C++20的实时渲染引擎，DirectX 11后端，ECS架构，支持反射和资源管理。

---

## 项目结构

```
engine/
├── core/            # 基础模块（log、math、reflect、window）
├── function/        # 功能模块
│   ├── asset/       # 资源管理（UID-based）
│   ├── framework/   # ECS框架
│   └── render/      # 渲染系统（RDG→RHI→DX11）
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

## 测试标签

| 标签 | 说明 |
|------|------|
| `[render_resource]` | Texture/Material/Model/Shader |
| `[scene]` | Scene管理 |
| `[reflection]` | 反射系统 |
| `[draw]` | 绘制测试 |
| `[pbr]` | PBR渲染 |
| `[light]` | 灯光系统 |

---

## Core Systems

### Engine Context

```cpp
EngineContext::init(mode);              // 初始化
auto* world = EngineContext::world();   // 获取子系统
EngineContext::exit();                  // 退出
```

### Tick System

分层传播：EngineContext → World → Scene → Entity → Component，使用 `delta_time` 保证帧率无关。

### Asset System

**UID-based资源引用**：使用128位UUID唯一标识资源，支持路径别名（如`/Engine/models/bunny.obj`）。

```cpp
auto model = Model::Load("/Engine/models/bunny.obj");  // 自动缓存，返回共享指针
// 相同路径多次Load返回同一实例，引用计数管理生命周期
```

**依赖处理（Deps）**：Asset通过`traverse_deps()`声明依赖，Scene/Prefab序列化时自动处理嵌套资源。

**ASSET_DEPS宏**：简化依赖管理，自动生成`traverse_deps`、`load_asset_deps`、`save_asset_deps`和序列化代码。

```cpp
// 在Asset子类中使用ASSET_DEPS宏声明依赖
class Material : public Asset {
    ASSET_DEPS(
        (TextureRef, texture_diffuse),              // 单一依赖
        (TextureRef, texture_normal),
        (std::vector<TextureRef>, texture_2d)       // 数组依赖
    )
    
    // 宏自动生成：
    // - traverse_deps()：遍历所有依赖
    // - load_asset_deps()：从UID加载指针
    // - save_asset_deps()：同步指针到UID
    // - serialize_deps()：序列化UID列表
};
```

**宏原理**：
- `DECL_ENTRY`：声明`AssetRef ptr`和`UID ptr_uid`成员
- `LOAD_ENTRY`：`ptr = AssetManager::load_asset<PtrType>(ptr_uid)`
- `SYNC_ENTRY`：`ptr_uid = ptr ? ptr->get_uid() : UID::empty()`
- `VISIT_ENTRY`：`if (ptr) callback(ptr)`

**手动实现（旧方式）**：
```cpp
class Scene : public Asset {
    void traverse_deps(std::function<void(std::shared_ptr<Asset>)> cb) const override {
        for (auto& entity : entities_) {
            entity->traverse_deps(cb);  // 遍历所有Entity的依赖
        }
    }
    void load_asset_deps() override;   // 反序列化后加载依赖
    void save_asset_deps() override;   // 保存时收集依赖
};
```

**Model序列化架构**：Model是资源组合容器，通过`ASSET_DEPS`宏管理Mesh和Material依赖。

**资源层级关系**：
```
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
│   └── MaterialParams (参数：roughness/metallic/color)
└── MaterialSlot[] (Mesh-Material绑定关系)
```

**代码结构**：
```cpp
class Model : public Asset {
    ASSET_DEPS(
        (std::vector<MeshRef>, mesh_deps_),       // 几何数据依赖
        (std::vector<MaterialRef>, material_deps_) // 材质依赖
    )
    
    // 运行时结构 - Slot定义渲染批次
    struct MaterialSlot {
        MeshRef mesh;           // 指向mesh_deps_[mesh_index]
        MaterialRef material;   // 指向material_deps_[material_index]
        uint32_t mesh_index;
        uint32_t material_index;
    };
    std::vector<MaterialSlot> material_slots_;
};

class Material : public Asset {
    ASSET_DEPS(
        (TextureRef, texture_diffuse),
        (TextureRef, texture_normal),
        (TextureRef, texture_arm),
        (ShaderRef, vertex_shader),
        (ShaderRef, fragment_shader)
    )
    
    MaterialType type_;  // PBR / NPR / Base
    Vec4 diffuse_;
    float roughness_, metallic_;
};
```

**渲染流程中的资源使用**：
1. **Culling阶段**：使用`Mesh.bounding_box`进行视锥剔除
2. **RenderBatch构建**：遍历`Model.material_slots_`，每个Slot生成一个DrawCall
3. **渲染阶段**：
   - `Mesh.vertex_buffer` / `index_buffer` → 绑定到GPU输入
   - `Material.shader` → 绑定Pipeline
   - `Material.texture_*` → 绑定到Shader Resource
   - `Material.params` → 更新Constant Buffer

**依赖同步流程**：
- `sync_slots_to_deps()`：保存前收集`material_slots_`中的指针到`mesh_deps_`/`material_deps_`
- `sync_deps_to_slots()`：加载后将依赖指针分配到`material_slots_`
- 保存Model时，ASSET_DEPS自动级联保存所有Mesh和Material

**性能考虑**：
- Mesh和Material可被多个Model共享（如实例化渲染）
- 实际顶点数据存储在GPU Buffer，Asset只存CPU端引用
- 材质参数变化不影响其他使用相同Material的Model

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

三层架构：**RDG → RHI → DX11**

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

---

## Agent工作流

1. **编码**：遵循代码风格，禁止占位符
2. **测试**：在 `test/` 编写测试用例
3. **验证**：`xmake run utest` 直到通过
4. **总结**：
   - "I have": 做了什么
   - "I omitted": 省略了什么及理由
   - 更新AGENTS.md（如必要）
