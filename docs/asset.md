# Asset System

## Core Mechanism

Assets use **UID-based references** with a **two-phase loading** process:

```
Phase 1: Deserialize          Phase 2: Resolve
┌─────────────────┐           ┌──────────────────┐
│ JSON:           │           │ AssetManager     │
│   mesh_deps_    │──────────▶│   UID→Ptr lookup │
│   = null        │  cereal   │   Load if needed │
│   mesh_deps_uid │           │                  │
│   = ["f150..."] │           │ Result:          │
└─────────────────┘           │   mesh_deps_     │
                              │   = [Ptr1,Ptr2]  │
                              └──────────────────┘
```

**Key insight:** Serialization stores UIDs (`"91727551-..."`), not pointers. Pointers are resolved at load time via `load_asset_deps()`.

## ASSET_DEPS Macro

```cpp
class Material : public Asset {
    ASSET_DEPS(
        (TextureRef, diffuse_tex),   // Single
        (std::vector<TextureRef>, tex_array)  // Array
    )
};
```

**Auto-generates:**
- `TextureRef diffuse_tex` + `UID diffuse_tex_uid`
- `load_asset_deps()` - UIDs → pointers
- `save_asset_deps()` - pointers → UIDs
- `serialize_deps(ar)` - JSON UIDs
- `traverse_deps(cb)` - dependency walking

## Critical: Override on_load_asset()

**❌ WRONG - Missing load_asset_deps():**
```cpp
void Model::on_load_asset() override {
    // mesh_deps_ is EMPTY here!
    sync_deps_to_slots();  // Rebuilds from empty → 0 submeshes
    on_load();
}
```
**Why this fails:** Cereal deserializes UIDs, not pointers. `mesh_deps_` vector is null until `load_asset_deps()` resolves them via AssetManager.

**✅ CORRECT:**
```cpp
void Model::on_load_asset() {
    load_asset_deps();       // 1. UIDs → pointers
    sync_deps_to_slots();    // 2. Rebuild runtime structures
    on_load();
}
```

### Why Base Class Can't Help

Base `Asset::on_load_asset()` does call `load_asset_deps()`, but **subclass override replaces it entirely**. You must either:
1. Call `Asset::on_load_asset()` (base implementation), OR
2. Call `load_asset_deps()` directly before accessing dependencies

## Virtual Paths & Path Resolution

Virtual paths abstract physical locations. The mapping is built at runtime by scanning asset directories.

```
/Game/models/hero.asset ─────┐
                              ├──► AssetManager scans ──► UID ↔ Path map
/Engine/shaders/pbr.hlsl ────┘
```

### How Path Resolution Works

```cpp
// 1. Initialize - scans directories, builds UID↔Path mappings
AssetManager::init("/path/to/project");  // Scans /assets recursively

// 2. Look up UID by virtual or physical path
UID uid = AssetManager::get_uid_by_path("/Game/models/hero.asset");
// Or: get_uid_by_path("D:/Project/assets/models/hero.asset")

// 3. Load by UID (uses internal uid_to_path_ map)
auto model = AssetManager::load_asset<Model>(uid);
```

**Critical:** The UID-to-path mapping is **not derived from the path string**. It's read from the asset file header:

```cpp
// Inside asset file (hero.asset):
{
    "uid": "91727551-e94d-a2b1-f7d9-cf8f68717dd4",  // ← Stored in file
    "asset": { ... }
}

// AssetManager reads this during scan_directory()
register_path(UID("91727551..."), "D:/Project/assets/models/hero.asset");
```

### Common Path Bug

**❌ WRONG - Hashing path to get UID:**
```cpp
// User code trying to load a scene file
std::string path = "D:/Renderer/assets/scene.asset";
UID uid = UID::from_hash(path);  // WRONG! Hash of path ≠ UID stored in file
auto scene = AssetManager::load_asset<Scene>(uid);  // FAILS
```

**Why it fails:** The asset file contains a specific UID (`91727551...`). Hashing the path produces a completely different value. AssetManager has no mapping for the hashed UID.

**✅ CORRECT (loading existing asset):**
```cpp
// Option 1: Use virtual path (resolves to UID internally)
auto scene = AssetManager::load_asset<Scene>("/Game/scene.asset");

// Option 2: Look up UID from registered path
UID uid = AssetManager::get_uid_by_path(phys_path);  // Reads from uid_to_path_ map
auto scene = AssetManager::load_asset<Scene>(uid);
```

