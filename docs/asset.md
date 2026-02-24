# Asset System

## Core Mechanism

Assets use **UID-based references** with a **two-phase loading** process, designed for efficient caching, persistent identity, and dependency resolution.

```
Phase 1: Deserialize          Phase 2: Resolve
┌─────────────────┐           ┌──────────────────┐
│ JSON (Disk):    │           │ AssetManager     │
│   mesh_deps_    │──────────▶│   UID→Ptr lookup │
│   = null        │  cereal   │   Load if needed │
│   mesh_deps_uid │           │                  │
│   = ["f150..."] │           │ Result:          │
└─────────────────┘           │   mesh_deps_     │
                              │   = [Ptr1,Ptr2]  │
                              └──────────────────┘
```

**Key insight:** Serialization stores UIDs (`"91727551-..."`), not pointers. Pointers are resolved at load time via `load_asset_deps()`.

## Asset Lifecycle & State

Assets inherit from `Asset` which manages basic states:
- `initialized_`: Indicates if the asset is fully loaded and ready.
- `dirty_`: Indicates if the asset has been modified and needs saving. Default is `true` for newly created assets.

When an asset is loaded via `AssetManager`, its `on_load_asset()` method is called:
```cpp
void on_load_asset() {
    load_asset_deps(); // 1. Resolves UIDs to actual Asset pointers
    on_load();         // 2. Custom initialization logic (override this!)
    clear_dirty();     // 3. Mark as clean since it matches disk state
}
```

When saved:
```cpp
void on_save_asset() {
    save_asset_deps(); // 1. Collects UIDs from Asset pointers
    on_save();         // 2. Custom save logic
}
```

## ASSET_DEPS Macro

Use `ASSET_DEPS` to automatically handle dependency properties in your `Asset` subclasses:

```cpp
class Material : public Asset {
    ASSET_DEPS(
        (TextureRef, diffuse_tex),            // Single reference
        (std::vector<TextureRef>, tex_array)  // Array of references
    )
};
```

**Auto-generates:**
- Properties (e.g., `TextureRef diffuse_tex` and `UID diffuse_tex_uid`)
- `load_asset_deps()` - Converts loaded UIDs to strong pointers
- `save_asset_deps()` - Converts strong pointers back to UIDs for saving
- `serialize_deps(ar)` - JSON serialization logic for cereal
- `traverse_deps(cb)` - Dependency walking

## Critical: Override `on_load()` instead of `on_load_asset()`

To avoid breaking the dependency resolution chain, you should generally override `on_load()` rather than `on_load_asset()`. 

**❌ WRONG - Overriding `on_load_asset()` and missing `load_asset_deps()`:**
```cpp
void Model::on_load_asset() override {
    // mesh_deps_ is EMPTY here!
    sync_deps_to_slots();  // Fails: Rebuilds from empty
    on_load();
}
```

**✅ CORRECT - Overriding `on_load()`:**
```cpp
void Model::on_load() override {
    // Dependencies are already loaded by the base on_load_asset()!
    sync_deps_to_slots();
}
```

## Virtual Paths & Path Resolution

Virtual paths (`/Game/...`, `/Engine/...`) abstract physical locations. The mapping is built at runtime by `AssetManager::init(game_path)` which scans asset directories.

```
/Game/models/hero.asset ─────┐
                              ├──► AssetManager scans ──► UID ↔ Path map
/Engine/shaders/pbr.hlsl ────┘
```

### Loading Assets

```cpp
// Option 1: Load by virtual path (Preferred for user-facing code)
auto scene = AssetManager::load_asset<Scene>("/Game/scene.asset");

// Option 2: Look up UID, then load
UID uid = AssetManager::get_uid_by_path("/Game/scene.asset");
auto scene = AssetManager::load_asset<Scene>(uid);
```
*Note: `load_asset` currently performs a synchronous blocking load. `enqueue_load_task` is available for async loading.*

### Generating UIDs for Source Assets

When importing a *new* source file (FBX, PNG) rather than loading an existing `.asset` file, use `UID::from_hash()` to ensure deterministic UUIDs:

```cpp
std::string source_path = "/Engine/models/hero.fbx";
UID model_uid = UID::from_hash(source_path); // Deterministic!

auto model = std::make_shared<Model>();
model->set_uid(model_uid);
AssetManager::save_asset(model, source_path + ".asset");
```

## Non-Asset Components

Entities and Components hold asset references but aren't `Asset` subclasses themselves.

```cpp
class MeshRendererComponent : public Component {
    ASSET_DEPS((ModelRef, model_))
    
    void on_init() {
        // Safe to use model_ here, because entity ensures load_asset_deps() is called first
        model_->get_submesh_count();
    }
};
```

**Critical Ordering in Scene:**
```cpp
scene->load_asset_deps();     // 1. Resolve all UIDs to pointers
init_scene_components(scene); // 2. Call on_init() across entities
```

## API Summary

```cpp
// AssetManager
asset_mgr->init("/path/to/project");
asset_mgr->load_asset<T>(uid);
asset_mgr->load_asset<T>("/Game/path.asset");
asset_mgr->save_asset(asset, "/Game/path.asset");
asset_mgr->get_physical_path("/Game/...");

// Asset state
asset->is_dirty();
asset->mark_dirty();
asset->is_initialized();
```