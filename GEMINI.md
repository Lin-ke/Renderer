# Renderer Project - Gemini Context

This project is a high-performance, real-time 3D rendering engine built with C++20 and DirectX 11. It follows a modular architecture with a focus on ECS (Entity Component System), RDG (Render Dependency Graph), and a robust Asset/Reflection system.

## Project Overview

- **Core Technology**: C++20, DirectX 11, HLSL.
- **Architecture**:
    - **ECS**: Managed via `World`, `Scene`, `Entity`, and `Component`.
    - **Rendering**: Three-layer stack: **RDG (Render Dependency Graph)** -> **RHI (Render Hardware Interface)** -> **Platform (DX11)**.
    - **Resource Management**: UID-based `AssetManager` with support for dependency traversal and shared references.
    - **Reflection/Serialization**: Powered by `cereal` for serialization and a custom `Registry` for runtime type information.
- **Concurrency**: `ThreadPool` for worker tasks and a dedicated render thread (configurable).

## Directory Structure

- `engine/`: Core engine source code.
    - `core/`: Fundamental utilities (logging, math, threading, windowing).
    - `function/`: Higher-level systems (asset, framework/ECS, render).
    - `platform/`: Platform-specific implementations (e.g., `dx11`).
- `game/`: Example game/application code and entry point.
- `assets/`: Shaders, models, textures, and JSON-based asset metadata.
- `test/`: Unit tests using `Catch2`.

## Building and Running

The project uses **xmake** as its build system.

### Prerequisites
- Windows OS (DX11 requirement).
- Visual Studio 2022 (recommended).
- xmake.

### Common Commands

```powershell
# Configure the project (Debug mode, VS2022)
xmake f -m debug -c --vs=2022

# Build all targets (engine, game, utest)
xmake build

# Run the main game/demo
xmake run game

# Run unit tests
xmake run utest "[tag]"  # e.g., [render_resource], [scene], [pbr]

# Generate compilation database for LSP (clangd)
xmake project -k compile_commands
```

## Development Conventions

### Coding Style
- **Classes**: `PascalCase` (e.g., `RenderSystem`).
- **Functions & Variables**: `snake_case` (e.g., `init_engine()`, `delta_time`).
- **Class Members**: `snake_case_` (trailing underscore, e.g., `active_scene_`).
- **Macros**: `SCREAMING_SNAKE_CASE` (e.g., `DECLARE_LOG_TAG`).
- **Files**: C++ source files use `.cpp` and headers use `.h`.

### Best Practices
- **No Exceptions**: Prefer `std::optional` or return codes for error handling.
- **TODOs**: Use `//####TODO####` to mark tasks for visibility.
- **Resource Management**: Always use `std::shared_ptr` or `std::unique_ptr` for lifetime management.
- **Logging**: Use the internal `INFO`, `WARN`, `ERROR` macros with appropriate tags (e.g., `LogEngine`, `LogGame`).
- **Reflection**: Register new components in the `Registry` to enable serialization and editor support.

## Core Systems Usage

### Asset System

#### Virtual Paths
Assets are referenced via **Virtual Paths** to abstract the physical file system:
- **/Game/**: Mapped to `{path_in_initial}/assets/`.
- **/Engine/**: Mapped to `{project_root}/assets/`.

Example: `Model::Load("/Game/models/hero.obj")` loads from `{path_in_initial}/assets/models/hero.obj`.

#### Asset Dependencies (AssetDeps)
To support automatic dependency loading, custom Asset types must override two methods:

1.  **`save_asset_deps()`**: Report dependencies to the AssetManager.
    ```cpp
    void MyAsset::save_asset_deps() override {
        // Report all AssetRef members
        traverse_deps([this](AssetRef& ref) {
            // Check your internal AssetRef variables
            if (my_dependency_) ref = my_dependency_;
        });
    }
    ```
    *Note: The `traverse_deps` mechanism collects UIDs for the asset header.*

2.  **`load_asset_deps()`**: Resolve UIDs to AssetRefs.
    ```cpp
    void MyAsset::load_asset_deps() override {
        // Dependencies are pre-loaded by AssetManager before this call
        // Use get_asset_immediate to resolve them without blocking
        if (!dep_uid_.is_empty()) {
            my_dependency_ = AssetManager::get().get_asset_immediate(dep_uid_);
        }
    }
    ```

### Render System

#### Render Passes
- **Location**: `engine/function/render/render_pass/`
- **Structure**: Inherit from `RenderPass` base class.

```cpp
class MyPass : public RenderPass {
public:
    void init() override {
        // Create shaders, pipelines, root signatures
    }
    
    void build(RDGBuilder& builder) override {
        // Record render commands to the graph
        // builder.add_pass(...);
    }
    
    PassType get_type() const override { return PassType::Custom; }
};
```

#### Integration
Currently, passes are manually integrated into `RenderSystem`:
1.  Add `std::shared_ptr<MyPass>` to `RenderSystem`.
2.  Initialize in `RenderSystem::init()`.
3.  Execute in `RenderSystem::tick()` (either via `build()` for RDG or manual `draw()` calls).

### Engine Initialization
```cpp
std::bitset<8> mode;
mode.set(EngineContext::StartMode::Asset);
mode.set(EngineContext::StartMode::Window);
mode.set(EngineContext::StartMode::Render);
EngineContext::init(mode);

// Access systems
auto* world = EngineContext::world();
auto* asset_mgr = EngineContext::asset();
```

### ECS Workflow
```cpp
auto scene = std::make_shared<Scene>();
auto* entity = scene->create_entity();
entity->add_component<TransformComponent>();
entity->add_component<MeshRendererComponent>();
```

### Asset Loading
```cpp
// Load by path (relative to assets directory)
auto model = Model::Load("/Engine/models/bunny.obj"); 
```

## Testing Strategy
New features must include unit tests in the `test/` directory. Use descriptive tags to allow targeted test runs. Verification is done via `xmake run utest`.