### When to Use UID::from_hash()

`UID::from_hash(path)` **is** useful, but for a different purpose: **generating deterministic UIDs for source assets**.

```cpp
// When importing a source file (FBX, OBJ, PNG) for the first time:
std::string source_path = "/Engine/models/hero.fbx";

// Generate deterministic UID from path
// Same path → same UID on every machine, every run
UID model_uid = UID::from_hash(source_path);

// Create the asset with this UID
auto model = std::make_shared<Model>(source_path, settings);
model->set_uid(model_uid);
AssetManager::save_asset(model, source_path + ".asset");
```

**Why use this:**
- **Consistent UIDs:** Artist A and Artist B get the same UID for `/Engine/models/hero.fbx`
- **No registry needed:** Source assets don't need a central UID database
- **Re-import safe:** Re-importing the same file generates the same UID, updating rather than duplicating

**The distinction:**

| Use Case | Method | UID Source |
|----------|--------|------------|
| **Source file** → Asset | `UID::from_hash(path)` | Deterministic from path |
| **Loading saved asset** | `get_uid_by_path(path)` | Read from `.asset` file header |

**Don't confuse them:**
- `from_hash()` generates UIDs (used when creating/importing)
- `get_uid_by_path()` looks up UIDs (used when loading existing assets)

### Path Mapping Storage

```cpp
// AssetManager maintains bidirectional maps
std::unordered_map<UID, std::string> uid_to_path_;      // UID → physical path
std::unordered_map<std::string, UID> path_to_uid_;      // physical path → UID
```

Populated during `init()` by scanning all `.asset` files and reading their headers via `peek_uid_from_file()`.

## Non-Asset Components

Components hold asset references but aren't Assets themselves:

```cpp
class MeshRendererComponent : public Component {
    ASSET_DEPS((ModelRef, model_))  // Generates load/save methods
    
    void on_init() {  // Called manually after loading!
        model_->get_submesh_count();  // Must be loaded first
    }
};

// Entity propagates to components
void Entity::load_asset_deps() {
    for (auto& comp : components_) {
        comp->load_asset_deps();  // Resolve component asset refs
    }
}
```

**Critical ordering:**
```cpp
scene->load_asset_deps();     // 1. Load all asset dependencies
init_scene_components(scene); // 2. Then call on_init()
```

**Why:** `on_init()` typically accesses assets (e.g., `model_->get_submesh_count()`). If called before `load_asset_deps()`, the model pointer exists but has 0 submeshes (meshes not loaded yet).

## Why Not Use Raw Pointers?

```cpp
// ❌ WRONG
class Bad { Texture* tex; };  // Becomes dangling on unload/reload

// ✅ CORRECT  
class Good { 
    TextureRef tex;      // Runtime pointer
    UID tex_uid;         // Persistent identity (auto by ASSET_DEPS)
};
```

| Scenario | Pointer | UID |
|----------|---------|-----|
| Asset unloaded | Dangling | Still valid |
| Hot-reload (artist update) | Old address | Maps to new object |
| Serialization | Lost | Preserved in JSON |

UIDs are **identity**. Pointers are just **memory locations**.

## Common Failures

| Symptom | Cause | Fix |
|---------|-------|-----|
| Model not rendered (0 submeshes) | `on_load_asset()` missing `load_asset_deps()` | Add call before accessing deps |
| Model pointer valid but empty | `sync_deps_to_slots()` before `load_asset_deps()` | Swap order |
| Component not rendering | `on_init()` before `load_asset_deps()` | Entity calls deps first, then `on_init()` |
| "Asset has no registered path" | `AssetManager::init()` not called | Initialize with game path |

## API

```cpp
// Loading
AssetManager::load_asset<T>(uid);
AssetManager::load_asset<T>("/Game/path.asset");

// Custom load logic (call load_asset_deps!)
void MyAsset::on_load_asset() {
    load_asset_deps();   // REQUIRED
    // ... custom ...
    on_load();
}

// Save
AssetManager::save_asset(asset, "/Game/path.asset");

// Path utils
AssetManager::get_physical_path("/Game/...");
AssetManager::get_uid_by_path("/Game/...");
```
