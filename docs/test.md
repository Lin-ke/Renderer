# 测试编写范式

本文档定义了项目测试编写的标准范式。所有测试应遵循这些约定以保证一致性和可维护性。

---

## 1. 测试文件组织

### 1.1 目录结构

```
test/
├── test_main.cpp              # 测试入口（保持简洁）
├── test_utils.h/.cpp          # 测试工具类（复用基础设施）
├── render_resource/           # 渲染资源测试
│   └── test_render_resources.cpp
├── draw/                      # 绘制测试
│   ├── test_draw_basic.cpp
│   └── test_draw_advanced.cpp
├── test_scene_core.cpp        # 场景核心测试
├── test_core_systems.cpp      # 核心系统测试
├── test_reflection.cpp        # 独立：反射系统
├── test_rdg.cpp               # 独立：RDG系统
└── test_tick_system.cpp       # 独立：Tick系统
```

### 1.2 合并原则

- **单一职责**：每个测试文件聚焦一个功能域
- **避免碎片**：相关测试合并到同一文件，使用 `SECTION` 区分
- **独立例外**：复杂或独立的系统（如反射、RDG）保持单独文件

---

## 2. 测试文件模板

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "test/test_utils.h"
#include "engine/main/engine_context.h"
// 其他必要的 include...

DEFINE_LOG_TAG(LogTestCategory, "TestCategory");

// 可选：共享的 helper 函数
static std::vector<uint8_t> helper_function(const std::string& input) {
    // ...
}

TEST_CASE("Test Case Name", "[tag]") {
    test_utils::TestContext::reset();
    
    // Test implementation
    
    test_utils::TestContext::reset();
}

TEST_CASE("Test Group Name", "[tag]") {
    test_utils::TestContext::reset();
    
    SECTION("Scenario A") {
        // Test A
    }
    
    SECTION("Scenario B") {
        // Test B
    }
    
    test_utils::TestContext::reset();
}
```

---

## 3. 命名规范

| 元素 | 规范 | 示例 |
|------|------|------|
| 测试文件 | `test_*.cpp` | `test_render_resources.cpp` |
| TEST_CASE | 描述性句子 | `"Texture RHI Initialization"` |
| SECTION | 场景描述 | `"Material Parameters"` |
| 标签 | 小写，方括号 | `[render_resource]` |
| Log Tag | 大驼峰 | `LogRenderResource` |

### 3.1 标签体系

| 标签 | 用途 |
|------|------|
| `[render_resource]` | 渲染资源测试（Texture/Shader/Material/Model） |
| `[draw]` | 基础绘制测试 |
| `[draw][advanced]` | 高级渲染测试（Deferred/RDG） |
| `[scene]` | 场景系统测试 |
| `[core]` | 核心系统测试（ThreadPool/Light） |
| `[reflection]` | 反射系统测试 |
| `[rdg]` | RDG 系统测试 |
| `[tick]` | Tick 系统测试 |

---

## 4. 测试生命周期

### 4.1 标准模式

每个 `TEST_CASE` 必须遵循以下模式：

```cpp
TEST_CASE("Example Test", "[tag]") {
    // 1. 重置测试状态
    test_utils::TestContext::reset();
    
    // 2. 初始化测试数据
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    // 3. 执行测试逻辑
    REQUIRE(EngineContext::rhi() != nullptr);
    // ...
    
    // 4. 清理资源（可选，reset会处理大部分）
    // ...
    
    // 5. 最终重置
    test_utils::TestContext::reset();
}
```

### 4.2 使用 SECTION 组织测试

当多个测试共享相同的初始化逻辑时，使用 `SECTION`：

```cpp
TEST_CASE("Material System", "[render_resource]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

    SECTION("PBR Material") {
        auto material = std::make_shared<PBRMaterial>();
        // ...
    }

    SECTION("NPR Material") {
        auto material = std::make_shared<NPRMaterial>();
        // ...
    }
    
    test_utils::TestContext::reset();
}
```

**注意**：
- `SECTION` 内的代码在每次执行时会重新运行外部代码
- 不要在 `SECTION` 之间共享可变状态

---

## 5. 断言使用规范

### 5.1 断言选择

| 断言 | 用途 |
|------|------|
| `REQUIRE(expr)` | 关键断言，失败立即停止测试 |
| `CHECK(expr)` | 非关键断言，失败继续测试 |
| `REQUIRE_FALSE(expr)` | 要求表达式为 false |
| `CHECK_FALSE(expr)` | 检查表达式为 false（非关键） |
| `REQUIRE(value == Approx(expected))` | 浮点数比较 |

### 5.2 浮点数比较

```cpp
// 正确：使用 Catch::Approx
REQUIRE(value == Catch::Approx(1.0f));

// 指定误差范围
REQUIRE(value == Catch::Approx(1.0f).epsilon(0.001));
REQUIRE(value == Catch::Approx(1.0f).margin(0.01));
```

### 5.3 指针检查

```cpp
REQUIRE(ptr != nullptr);           // 必须非空
CHECK(ptr->is_valid());            // 检查状态
```

---

## 6. 资源管理

### 6.1 智能指针

始终使用智能指针管理资源：

```cpp
{
    auto texture = std::make_shared<Texture>(...);
    // 使用 texture...
    
    // 显式重置（可选，离开作用域会自动释放）
    texture.reset();
}
```

### 6.2 测试间隔离

```cpp
// 每个 TEST_CASE 后调用 reset
test_utils::TestContext::reset();

