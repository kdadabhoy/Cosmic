# Old / parked projects

Projects in this folder are **excluded from `build_all`**. The root CMake scans
`Projects/*` for a `CMakeLists.txt`; since these apps live one level deeper
(`Projects/Old/<App>`), they're never configured or built.

Currently parked: `SF_DrivetrainCalcsApp`, `SF_Telem_Weapon`, `Shear_Force_TelemApp`.

To bring one back into the build, just move it up a level, e.g.:

```
move Old\SF_Telem_Weapon ..\SF_Telem_Weapon      (Windows)
mv Old/SF_Telem_Weapon ../SF_Telem_Weapon         (bash)
```

Then re-run `build_all.bat`. The active apps are **SF_Telem** and **SF_TelemTest**.
