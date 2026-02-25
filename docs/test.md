# 测试编写范式 (Testing Paradigm)

本文档定义了项目测试编写的标准范式。所有测试应遵循这些约定以保证一致性、独立性和可维护性�?

---

## 1. 测试生命周期管理 (TestContext)

我们的测试环境基�?`Catch2` 框架。为了避免频繁地完整重启引擎带来的巨大开销，我们使用了全局 `TestContext` 结合局部的 `reset`�?

### 1.1 全局 Engine 生命周期
`test_main.cpp` 中负责全局�?Engine 初始化和关闭�?*不要在具体的 `TEST_CASE` 中初始化引擎**�?

```cpp
// �?test_main.cpp 中自动执�?
test_utils::TestContext::init_engine();
int result = Catch::Session().run(argc, argv);
test_utils::TestContext::shutdown_engine();
```

### 1.2 `TEST_CASE` 级生命周�?
每个 `TEST_CASE` 的开头和结尾必须调用 `test_utils::TestContext::reset()`，以确保测试间的隔离状态（清除活跃场景、清空渲染批次等）�?

```cpp
TEST_CASE("Example Test", "[tag]") {
    test_utils::TestContext::reset(); // 1. 重置上一个测试可能残留的状�?
    
    // 2. 准备测试资产目录
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    // 3. 执行测试逻辑
    REQUIRE(EngineContext::rhi() != nullptr);
    
    test_utils::TestContext::reset(); // 4. 最终清�?
}
```

---

## 2. 实用测试工具 (`test_utils`)

`test_utils.h` 提供了大量的辅助工具类和函数来简化测试编写�?

### 2.1 临时资产清理 (`ScopedAssetCleanup`)
当测试需要生成临时的 `.asset` 文件并要求自动清理时，使�?`ScopedAssetCleanup`。它会在作用域结束时自动清理基于 UUID 命名的资产文件�?

```cpp
SECTION("Test Material Save/Load") {
    auto cleanup = test_utils::ScopedAssetCleanup(test_asset_dir);
    
    // ... 生成并保存临时资�?...
    
    // 离开作用域时，生成的临时UUID文件会被自动删除
}
```

### 2.2 场景加载 (`SceneLoader`)
用于一键式处理场景的虚拟路径解析、UID查找、加载、以及组件初始化�?

```cpp
auto result = test_utils::SceneLoader::load("/Game/scene.asset", true);
REQUIRE(result.is_valid());
REQUIRE(result.scene != nullptr);
REQUIRE(result.camera != nullptr);
```

### 2.3 渲染测试框架 (`RenderTestApp`)
当你需要执行真实的 GPU 绘制并获取截图（或验证渲染流程）时，使用 `RenderTestApp`�?

```cpp
test_utils::RenderTestApp::Config config;
config.scene_path = "/Game/test_scene.asset";
config.width = 1280;
config.height = 720;
config.max_frames = 60;
config.capture_frame = 30; // 捕获�?0帧作为截图验�?
config.create_scene_func = custom_scene_setup;

std::vector<uint8_t> screenshot_data;
int frames_rendered = 0;
bool success = test_utils::RenderTestApp::run(config, screenshot_data, &frames_rendered);

REQUIRE(success);
// 验证图像亮度
float brightness = test_utils::calculate_average_brightness(screenshot_data);
REQUIRE(brightness > 10.0f);
```

---

## 3. 测试文件组织与标�?

测试文件应当保持**单一职责**，并通过 Catch2 �?Tag 系统进行归类�?

### 目录结构参�?
```
test/
├── test_main.cpp              # Catch2 Session 入口，管理引擎全局启停
├── test_utils.h/.cpp          # 测试核心基建（TestContext, SceneLoader 等）
├── render/
�?  ├── test_basic.cpp         # 基础绘制测试 [draw]
�?  ├── test_pbr.cpp           # PBR渲染测试 [draw][advanced]
�?  ├── test_rdg.cpp           # RDG 框架测试 [rdg]
�?  └── test_rdg_builder.cpp
├── render_resource/
�?  └── test_render_resources.cpp # 纹理/材质/Buffer等资源测�?[render_resource]
└── test_scene_core.cpp        # 场景和组件系�?[scene]
```

### 常用标签体系
- `[render_resource]`：渲染资�?(Texture/Buffer/Shader) 相关
- `[draw]`：各类绘制测�?
- `[scene]`：场景系统、实体和组件测试
- `[core]`：底层基础设施如日志、线程池
- `[reflection]`：反射系统测�?
- `[rdg]`：渲染依赖图 (Render Dependency Graph) 测试

---

## 4. 断言与最佳实�?

### 4.1 断言选择
- 使用 `REQUIRE(expr)` 作为关键断言，失败会立即终止当前测试�?
- 使用 `CHECK(expr)` 进行非关键验证，允许继续执行收集更多错误信息�?
- 浮点数对比必须使�?`Catch::Approx`�?
  ```cpp
  REQUIRE(value == Catch::Approx(1.0f).margin(0.001));
  ```

### 4.2 SECTION 的使�?
在同一�?`TEST_CASE` 中使�?`SECTION` 可以重用公共�?Setup 代码�?*注意：每次运�?`SECTION` 时，它外部的 `TEST_CASE` 代码都会从头执行一次�?*

```cpp
TEST_CASE("Buffer Tests", "[render_resource]") {
    test_utils::TestContext::reset();
    auto buffer = create_shared_buffer(); // 每个 SECTION 都会创建一个新 Buffer
    
    SECTION("Vertex Buffer") {
        // ...
    }
    SECTION("Index Buffer") {
        // ...
    }
    test_utils::TestContext::reset();
}
```

### 4.3 避免反模�?
- �?忘记在测试末尾调�?`TestContext::reset()`�?
- �?�?`SECTION` 之间共享可变状态�?
- �?编写没有验证逻辑的空测试（如仅仅检查非空而不验证行为）�?
- �?将临时资产写入源码目录且不清理（必须使用 `ScopedAssetCleanup`）�?

---

## 5. 运行测试

```bash
# 运行所有测�?
xmake run utest

# 运行特定标签（例如仅运行 RDG 测试�?
xmake run utest "[rdg]"

# 运行特定名称的测�?
xmake run utest "Test Case Name"
```