// reset 会自动：
// - 清除活跃场景
// - 等待 GPU 完成
// - 清理渲染系统状态
```

---

## 7. 辅助工具

### 7.1 日志输出

```cpp
DEFINE_LOG_TAG(LogMyTest, "MyTest");

TEST_CASE("Example", "[tag]") {
    INFO(LogMyTest, "Starting test...");
    // ...
    WARN(LogMyTest, "Optional warning: {}", value);
    ERR(LogMyTest, "Error: {}", error_msg);
}
```

### 7.2 场景加载

```cpp
auto result = test_utils::SceneLoader::load("/Game/scene.asset", true);
REQUIRE(result.success);
REQUIRE(result.scene != nullptr);
REQUIRE(result.camera != nullptr);
```

### 7.3 渲染测试

```cpp
test_utils::RenderTestApp::Config config;
config.scene_path = "/Game/test_scene.asset";
config.width = 1280;
config.height = 720;
config.max_frames = 60;
config.capture_frame = 30;
config.create_scene_func = create_scene_func;

std::vector<uint8_t> screenshot_data;
int frames = 0;
bool screenshot_taken = test_utils::RenderTestApp::run(config, screenshot_data, &frames);
```

---

## 8. 常见反模式

### ❌ 避免

```cpp
// 1. 缺少 reset
TEST_CASE("Bad Test", "[tag]") {
    // 没有 test_utils::TestContext::reset();
    // 测试...
    // 没有最终的 reset
}

// 2. SECTION 共享可变状态
TEST_CASE("Bad Sections", "[tag]") {
    int value = 0;
    
    SECTION("A") {
        value = 10;  // 修改共享状态
    }
    
    SECTION("B") {
        REQUIRE(value == 0);  // 可能失败，取决于执行顺序
    }
}

// 3. 直接使用 Catch::Session
int main(int argc, char* argv[]) {
    // 不要自定义 main，使用 test_main.cpp
}
```

### ✅ 推荐

```cpp
// 1. 完整的生命周期
TEST_CASE("Good Test", "[tag]") {
    test_utils::TestContext::reset();
    // ... 测试代码 ...
    test_utils::TestContext::reset();
}

// 2. SECTION 独立
TEST_CASE("Good Sections", "[tag]") {
    SECTION("A") {
        int value = 10;
        REQUIRE(value == 10);
    }
    
    SECTION("B") {
        int value = 0;
        REQUIRE(value == 0);
    }
}
```

---

## 9. 运行测试

### 9.1 运行所有测试

```bash
xmake run utest
```

### 9.2 运行特定标签

```bash
xmake run utest "[render_resource]"
xmake run utest "[draw]"
xmake run utest "[scene]"
```

### 9.3 运行特定测试

```bash
xmake run utest "Texture RHI Initialization"
```

---

## 10. 测试精简原则

### 10.1 删除标准

以下测试应该被删除或合并：

| 类型 | 示例 | 原因 |
|------|------|------|
| 简单getter/setter测试 | `CHECK(setting.smooth_normal == true)` | 只是检查bool赋值 |
| 空值检查测试 | `REQUIRE(ptr != nullptr)` | 过于简单，无实质价值 |
| 文件存在性检查 | 检查MTL文件内容 | 功能测试应覆盖 |
| 重复测试 | 多个submesh测试（bunny只有1个） | 数据不适合 |
| 过长测试 | 300帧渲染循环 | 影响测试速度 |
| 独立backend测试 | 创建独立DX11 backend | 干扰测试环境 |

### 10.2 保留标准

以下测试应该保留：

1. **功能完整性测试** - 测试完整的渲染流程
2. **序列化/反序列化测试** - 验证数据持久化
3. **依赖关系测试** - 验证资源引用正确
4. **集成测试** - 测试多组件协作

---

## 11. 测试统计

### 11.1 第一阶段：文件合并

| 类别 | 文件数 | 合并前 | 合并后 |
|------|--------|--------|--------|
| render_resource | 1 | 7 | 1 |
| draw | 2 | 5 | 2 |
| scene/core | 2 | 4 | 2 |
| 独立测试 | 4 | 4 | 4 |
| **总计** | **9** | **20** | **9** |

### 11.2 第二阶段：内容精简

| 文件 | 精简前行数 | 精简后行数 | 删除率 |
|------|-----------|-----------|--------|
| test_render_resources.cpp | 495 | ~295 | -40% |
| test_draw_basic.cpp | 704 | ~320 | -55% |
| test_draw_advanced.cpp | 383 | ~210 | -45% |
| test_core_systems.cpp | 352 | ~280 | -20% |
| test_scene_core.cpp | 370 | ~238 | -36% |

### 11.3 最终统计

| 指标 | 原始 | 最终 | 优化率 |
|------|------|------|--------|
| 测试文件数 | 20 | 9 | -55% |
| 代码总行数 | ~2300 | ~1350 | -41% |
| TEST_CASE数 | ~35 | ~20 | -43% |

**优化收益**：显著减少维护负担和编译时间，保留核心功能测试覆盖。
