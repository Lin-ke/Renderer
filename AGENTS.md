# Renderer Project - Agent Guide

## Project Overview

这是一个基于C++20开发的实时渲染引擎项目，采用DirectX 11作为图形后端，设计目标是构建一个具有现代化渲染管线、ECS架构、反射系统和资源管理功能的游戏引擎。在Windows系统（PowerShell）命令行下工作。

### Key Features
- **Rendering**: DirectX 11 RHI (Render Hardware Interface) 抽象层，支持多种渲染Pass（Forward、Deferred、Post-processing等）
- **ECS架构**: Entity-Component-System设计，Component支持反射和序列化
- **资源管理**: UID-based资源引用系统，支持Model、Texture、Shader、Material、Scene、Prefab等资产类型
- **反射系统**: 基于cereal的序列化，支持JSON和二进制格式，可用于编辑器属性编辑
- **多线程**: 线程池支持，分离的渲染线程

---

## 工作流指南

### 开发流程
1. **阅读本文档**: 首先阅读本指南了解项目结构和规范
2. **编码规范**: 遵循下方代码风格完成任务
   - **禁止占位符**: 不允许使用注释、dummy code、默认实现、`// ...` 来偷懒
   - **TODO标记**: 如果逻辑复杂必须跳过，使用 `//####TODO####` 注释并说明理由
3. **测试驱动**: 所有功能完成后需要在 `test/` 目录编写测试
4. **验证**: 调用 `xmake run utest ["tag"]` 循环测试直到通过
5. **总结**: 最后总结时按照以下结构：
   - "I have": 说明你做了什么
   - "I omitted": 说明你省略了什么（特别是TODO注释的部分）及理由
   - 修改AGENTS.md 修改Project Structure和 test标签说明，如必要

---

## Project Structure

```
renderer/
├── engine/              # 核心引擎代码（静态库）
│   ├── core/            # 基础核心模块
│   │   ├── log/         # 日志系统（基于glog）
│   │   ├── math/        # 数学库（基于Eigen）
│   │   ├── os/          # 操作系统抽象（线程池）
│   │   ├── reflect/     # 反射和序列化系统
│   │   ├── hash/        # 哈希算法（MurmurHash）
│   │   ├── utils/       # 工具函数（Timer等）
│   │   ├── window/      # 窗口管理（GLFW）
│   │   └── dependency_graph/  # 依赖图
│   ├── function/        # 功能模块
│   │   ├── asset/       # 资源管理系统
│   │   ├── framework/   # ECS框架（Entity、Component、Scene、World）
│   │   ├── input/       # 输入系统
│   │   └── render/      # 渲染系统
│   │       ├── rhi/             # 渲染硬件接口
│   │       ├── render_resource/ # 渲染资源（Buffer、Texture、Shader等）
│   │       ├── render_system/   # 渲染管理层
│   │       ├── render_pass/     # 渲染Pass定义
│   │       ├── graph/           # RDG（Render Dependency Graph）
│   │       └── data/            # 渲染数据结构
│   ├── main/            # 引擎上下文和主循环
│   └── platform/        # 平台相关实现
│       └── dx11/        # DirectX 11 RHI实现
├── game/                # 游戏可执行文件
│   ├── main.cpp         # 游戏入口
│   ├── asset/           # 游戏资源文件
│   ├── level/           # 关卡文件
│   └── logic/           # 游戏逻辑
├── test/                # 单元测试
│   ├── test_main.cpp    # Catch2主入口
│   ├── draw/            # 绘制测试
│   │   ├── test_triangle.cpp
│   │   └── test_bunny.cpp
│   └── render_resource/ # 渲染资源测试
│       ├── test_texture.cpp
│       ├── test_material.cpp
│       ├── test_model.cpp
│       ├── test_shader.cpp
│       └── test_mesh_renderer.cpp
├── RD/                  # 旧的渲染实现（已排除编译，仅作参考）
├── assets/              # 资产文件
└── logs/                # 日志输出目录
```

---

## Technology Stack

### Build System
- **Xmake**: 主要构建工具 (xmake.lua)
- **C++20**: 语言标准
- **MSVC**: Visual Studio 2022 编译器

