# Sun Temple 导入与查看

使用 ORCA SunTemple_v4 的 `SunTemple.fbx`、`Textures/*.dds` 和 `SunTemple_Skybox.hdr`。
资源包 README 规定 `Specular.dds` 的 RGB 分别为 AO、Roughness、Metallic，法线采用 DirectX 约定。
原始资源随仓库保存在 `game/assets/sun_temple/source/`，转换结果保存在 `game/assets/sun_temple/`。
FBX、DDS、HDR 和二进制原生资源使用 Git LFS；克隆后先运行 `git lfs pull`。
资源许可与署名文件保留在 `source/LICENSE.txt` 和 `source/README.txt` 中。

```powershell
xmake build sun_temple
xmake run sun_temple "game/assets/sun_temple/source" --reimport

# 从项目内的原生资源加载
xmake run sun_temple "game/assets/sun_temple/source"

# 隐藏窗口截图，完成后退出
xmake run sun_temple "game/assets/sun_temple/source" --capture "build/test-results/sun_temple.png"
```

WASD 移动，右键拖动转向，Space/Ctrl 升降，Ctrl+S 保存场景。
`--frames N` 限制运行帧数，`--verbose` 输出完整导入日志。

```powershell
xmake f --mode=release
xmake build sun_temple
xmake run sun_temple "game/assets/sun_temple/source" --benchmark --frames 150
```

基准会跳过前 30 帧，输出平均帧时间、P95、录制与提交耗时、GPU Pass 耗时及可见网格批次数。加入 `--capture <png-path>` 可在隐藏窗口运行并保存第 3 帧，用于比较像素输出。

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
