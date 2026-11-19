# Sun Temple 导入与查看

从 [NVIDIA ORCA 的 UE4 Sun Temple 页面](https://developer.nvidia.com/ue4-sun-temple) 下载 `SunTemple_v4.zip`。解压后，把包含 `SunTemple.fbx`、`Textures/` 和 `SunTemple_Skybox.hdr` 的目录放到 `game/assets/sun_temple/source/`，或在下面的命令中传入该目录的实际路径。

导入生成的 `.asset` 和 `.binasset` 保存在本地 `game/assets/sun_temple/`。Git 会忽略这个目录中的源文件和生成文件。资源包的 `LICENSE.txt` 和 `README.txt` 记录许可、署名及贴图约定。`Specular.dds` 的 RGB 分别为 AO、Roughness、Metallic，法线采用 DirectX 约定。

```powershell
xmake build sun_temple
xmake run sun_temple "game/assets/sun_temple/source" --reimport

# 从项目内的原生资源加载
xmake run sun_temple "game/assets/sun_temple/source"

# 隐藏窗口截图，完成后退出
xmake run sun_temple "game/assets/sun_temple/source" --capture "build/test-results/sun_temple.png"

# 在 Render/Device Thread 路径验证 Bloom
xmake run sun_temple "game/assets/sun_temple/source" --bloom --capture "build/test-results/sun_temple_bloom.png" --frames 6
```

WASD 移动，右键拖动转向，Space/Ctrl 升降，Ctrl+S 保存场景。
`--frames N` 限制运行帧数，`--verbose` 输出完整导入日志。
`--bloom` 开启半分辨率亮部提取与横纵高斯模糊，参数默认 threshold=0.25、intensity=0.8。游戏编辑器的 Renderer Debug 面板也提供 Bloom 开关和参数。

`--gpu-driven` 启用第一阶段的 GPU 视锥剔除与 Indexed Indirect 纯色几何路径，默认关闭。它用于验证真实三线程场景提交，当前不包含 PBR/贴图和 Alpha Mask，不能与默认路径作同画质性能对照。运行和实现边界见 [gpu_driven.md](gpu_driven.md)。

```powershell
xmake f --mode=release
xmake build sun_temple
xmake run sun_temple "game/assets/sun_temple/source" --benchmark --frames 150
```

基准会跳过前 30 个完成帧，输出完成帧吞吐率、Device 墙钟时间与 GPU 整帧时间的均值/P95，并保存 Profile JSON。加入 `--capture <png-path>` 可在隐藏窗口运行并保存第 3 帧，用于比较像素输出。

## 加载与资源采集

```powershell
xmake run sun_temple game/assets/sun_temple/source --profile --frames 6 --bloom --capture build/test-results/profile-sun-temple.png
```

控制台输出初始化、场景加载和首帧耗时，以及阶段边界的 CPU/GPU 内存。默认报告为 `build/test-results/sun-temple-profile.json`，可用 `--profile-output` 指定。报告包含跨线程 CPU 时间线、GPU 帧/Pass、RDG 依赖/lifetime/物理资源映射和 Device 显存快照。采集口径、CPU 内存 API 和当前覆盖范围见 [profile.md](profile.md)。

## 实现

- 场景导入保留 FBX 节点层级和局部变换。导入时根据世界变换决定三角形索引顺序，渲染统一使用逆时针正面。引用同一组网格和材质、索引顺序相同的实体共享 Model。
- `scene.asset` 引用多个 Model。生成的 Model、Mesh、Material、Texture 按 UID 保存在同一目录，打开原生场景时直接加载这些依赖。
- `ModelImportOptions` 提供左手坐标转换和 Specular-as-ARM 两个源格式选项。
- 导入时从 DDS 取出 BC1、BC3、BC5 的原始 mip 数据，以 Zstd 帧存入纹理 `.binasset` 的数据字段。DX12 加载时解开 Zstd 并逐级上传 BC 块，保留贴图的 sRGB 或线性采样。
- GBuffer 使用实际切线构建 TBN，重建 BC5 法线 Z；材质剔除模式支持双面植被。
- 场景使用导出相机的位置与朝向，并配置方向光。当前照明没有完整复现原场景的环境光和反射效果。

## 验证状态

- Debug 目标曾成功构建，首次真实导入生成了 1,056 个 Mesh 的场景截图：`build/test-results/sun_temple_first.png`。
- 首张截图能显示几何与材质，拱顶仍有条纹，需要继续定位深度问题。
- 分层场景重导得到 517 个可渲染实体和 1,056 个网格实例。148 张 BC 纹理写入约 273 MiB 的 Zstd 数据；原生场景可在不提供源资源目录的情况下打开并截图。
- `[dds]` 验证资源内只保存压缩块，并逐级回读 GPU 上的 BC 数据。`[scene_import]` 验证普通和镜像实例的绕序、同绕序实例的 Model 复用，以及保存重载。`[sun_temple]` 验证实际导入、原生资源重载和隐藏窗口截图。
- 单模型 glTF 导入的崩溃源于 cereal 和 Assimp 的 RapidJSON 符号冲突。构建已隔离 cereal 的命名空间，原导入用例用于回归验证。

```powershell
xmake build utest
xmake run utest "[dds],[scene_import]"

$env:SUN_TEMPLE_DIR = (Resolve-Path "game/assets/sun_temple/source").Path
xmake run utest "[sun_temple]"
```

Sun Temple 集成测试默认隐藏，需显式选择标签；资源许可与署名信息见原始资源包中的 `LICENSE.txt` 和 `README.txt`。
