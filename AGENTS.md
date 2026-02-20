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
