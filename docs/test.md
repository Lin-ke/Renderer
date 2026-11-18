# 功能测试约定

测试以真实功能输出为单位：资源导入和保存重载、场景/Prefab 持久化、任务执行、GPU 计算/绘制/拷贝及截图、Profile 报告导出。删除只检查数学计算、getter/setter、内部容器内容或编译器中间状态的独立逻辑测试。

## 生命周期

Catch2 的 `test_main.cpp` 统一初始化并关闭隐藏窗口引擎。具体 `TEST_CASE` 不重复初始化引擎，开头和结尾调用 `test_utils::TestContext::reset()`。全局夹具使用 SingleThread；实际 Game/Render/Device 队列通过 Sun Temple 有限帧样例验证，Bloom 也有实际线程构图/录制与像素回读测试。

临时资源放在进程专属的 `build/test-work` 项目，通过 `/Game` 访问；退出时清理。截图、Profile 报告等可查看结果放在 `build/test-results`。测试不改写仓库资源。

## 断言

保留运行所需的关键前置条件和最终功能结果。`REQUIRE` 用于失败后无法继续的条件，`CHECK` 用于输出验证，浮点比较用 `Catch::Approx`。

像素和 Buffer 仍验证全部数据，但先聚合比较结果，再执行一次断言，不在每个像素/元素上调用断言。RGBA 图像可使用 `test_utils::image_matches(pixels, width, expected)`，expected(x,y) 返回四通道预期值，可传通道容差。大型场景验证截图、亮度和色彩分布，不依赖 UI 亮度或内部实现顺序。

不要断言固定的 Pass 创建顺序、内部池对象数量、资源字段的重复 getter/setter 结果。依赖排序和资源复用通过真正执行 compute/render/copy 并回读输出验证。失败时保存图像和必要上下文，方便定位。

## 运行

```powershell
xmake build utest
xmake run utest
xmake run utest "[bloom]"
xmake run utest "[profile]"
```

常用标签为 `[render]`、`[rhi][dx12]`、`[rdg]`、`[render_resource]`、`[scene]`、`[prefab]`、`[profile]`。Sun Temple 导入用例默认隐藏，按 [sun_temple.md](sun_temple.md) 显式运行。

渲染测试通过 `RenderTestApp` 或 RHI/RDG 执行真实命令，使用隐藏窗口，截图检查最终输出；具体接口见 [dx12.md](dx12.md) 和 [rdg.md](rdg.md)。性能数据采用 [profile.md](profile.md) 的会话报告，不写固定耗时断言。
