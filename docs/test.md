# 渲染测试规范

## 三段式
```
Part 1: Create    Part 2: Load                 Part 3: Render
────────────      ─────────                    ─────────────
创建Scene         auto r=SceneLoader::load()   渲染60帧，第30帧截图
设置Camera/Model  r.scene/camera/success       CHECK(brightness>1)
保存.asset
```

## 模板
```cpp
static const std::string P = "/Game/test.asset";
bool create() {  // Part 1
    auto s = std::make_shared<Scene>();
    s->create_entity()->add_component<CameraComponent>()->on_init();
    s->create_entity()->add_component<MeshRendererComponent>()->set_model(Model::Load(...));
    EngineContext::asset()->save_asset(s, P); return true;
}
TEST_CASE("Test", "[draw]") {
    EngineContext::init({Asset, Window, Render, SingleThread});
    REQUIRE(create());  // 可注释
    auto r = test_utils::SceneLoader::load(P); REQUIRE(r.success);  // Part 2
    std::vector<uint8_t> d(1280*720*4);  // Part 3
    for (int f=0; f<60; f++) {
        EngineContext::world()->tick(0.016f);
        EngineContext::render_system()->tick({r.camera, r.scene.get()});
        if (f==30) capture_screenshot(d);
    }
    test_utils::save_screenshot_png("out.png", 1280, 720, d);
    CHECK(test_utils::calculate_average_brightness(d) > 1.0f);
    EngineContext::exit();
}
```

## API
- `SceneLoader::load(path)` - 加载Scene
- `scene->get_camera()` - 获取Camera
- `save_screenshot_png(p,w,h,d)` - 保存截图
- `calculate_average_brightness(d)` - 亮度验证

## 注意
1. Part 1创建后可注释跳过
2. 必须用SceneLoader，不要手写加载逻辑
3. 亮度>1.0f确保渲染成功
