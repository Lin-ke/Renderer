# Sun Temple 加载优化

目标：将当前 Sun Temple 原生场景的加载开销控制在 10 秒内，并验证资源和最终渲染结果正确。

## 测量口径

- Windows ARM64，本机 10 个加载 Worker；1280×720，Bloom 开启，隐藏窗口。
- 使用现有 `game/assets/sun_temple` 原生资源：517 个可渲染实体、1,056 个 Mesh 实例，全部加载。
- `LoadScene`：从开始读取场景到 AssetManager 完成依赖初始化和加载提交刷新。
- `FirstFrameReady`：从开始加载场景到第一帧 GPU 完成并 Present 返回。
- 启动到首帧：Profile 会话开始到首帧完成，包含引擎初始化和资源目录扫描。
- 各次运行使用独立进程，测量时不并行构建或测试，没有主动清除 OS 文件缓存。
- 基线来自 `build/test-results/sun-temple-investigation-{debug,release}.json`。

## 基线与第一轮结果

| 实现 | 构建 | LoadScene | 读取/反序列化等待 |
|---|---|---:|---:|
| 原始字符串流解析 | Debug | 88.429 s | 84.585 s |
| 原始字符串流解析 | Release | 85.758 s | 83.014 s |
| 仅替换标量解析 | Debug | 26.032 s | 21.848 s |
| 同时优化二进制向量数组读取，首次实测 | Debug | 5.301 s | 1.424 s |

第一轮只用 `from_chars` 替换了向量字符串中的数字提取，仍保留 cereal 对每个元素的完整调用和临时字符串分配，所以尚未达到目标。第二轮复用二进制读取缓冲区，移除数组内逐元素的通用归档派发后达到目标。

## 最终重复测量与验收

以下六次运行使用最终实现，每次独立启动进程，固定为 6 帧并保存第 3 帧截图。单位为秒。

| 构建 | 次数 | LoadScene | 读取等待 | 加载到首帧 | 启动到首帧 | 整个进程墙钟 |
|---|---:|---:|---:|---:|---:|---:|
| Debug | 1 | 4.996 | 1.254 | 5.141 | 6.084 | 9.360 |
| Debug | 2 | 4.742 | 1.303 | 4.899 | 5.818 | 8.929 |
| Debug | 3 | 4.686 | 1.275 | 4.843 | 5.821 | 8.962 |
| Release | 1 | 3.729 | 0.682 | 3.807 | 4.448 | 6.068 |
| Release | 2 | 3.207 | 0.429 | 3.277 | 3.862 | 5.288 |
| Release | 3 | 3.181 | 0.449 | 3.252 | 3.861 | 5.517 |

验收：六次的场景加载和启动到首帧均低于 10 秒；连同 6 帧渲染、截图、退出及报告写入的整个进程墙钟也均低于 10 秒。每次都保留 517 个实体、1,056 个 Mesh 实例；各有 6 个图和 6 个 GPU 帧记录，丢失事件为 0。

Debug 加载三次均值为 4.808 秒，Release 为 3.372 秒。对应基线分别为 88.429 和 85.758 秒。

六张最终截图分别与同构建模式的原始基线比较，RGB 像素全部一致。没有通过减少场景内容、降低纹理质量、跳过 GPU 初始化或重写资源来满足耗时目标。

逐次原始数值同时保存在 `build/test-results/load-optimization-verified-runs.json`；全进程时间由外层 PowerShell Stopwatch 记录，包含进程启动和结束开销，与应用内部阶段计时相互独立。

## 实现

代码位于 `engine/core/reflect/math_reflect.h`。

### 无字符串流的数字解析

紧凑数学文本使用 `std::from_chars` 解析，返回 `std::optional`。覆盖浮点/整数向量，以及 Quaternion 和 Extent。显式处理 ASCII 空白、正号和科学计数法。异常文本回退到原来的字符串流读取，以保留旧格式行为；新代码不抛异常。

写入逻辑不变，仍按原有六位小数文本保存。没有改写资源、重新导入场景或改变资源路径。

### 兼容旧格式的二进制数组读取

现有 `.binasset` 数学向量数组格式为：

```text
uint64 元素数量
重复每个元素：
    uint64 字符串字节数
    紧凑向量文本
```

为 `BinaryInputArchive` 的数学向量数组提供直接读取入口，维持同一套字节格式。每个数组复用 256 字节栈缓冲区；长文本使用可复用的动态字符串。通过原始 archive 的 `loadBinary` 消费数据，仍由现有归档错误处理捕获短读等问题。

这条路径减少了数百万个临时字符串及通用序列化调用。JSON 数组和其他 Archive 继续走原有路径。未来若修改数学类型的二进制写入格式，必须同步调整此读取约定，并设计版本兼容。

## 正确性验证

- 新增 `[asset_load]` 功能测试：保存 JSON 和二进制 Mesh，卸载后通过 AssetManager 加载，再从真实 GPU 回读 position、normal、tangent、UV 和 index，整体比较全部字节。
- JSON 用例包含正号、科学计数法和转义空白；二进制用例包含超过栈缓冲区长度的旧式文本。
- 实际 Sun Temple 运行检查场景数量、GPU 输出截图、图/GPU 帧数量及丢失事件计数。
- Debug 优化后的截图与原始 Debug 基线按 RGB 全像素比较。不能使用 RGBA 差分的默认 alpha 包围盒代替 RGB 比较。
- 最终重复测量结果见上表，全量功能测试结果见下方验证记录。

## 构建与功能测试结果

- `xmake build -a`：Debug 下 engine、game、sun_temple、utest 全部构建通过。
- Release 的 `sun_temple` 构建和三次实际运行通过。
- `xmake run utest`：**37 个功能测试、469 个断言全部通过**，日志为 `build/load-optimization-utest.log`。
- `[asset_load]` 覆盖旧式 JSON 数字文本、超长二进制文本以及全部几何流的 GPU 回读；没有新增逐分量的逻辑测试或固定时间单元测试。
- 实际性能验收使用上述六次进程测量。当前 xmake 配置已恢复为 Debug。

## 复现

```powershell
xmake f -y -m debug
xmake build -a
xmake run utest
xmake run sun_temple game/assets/sun_temple/source --profile --bloom --frames 6 --profile-output build/test-results/load-optimization-debug.json --capture build/test-results/load-optimization-debug.png
```

Release 使用相同资源和参数，先配置 `xmake f -y -m release` 并构建 `sun_temple`。结束后恢复 Debug 配置。

完整 JSON、PNG 和运行日志保存在 `build/test-results/load-optimization-*` 与 `build/load-optimization-*.log`。本次以加载时间为验收目标，未更改 Mesh 的磁盘格式、GPU 绑定缓存、显存子分配或上传回收策略。
