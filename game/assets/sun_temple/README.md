# Sun Temple data

- `model.asset`, `scene.asset`, and UUID-named `.binasset` files: the initial imported renderer scene and its embedded dependencies.
- `source/`: the complete original ORCA SunTemple_v4 package, including textures and environment maps for reimport.
- Large binary files are stored with Git LFS. Run `git lfs pull` after cloning.

Run from the repository root:

```powershell
xmake build sun_temple
xmake run sun_temple "game/assets/sun_temple/source"
```

This is the initial import checkpoint; remaining rendering and regression-test issues are recorded in `docs/sun_temple.md`.

## Attribution and license

Unreal Engine Sun Temple, Epic Games, 2017, Open Research Content Archive (ORCA).
Exported from Unreal Engine by Kai-Hwa Yao and Nicholas Hull.

The source assets are licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
The imported assets are derived from that source and retain the same license.
See [source/LICENSE.txt](source/LICENSE.txt), [source/README.txt](source/README.txt), and
[the original ORCA page](https://developer.nvidia.com/orca/epic-games-sun-temple).
