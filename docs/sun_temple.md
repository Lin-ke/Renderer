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

- 静态节点变换烘焙到 Mesh；法线使用逆转置，镜像节点修正绕序及切线符号。
- `ModelImportOptions` 提供左手坐标转换和 Specular-as-ARM 两个源格式选项。
- 使用 DirectXTex 解码 DDS，原始压缩图片嵌入原生纹理资源；RHI 上传使用紧密排列的行数据。
- GBuffer 使用实际切线构建 TBN，重建 BC5 法线 Z；材质剔除模式支持双面植被。
- 场景使用导出相机的位置与朝向，并配置方向光。当前照明没有完整复现原场景的环境光和反射效果。

## 验证状态

- Debug 目标曾成功构建，首次真实导入生成了 1,056 个 Mesh 的场景截图：`build/test-results/sun_temple_first.png`。
- 首张截图能显示几何与材质，拱顶仍有条纹，需要继续定位深度问题。
- 新增变换导入回归测试运行时出现 SIGSEGV，尚未定位。
- Sun Temple 集成测试已通过，覆盖实际导入、原生资源重载和隐藏窗口截图。全量 Debug 测试仍有可在改动前复现的 DX12 诊断消息、像素断言和模型导入失败。

```powershell
xmake build utest
xmake run utest "[dds],[import]"

$env:SUN_TEMPLE_DIR = (Resolve-Path "game/assets/sun_temple/source").Path
xmake run utest "[sun_temple]"
```

Sun Temple 集成测试默认隐藏，需显式选择标签；资源许可与署名信息见原始资源包中的 `LICENSE.txt` 和 `README.txt`。