### Dependencies
| Package | Purpose |
|---------|---------|
| glfw | 窗口管理 |
| imgui | 即时GUI |
| glog | 日志记录 |
| stb | 图像加载 |
| assimp | 模型导入 |
| cereal | 序列化（JSON/Binary）|
| eventpp | 事件系统 |
| eigen | 数学库 |
| stduuid | UUID生成 |
| catch2 | 单元测试框架 |

### System Libraries
- DirectX 11 (d3d11, dxgi, dxguid, D3DCompiler)
- Direct2D/DWrite (d2d1, dwrite)
- Windows API (winmm, user32, gdi32, ole32)

---

## Build Instructions

### Prerequisites
- Windows系统
- Visual Studio 2022
- Xmake 构建工具 (https://xmake.io)

### Build Commands

```bash
# 配置项目（Debug模式，VS2022）
xmake f -c --vs=2022 --mode=debug

# 配置项目（Release模式）
xmake f --mode=release

# 构建引擎（静态库）
xmake build engine

# 构建游戏
xmake build game

# 运行游戏
xmake run game

# 运行单元测试
xmake run utest

# 运行特定标签的测试
xmake run utest "[render_resource]"
xmake run utest "[scene]"
xmake run utest "[draw]"
```

### Build Targets
| Target | Type | Description |
|--------|------|-------------|
| engine | static | 核心引擎库 |
| game | binary | 游戏可执行文件 |
| utest | binary | 单元测试可执行文件 |

---

## Code Style Guidelines

### 代码格式化
- 使用 **.clang-format** 配置
- **4个空格缩进**（Tab宽度为4）
- 左大括号在同一行末尾，右大括号单独一行
- 使用C++20标准
- 行宽限制：120字符

### 命名规范
| 类型 | 规范 | 示例 |
|------|------|------|
| 类名 | 大驼峰 | `class RenderSystem` |
| 函数 | 下划线命名 | `void init_window()` |
| 变量 | 下划线命名 | `int frame_count` |
| 类成员 | 下划线后缀 | `int frame_count_` |
| 宏定义 | 全大写下划线 | `DEFINE_LOG_TAG` |
| 常量 | 全大写下划线 | `FRAMES_IN_FLIGHT` |

### 关键约定
1. **不使用异常**：错误处理使用 `std::optional<T>`，失败时返回 `std::nullopt`
2. **LOG宏**：使用 `INFO(Tag, ...)`、`WARN(Tag, ...)`、`ERR(Tag, ...)`、`FATAL(Tag, ...)`
3. **日志Tag定义**：`DEFINE_LOG_TAG(LogTagName, "DisplayName")`
4. **API注释**：使用Doxygen风格注释

### 示例代码
```cpp
// 类定义
class MyComponent : public Component {
    CLASS_DEF(MyComponent, Component)
public:
    void update(float delta_time);
private:
    int value_ = 0;
};

// 函数实现
void MyComponent::update(float delta_time) {
    if (delta_time <= 0) {
        ERR(LogComponent, "Invalid delta time: {}", delta_time);
        return;
    }
    value_ += static_cast<int>(delta_time * 100);
}
```

---

## Testing Instructions

### 测试框架
- **Catch2 v3**: 单元测试框架
- 测试文件命名：`test_<tag>.cpp`
- 不使用Section，直接写TEST_CASE
- 测试资源放在 `test/test_internal/` 目录
- 代码中使用 `ENGINE_PATH` 宏获取项目根目录路径

### 测试标签说明

| 标签 | 说明 | 命令 |
|------|------|------|
| **render_resource** | Render Resource 相关测试 (Texture, Material, Model, Shader, MeshRenderer) | `xmake run utest "[render_resource]"` |
| **scene** | Scene 管理测试 (序列化、依赖) | `xmake run utest "[scene]"` |
| **reflection** | 反射系统测试 | `xmake run utest "[reflection]"` |
| **prefab** | Prefab 系统测试 | `xmake run utest "[prefab]"` |
| **rdg** | 渲染依赖图测试 | `xmake run utest "[rdg]"` |
| **thread_pool** | 线程池测试 | `xmake run utest "[thread_pool]"` |
| **draw** | 所有绘制测试 | `xmake run utest "[draw]"` |
| **triangle** | Triangle 绘制测试 (基础DX11、Cube) | `xmake run utest "[triangle]"` |
| **bunny** | Bunny 模型渲染测试 | `xmake run utest "[bunny]"` |
| **pbr** | PBR 渲染测试 (PBR Forward Pass, FBX Model) | `xmake run utest "[pbr]"` |
| **rdg** | RDG 渲染测试 (RDG Forward Pass) | `xmake run utest "[rdg]"` |
| **light** | 灯光系统测试 (LightManager, DirectionalLight, PointLight) | `xmake run utest "[light]"` |
| **tick** | Tick 系统测试 (OrbitComponent, 飞船轨道运动) | `xmake run utest "[tick]"` |

### 测试目录结构
```
test/
├── test_main.cpp              # Catch2 主入口
├── test_rdg.cpp               # [rdg] 渲染依赖图
├── test_reflection.cpp        # [reflection] 反射 + [prefab] Prefab
├── test_scene.cpp             # [scene] Scene 管理
├── test_thread_pool.cpp       # [thread_pool] 线程池
├── test_light_manager.cpp     # [light] 灯光系统
├── test_tick_system.cpp       # [tick] Tick 系统（飞船轨道运动）
├── draw/                      # 绘制测试
│   ├── test_triangle.cpp      # [draw][triangle] DX11 Triangle + Cube
│   ├── test_bunny.cpp         # [draw][bunny] Bunny 渲染
│   ├── test_rdg_forward.cpp   # [draw][rdg] RDG Forward Pass 渲染
│   └── test_pbr_klee.cpp      # [draw][pbr] PBR 渲染测试
└── render_resource/           # 渲染资源测试
    ├── test_texture.cpp       # [render_resource] Texture
    ├── test_material.cpp      # [render_resource] Material
    ├── test_model.cpp         # [render_resource] Model + Mesh
    ├── test_shader.cpp        # [render_resource] Shader
    └── test_mesh_renderer.cpp # [render_resource] MeshRenderer
```

### 编写新测试
```cpp
#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"

TEST_CASE("Test Description", "[tag]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    EngineContext::init(mode);
    
    // 测试代码...
    REQUIRE(condition == true);
    
    EngineContext::exit();
}
```

### 测试资源清理规范

使用 `EngineContext` 的测试必须遵循以下清理约定：

| 测试类型 | 初始化 | 清理 | 说明 |
|----------|--------|------|------|
| 标准测试 | `EngineContext::init(mode)` | `EngineContext::exit()` | 所有资源自动清理 |
| 底层 RHI 测试 | `glfwInit()` + `RHIBackend::init()` | `backend->destroy()` + `RHIBackback::reset_backend()` + `glfwDestroyWindow()` | 不调用 `glfwTerminate()`，保持与 EngineContext 一致 |

**注意**：GLFW 的 `glfwTerminate()` 由进程退出时自动处理，测试代码中不单独调用，以支持多个测试连续运行。

---

## Core Systems

### Engine Context
`EngineContext` 是引擎的核心管理器，采用单例模式：

```cpp
// 初始化引擎（使用StartMode位掩码）
EngineContext::init(
    (1 << EngineContext::Asset) |
    (1 << EngineContext::Render) |
    (1 << EngineContext::Window)
);

// 主循环
EngineContext::main_loop();

// 退出
EngineContext::exit();

// 获取子系统
EngineContext::rhi();              // RHI后端
EngineContext::asset();            // 资产管理器
EngineContext::render_system();    // 渲染系统
EngineContext::world();            // 游戏世界
EngineContext::window();           // 窗口
EngineContext::thread_pool();      // 线程池

// 时间和Tick信息
EngineContext::get_delta_time();    // 获取上一帧耗时（秒）
EngineContext::get_current_tick();  // 获取当前总帧数
```

### Tick System
引擎采用分层Tick系统，每帧按顺序更新：

```cpp
// 1. EngineContext 计算 delta_time 并更新系统
void EngineContext::main_loop_internal() {
    delta_time_ = timer_.get_elapsed_sec();  // 计算帧间隔
    
    Input::get_instance().tick();            // 输入系统tick
    world_->tick(delta_time_);               // 世界/场景/组件tick
    asset_manager_->tick();                  // 资源管理器tick
    // ... 渲染系统tick
}

// 2. World 转发 tick 到 Scene
void World::tick(float delta_time) {
    if (active_scene_) active_scene_->tick(delta_time);
}

// 3. Scene 转发 tick 到所有 Entity
void Scene::tick(float delta_time) {
    for (auto& entity : entities_) {
        entity->tick(delta_time);
    }
}

// 4. Entity 转发 tick 到所有 Component
void Entity::tick(float delta_time) {
    for (auto& comp : components_) {
        comp->on_update(delta_time);
    }
}

// 5. Component 实现具体的更新逻辑
class MyComponent : public Component {
    void on_update(float delta_time) override {
        // 每帧执行的逻辑，使用 delta_time 保证帧率无关
        position += velocity * delta_time;
    }
};
```

**关键特性：**
- **分层传播**: EngineContext → World → Scene → Entity → Component
- **帧率无关**: 使用 `delta_time`（秒）确保动画/移动在不同帧率下表现一致
- **Timer工具**: `engine/core/utils/timer.h` 提供高精度计时
- **自动触发**: 无需手动调用，main_loop 自动处理所有 tick

### Asset System
- **UID**: 使用128位UUID标识资源
- **AssetManager**: 管理资源加载、保存和生命周期
- **AssetType**: Model、Texture、Shader、Material、Scene、Prefab等
- **序列化**: 支持JSON（编辑）和二进制（运行时）格式

### Component System
- 所有组件继承自 `Component` 基类
- 使用 `CLASS_DEF` 宏定义类型信息
- 支持反射序列化（通过cereal）
- Entity使用 `add_component<T>()` 和 `get_component<T>()` 管理组件

### Render System
- **RHI**: 抽象渲染硬件接口，当前仅实现DX11
- **RDG**: Render Dependency Graph，用于管理渲染Pass和资源依赖
  - `RDGBuilder`: 构建渲染图，声明资源和Pass依赖
  - `RDGTextureHandle/RDGBufferHandle`: 资源句柄
  - `RDGRenderPassBuilder`: 渲染Pass构建器
  - 自动资源状态管理和屏障生成
- **Render Resources**: Buffer、Texture、Shader、Sampler、Material、Model等
- **Render Passes**: ForwardPass (使用RDG)、DeferredPass、Post-processing等

---

## Configuration

### 编译配置 (engine/configs.h)
```cpp
#define FRAMES_IN_FLIGHT 3    // 帧缓冲数量
#define ENABLE_DEVICE_DEBUG 1 // 启用调试层
#define ENABLE_RT 1           // 启用光追
```

### 构建模式
- **Debug**: 启用 `_DEBUG_` 宏，关闭优化
- **Release**: 启用 `-O3` 优化

---

## Important Notes for Agents

1. **禁止占位符**: 不允许使用注释、dummy code、默认实现来偷懒
2. **TODO标记**: 如果必须跳过复杂逻辑，使用 `//####TODO####` 注释并说明理由
3. **资源路径**: 使用 `ENGINE_PATH` 宏拼接绝对路径
4. **错误处理**: 不使用异常，使用 `std::optional` 和错误日志
5. **测试要求**: 所有功能完成后需要在 `test/` 目录编写测试
6. **工作流**: 实现功能 → 编写测试 → `xmake run utest` → 直到通过

---

## File Locations

| 内容 | 路径 |
|------|------|
| 引擎代码 | `engine/` |
| 游戏代码 | `game/` |
| 测试代码 | `test/` |
| 测试资源 | `test/test_internal/` |
| 游戏资源 | `game/asset/` |
| 构建配置 | `xmake.lua` |
| 代码风格 | `.clang-format` |
| 日志输出 | `logs/` |
