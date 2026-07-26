# Assets & the Virtual File System — Guide

**What this covers:** getting files into your project and out of it — the `engine://`,
`project://` and `user://` schemes and exactly what each resolves to in a dev tree versus a
shipped install, the two ways `project://` mounts, **per-app `user://` isolation and portable
mode**, the `AssetLibrary` cache and what it does on a miss, importing models and textures, the
`.cmeta` sidecar that pins import units, TOML configuration through `Config`, and the small
utility surface around all of it: `FileDialog`, `FileWatcher`, `ImageIO`, `DataExport`.
**Source of truth:** `Cosmic/src/utils/FileSystem.{h,cpp}`, `assets/AssetLibrary.{h,cpp}`,
`assets/MeshImport.{h,cpp}`, `utils/Config.{h,cpp}`, `utils/FileDialog.{h,cpp}`,
`utils/FileWatcher.h`, `utils/ImageIO.h`, `utils/DataExport.{h,cpp}`, `utils/Branding.h`,
`graphics/Texture.cpp`, `platform/OpenGL/OpenGLTexture.cpp`, `graphics/Shader.cpp`,
`core/Application.cpp`, `Runtime/Main.cpp`, `Projects/Starforge/src/StarforgeApp.cpp`
(`ImportModelFile`), `Projects/Starforge/src/EditorPrefs.h`, `Projects/ViperSim/src/SimHub.cpp`,
`Cosmic/templates/ExampleProject/src/TemplateProject.cpp`, `tests/test_filesystem_mounts.cpp`,
`test_assetlibrary.cpp`, `test_config.cpp`, `test_meshimport.cpp`, `test_filewatcher.cpp`
**API Reference:** [`../reference/assets-io.md`](../reference/assets-io.md) *(skeleton — D16
unwritten; this chapter is the client-facing source until it lands)* · **How it works:**
[`../systems/assets-vfs.md`](../systems/assets-vfs.md) *(skeleton — D32)*
**Configuration:** **both**, with one exception. `FileSystem`, `Config`, the whole `utils/` tier
and `AssetLibrary`'s texture / shader / material verbs ship on both engines. `AssetLibrary::GetMesh`,
`GetModel`, `GetAnimationClip`, `GetAnimationClipNames` and the entire `assets/MeshImport.h` header
are inside `#ifndef COSMIC_2D_ONLY` — a 2D build has the cache but not the 3D backends behind it.
See [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

Three things are stacked here and it pays to keep them apart:

| Layer | What it does | Fails how |
| --- | --- | --- |
| `FileSystem::Resolve` | turns `project://models/rover.glb` into a real path | never — an unknown scheme is returned unchanged |
| `AssetLibrary::Get*` | turns a path into a shared `Ref<>`, loading once | per type; see [the miss table](#what-a-miss-actually-does) |
| the loaders (`Texture2D::Create`, `Shader::Create`, `MeshImport::Import`) | turn bytes into a GPU object | per loader; conventions differ **on purpose** |

## Quick start

```cpp
#include <Cosmic.h>

void MyLayer::OnAttach()
{
    // 1. Mount your project. Everything project:// resolves against this.
    //    (The engine already set it from your DLL's stem before OnAttach ran —
    //    this call makes the name explicit and survives a rename.)
    Cosmic::FileSystem::SetActiveProject("MyGame");

    // 2. Load through the cache, not through the raw factories: the same path
    //    hands back the same Ref, and the file is uploaded to the GPU once.
    m_Atlas  = Cosmic::AssetLibrary::GetTexture("project://textures/atlas.png");
    m_Shader = Cosmic::AssetLibrary::GetShader("engine://shaders/Texture.glsl");

    // 3. Tunables live in TOML, not in code.
    if (Cosmic::Ref<Cosmic::Config> cfg = Cosmic::Config::Load("project://config/game.toml"))
        m_PlayerSpeed = cfg->Get<float>("player.speed_mps", m_PlayerSpeed);

    // 4. Anything you WRITE goes to user://. Never to project://, never to ".".
    Cosmic::DataExport::AppendRow(
        Cosmic::FileSystem::Resolve("user://mygame/session.csv"), { 0.0, 1.0 });
}
```

That is the whole contract in four calls: mount, read through the cache, configure from TOML,
write to `user://`.

## The three schemes

`FileSystem::Resolve` is a pure string transform. It does no I/O, never touches the disk, and
never fails — a path with no recognised scheme comes back byte-for-byte unchanged
(`FileSystem.cpp:52`). That is what makes it safe to call on an already-resolved path, which the
engine does constantly.

| Scheme | Resolves to | For |
| --- | --- | --- |
| `engine://x` | `assets/x` — **relative to the working directory** | engine-owned read-only content: shaders, the grid texture, default fonts |
| `project://x` | NAME mode: `assets/projects/<name>/x` · PATH mode: `<root>/[assets/]x` | your project's own content, read-only at runtime |
| `user://x` | `<user data root>/x` — see [below](#where-user-actually-lands) | **everything you write**: logs, prefs, saves, recordings, screenshots |
| *(anything else)* | returned unchanged | absolute paths, already-resolved paths, CWD-relative paths |

Two properties worth internalising:

- **The result is always forward-slashed.** `Resolve` builds through `std::filesystem::path` and
  returns `generic_string()`, so `engine://shaders\Foo.glsl` normalises on the way out. The result
  drops straight into `std::ifstream`, `Shader::Create` and `Texture2D::Create`.
- **`engine://` and NAME-mode `project://` are relative to the process working directory.**
  `Runtime/Main.cpp:28-31` forces the CWD to the exe's own directory on every boot, before
  anything else happens, so "relative to the CWD" means "relative to the exe" whether the app was
  double-clicked, launched from a terminal, or started from a shortcut. PATH-mode `project://` is
  absolute and does not care.

### Dev tree versus a shipped app

Same call, same string, three different disk locations — which is the entire point.

| Situation | `engine://shaders/PBR.glsl` | `project://scenes/Main.cscene` |
| --- | --- | --- |
| Dev tree, plugin app booted by the Launcher | `assets/shaders/PBR.glsl` under the build output dir | `assets/projects/MyGame/scenes/Main.cscene` |
| Dev tree, Starforge with an external project open | same | `C:/work/MyGame/[assets/]scenes/Main.cscene` (PATH mode) |
| Packaged `dist/MyGame` folder | `<dist>/assets/shaders/PBR.glsl` | `<dist>/assets/projects/MyGame/scenes/Main.cscene` |

Never hand-build these strings. `"assets/projects/" + name + "/..."` is correct today and wrong the
moment the project is opened from an absolute path.

## Mount `project://`: NAME mode versus PATH mode

`project://` has two mount modes and **the last setter wins**.

```cpp
// NAME mode — the in-tree layout shipped plugin apps use.
Cosmic::FileSystem::SetActiveProject("MyGame");
// -> project://scenes/Main.cscene  ==  assets/projects/MyGame/scenes/Main.cscene

// PATH mode — a self-contained project folder anywhere on disk.
Cosmic::FileSystem::SetActiveProjectPath("C:/work/MyGame");
// -> project://scenes/Main.cscene  ==  C:/work/MyGame/scenes/Main.cscene
//    ...or C:/work/MyGame/assets/scenes/Main.cscene if an assets/ subdir exists.
```

`SetActiveProjectPath` **probes once, at mount time**, for an `assets/` subdirectory under the
root (`FileSystem.cpp:130`). Present → `project://` resolves under `<root>/assets/`; absent →
under `<root>/` directly, which is the flat layout the Starforge project scaffold writes. Create
an `assets/` folder afterwards and nothing re-probes until the next mount.

`SetActiveProject` clears any absolute mount (`FileSystem.cpp:121`), so a legacy in-tree project
always resolves the legacy way even if a PATH-mode project was open a moment ago. All four
behaviours are pinned by `tests/test_filesystem_mounts.cpp`.

**The mount is process-wide.** The state lives in the engine DLL (`FileSystem.cpp`), and every
module — engine, editor, your game DLL — calls the same exported functions, so there is exactly
one active project per process. This was not always true: before the Phase 20 A1 fix the class was
header-only with `static inline` members, giving each DLL its own copy.

> **A stale rule you will find in the samples.** `TemplateProject.cpp:40-43`, `SimHub.cpp:30-31`,
> `Sound.h:16-20` and `LookupTable.h:95-96` all still teach "resolve `project://` in the CALLING
> DLL, because `FileSystem` has per-DLL static state". **The reason is obsolete.** The state moved
> into the engine DLL, so `Config::Load("project://config/x.toml")` from a plugin works directly.
> The practice is harmless — a resolved path passes through `Resolve` unchanged — so the samples
> are not broken, just over-cautious for a reason that no longer exists.

**Threading:** both setters are main-thread only, and no worker may call `Resolve` with a
`project://` path concurrently with a setter. There is no lock. Nothing in the engine loads assets
off-thread today; if that changes, this is the thing to guard.

**Ordering:** `Application::LoadProjectDLL` sets the active project from the DLL's file stem
*before* it calls your layer's `OnAttach` (`Application.cpp:739-740`), so `project://` already
works in `OnAttach`. Calling `SetActiveProject` yourself makes the name explicit and independent of
the DLL filename — which is why every shipped sample does it anyway.

## Where `user://` actually lands

`user://` is the only writable root. An installed app's own directory is under Program Files and is
read-only; anything that writes next to the exe works in the dev tree and fails on a user's
machine. Route every write here.

The root is decided **once, at first use**, and memoised in a function-local static
(`FileSystem.cpp:60`). The decision has two halves.

**Without an app identity (dev boots, the Launcher, `--project`)** — the historical shared root:

| Condition | `user://` root |
| --- | --- |
| working directory is writable (dev tree, unzipped folder) | `.` — user data sits next to the app |
| working directory is read-only | `%LOCALAPPDATA%/Cosmic/` |

**With an app identity set (a packaged boot — Phase 16 / S6)** — per-app isolation, so two shipped
Cosmic apps never share prefs, logs or recordings:

| Condition | `user://` root |
| --- | --- |
| a `portable.txt` sits next to the exe, **or** the exe dir is writable | `<exe>/user/` — *portable mode* |
| exe dir is read-only (installed under Program Files) | `%LOCALAPPDATA%/<AppName>/` |
| `LOCALAPPDATA` unset | `<system temp>/<AppName>/` |

Note the consequence: an app **unzipped to the desktop is portable by default**, because its exe
dir is writable. `portable.txt` only matters when the exe dir happens to be read-only and you want
the data local anyway.

### Which boots get an identity

This is the part that surprises people. `Runtime/Main.cpp:100-101` arms the identity **only** when
the startup project came from a `boot.cfg` next to the exe:

```cpp
if (fromBootCfg && !startupProject.empty())
    Cosmic::FileSystem::SetAppIdentity(startupProject);
```

So:

| Boot route | Identity | `user://` |
| --- | --- | --- |
| `MyGame.exe` with a `boot.cfg` (what the Starforge packager writes) | `MyGame` | isolated, per-app |
| `CosmicApp.exe --project MyGame` | *none* | the shared root |
| a dedicated exe built with `COSMIC_STARTUP_PROJECT` | *none* | the shared root |
| bare `CosmicApp.exe` → Launcher | *none* | the shared root |

A `package.bat <AppName>` dist boots via `--project` and therefore does **not** get per-app
isolation; only the `boot.cfg` path does. If you want isolation, ship a `boot.cfg`.

`SetAppIdentity` must run before the first `GetUserDataRoot()`, which is why it lives in `main()`
ahead of constructing `Application`. Setting it later has no effect — the static is already bound.

Every boot logs where it landed, right after the app is constructed
(`Runtime/Main.cpp:137`):

```
[COSMIC] user:// root -> C:/Users/you/AppData/Local/MyGame
```

### Logs belong in `user://`

The engine already does the right thing: `Application` initialises logging into
`FileSystem::Resolve("user://logs")` (`Application.cpp:92`).

**Do not copy the samples here.** `TemplateProject`, `Frontier` and `SF_Telem` all call
`Log::SetLogDirectory(Resolve("project://logs"))` on attach and `"logs"` (bare, CWD-relative) on
detach. Both fail once the app is installed — `project://` is read-only content and a bare relative
path lands wherever the CWD happens to be. Only `StarforgeApp` gets it right, and it is the pattern
to copy:

```cpp
void MyLayer::OnAttach()
{
    Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("user://logs"));
}
```

### Conventions already in use

Worth knowing before you invent your own layout — these are live and reserved:

| Path | Owner |
| --- | --- |
| `user://logs/` | the engine's log sink |
| `user://imgui.ini` | the ImGui docking layout (`ImGuiLayer.cpp:53`) |
| `user://branding/icon.png` | the per-user app-icon override (`utils/Branding.h`) |
| `user://starforge/…` | the editor: `projects.toml`, `editor.toml`, `layouts/`, `autosave/`, `takes/` |
| `user://recordings/…` | telemetry takes (ViperSim, SF_Telem) |

Namespace your own writes the same way — `user://mygame/…` — so a shared root stays legible.

## Load an asset through the cache

`AssetLibrary` is a process-wide, path-keyed cache. Ask for a path; a **hit** returns the already
loaded `Ref`, a **miss** loads it, stores it, and returns it. The point is that ten entities
referencing `project://textures/atlas.png` share one GPU upload.

```cpp
Cosmic::Ref<Cosmic::Texture2D>     tex  = Cosmic::AssetLibrary::GetTexture("project://textures/atlas.png");
Cosmic::Ref<Cosmic::Shader>        sh   = Cosmic::AssetLibrary::GetShader("engine://shaders/Texture.glsl");
Cosmic::Ref<Cosmic::Material>      mat  = Cosmic::AssetLibrary::GetMaterial("project://models/rover_body.cmat");
#ifndef COSMIC_2D_ONLY
Cosmic::Ref<Cosmic::Mesh>          mesh = Cosmic::AssetLibrary::GetMesh("project://models/rover.glb");
Cosmic::Ref<Cosmic::Model>         mdl  = Cosmic::AssetLibrary::GetModel("project://models/duck.glb");
Cosmic::Ref<Cosmic::AnimationClip> clip = Cosmic::AssetLibrary::GetAnimationClip("project://models/fox.glb#Run");
#endif
```

### Keys are normalised, so spellings collapse

The cache key is `FileSystem::Resolve(path)` run through `lexically_normal().generic_string()`
(`AssetLibrary.cpp:86-91`). Purely lexical — no disk I/O, so it works headless and is unit-tested
four ways in `tests/test_assetlibrary.cpp`. In practice:

```cpp
// All three are ONE cache slot:
AssetLibrary::GetTexture("engine://textures/grid.png");
AssetLibrary::GetTexture("assets/textures/grid.png");
AssetLibrary::GetTexture("assets/models/../textures/grid.png");
```

Backslashes normalise to forward slashes, `..` segments collapse, and `NormalizeKey` is idempotent
on its own output. It is public precisely so you can key your own side tables the same way.

### What a miss actually does

The shared miss path (`GetOrLoad`, `AssetLibrary.cpp:64-83`) logs
`AssetLibrary: failed to load '<path>'` and **does not cache a null**, so a later call retries.
Whether you ever reach that branch depends on the loader, and the loaders do not agree:

| Verb | Loader | On a missing/broken file | Cached? |
| --- | --- | --- | --- |
| `GetTexture` | `Texture2D::Create` | **non-null degraded object**: logs `Failed to load texture at …`, `GetWidth()`/`GetHeight()` are `0` | **yes** — the degraded object occupies the slot |
| `GetShader` | `Shader::Create` | `nullptr`, logged | no — retried next call |
| `GetMaterial` | `.cmat` → `BuildMaterial` | `nullptr` if the `.cmat` will not load or the PBR shader fails | no |
| `GetMesh` ³ᴰ | `MeshImport::Import` / `Mesh::CreateFromOBJ` | `nullptr`, logged | no |
| `GetModel` ³ᴰ | `Model::CreateFromGLTF` | `nullptr`, logged | no |
| `GetAnimationClip` ³ᴰ | clip-set parse | `nullptr` + a warn naming the clip and the file's clip count | the **set** is cached even when empty; a parse failure is not |

The texture row is the trap. Because `Texture2D::Create` never returns null under OpenGL, a texture
that failed to load is a perfectly good cache entry, and every subsequent `GetTexture` hands back
the same broken object. Fix the file, then call `AssetLibrary::Reload(path)` — nothing else evicts
it.

That also means a `.cmat` referencing a missing map is worse than it looks:
`BuildMaterial`'s `setMap` writes `u_HasAlbedoMap = 1.0f` whenever `GetTexture` returns non-null
(`AssetLibrary.cpp:177-182`), which for textures is *always*. A typo'd map path therefore tells the
shader the map exists and binds a zero-sized texture.

### Sampling: the pixel-art preset

```cpp
// Before any content loads — project open, or your layer's OnAttach.
Cosmic::AssetLibrary::SetDefaultTextureSampling(Cosmic::TextureFilter::Nearest,
                                                Cosmic::TextureWrap::ClampToEdge);
...
Cosmic::AssetLibrary::ClearDefaultTextureSampling();   // back to the loader default
```

This is process-wide and applies to every texture **loaded from this call on**. Already-cached
textures keep their sampling — set it before content loads, not after. Starforge drives it from the
`pixel_art` key in a project's `.cproj` manifest (`StarforgeApp.cpp:223`) and clears it when the
project closes.

### Hot reload

```cpp
if (Cosmic::AssetLibrary::Reload("project://textures/atlas.png"))
    CS_INFO("atlas refreshed");
```

`Reload` returns `true` when something was evicted. Textures are evicted **and eagerly re-loaded**
so the refreshed image is ready for the next `GetTexture`; every other type is just evicted and
reloads on demand. Reloading a model file also drops its `#N` sub-mesh entries and its cached clip
set, so a re-import refreshes every child of a multi-mesh source.

**The one thing `Reload` cannot do** is repoint `Ref`s you already handed out. A `Material` holding
the old `Ref<Texture2D>` keeps the old GPU handle forever. That is why Starforge's importer,
after `Reload`, walks the scene and clears `MeshRendererComponent::MeshAsset` /
`MaterialAsset` so the next frame's resolve picks the new one up
(`StarforgeApp.cpp:4228-4248`). Copy that shape if you reload at runtime.

### Introspection and lifetime

```cpp
uint64_t gpu = 0;
Cosmic::AssetLibrary::Enumerate([&](const Cosmic::AssetEntry& e)
{
    gpu += e.GpuBytes;
    CS_TRACE("{} [{}] refs={} gpu={}B", e.Path, (int)e.Type, e.Refs, e.GpuBytes);
});
```

`Enumerate` is read-only: no loads, no eviction, unspecified order. `Refs` is the `shared_ptr`
use count and **includes the library's own reference**, so an asset nothing else holds reports
`1`. `CpuBytes`/`GpuBytes` are estimates for a status bar — shaders and models report `0` because
neither is size-tracked (a `Model`'s bytes roll up in its separately-cached meshes and textures).

`AssetLibrary::Clear()` releases every cached `Ref` and **must run while a live GL context still
exists**, because the cached GPU resources delete their handles in their destructors. There is
exactly one call site: `Application::Shutdown` (`Application.cpp:428`). Nothing clears the cache
between project loads — keys are fully-resolved paths, so two projects never collide, but a long
editor session accumulates.

Everything in `AssetLibrary` is **main-thread only**, matching the factories and `Resolve`.

## Import a model

**3D only.** `assets/MeshImport.h` is fenced out of the 2D build.

Import turns a DCC/CAD file into engine geometry, and the thing it is really solving is **units**.
STL is unitless and everyone means millimetres; FBX is authored in centimetres; glTF and OBJ mean
metres. Get that wrong and your rover is 1000× too big. Cosmic never guesses silently: it writes
the assumption into a `.cmeta` sidecar the first time it sees a file, and re-reads it on every
import after that.

| Format | Backend | Always available? |
| --- | --- | --- |
| OBJ | the engine's own parser (merged path) / assimp (rich path) | yes |
| glTF, GLB | cgltf | yes |
| FBX, STL, DAE, PLY | vendored assimp | only with `COSMIC_WITH_ASSIMP` (**default ON**) |

```cpp
const std::string ext = Cosmic::MeshImport::Extension("Models/Rover.STL");   // -> "stl"
if (!Cosmic::MeshImport::Supports(ext))
    CS_WARN("this build cannot import .{} (assimp: {})", ext, Cosmic::MeshImport::AssimpEnabled());
```

### `.cmeta` sidecars

`MeshImport::LoadOrInitMeta(resolvedPath)` reads `<source>.cmeta` if present; if it is absent it
seeds one from the per-extension preset **and writes it out**, so the very first import is already
reproducible. The file is plain TOML:

```toml
# Cosmic mesh import settings (.cmeta) — edit + re-import to change.
[import]
source = "rover.stl"
scale = 0.001
up_axis = "Y"
flip_uvs = false
generate_normals = true
```

| Key | Meaning | Preset |
| --- | --- | --- |
| `scale` | source unit → metres | `0.001` for STL, `0.01` for FBX, `1.0` for everything else |
| `up_axis` | `"Y"` or `"Z"`; `Z` is rotated −90° about X into the engine's Y-up | `"Y"` |
| `flip_uvs` | flip V (some exporters ship it flipped) | `false` |
| `generate_normals` | synthesize normals when the source has none | `true` |

`source` is recorded for humans and read by nothing. Unknown or missing keys fall back per field to
the preset, so a partial hand-edited `.cmeta` is fine. Both directions round-trip in
`tests/test_meshimport.cpp`, and the presets are asserted there as "the CAD trap".

Edit the `scale`, re-import, and the change sticks — Starforge re-reads the `.cmeta` next to the
*copy* inside the project, not next to the original.

> `LoadOrInitMeta` writes next to the **resolved** source path. Import from a read-only folder and
> you get `MeshImport: could not write '<path>.cmeta'` and the preset is used un-persisted.

### Two products from two code paths

This catches people. `Mesh` and `Model` are different results of different pipelines:

| Call | Product | Formats |
| --- | --- | --- |
| `AssetLibrary::GetMesh(path)` | `Ref<Mesh>` — geometry with a submesh table | OBJ, glTF/GLB, and the assimp formats |
| `AssetLibrary::GetModel(path)` | `Ref<Model>` — parts, each with its own material | **glTF/GLB only** (`Model::CreateFromGLTF`) |

`GetMesh` is what `MeshRendererComponent::MeshPath` uses, and since A1 it routes glTF through the
importer too, so dropping a `.glb` onto a mesh slot works. `GetModel` is the dedicated per-part
glTF path. FBX/STL/DAE/PLY never produce a `Model`.

### Sub-mesh fragments

A multi-mesh source addresses its parts with a `#` fragment:

```cpp
const std::string part = Cosmic::MeshImport::SubmeshPath("project://models/gun.fbx", 2);
// -> "project://models/gun.fbx#2"
Cosmic::Ref<Cosmic::Mesh> barrel = Cosmic::AssetLibrary::GetMesh(part);
```

Each fragment is its own cache slot, but the file opened — and the `.cmeta` that governs it — is
the base path before the `#`. A path with no fragment keeps its exact pre-A1 meaning: the whole file
merged into one mesh.

Animation clips use the same `#` convention with a different vocabulary — `"…/fox.glb#Run"` by
name, `"…/fox.glb#1"` by index, bare path for the first clip. The whole file's clip set is parsed
and cached on the first request (CPU only, no GL), so switching clips inside one file is free.

### The rich import: what an editor does with a file

`MeshImport::ImportModelData` describes a whole source file without touching the GPU: every
sub-mesh (node transform baked), every material (factors plus texture references), every embedded
texture blob. The engine describes; the caller decides what to build. `StarforgeApp::ImportModelFile`
(`StarforgeApp.cpp:4157`) is the reference implementation, and it is worth knowing what it does
because it defines what "import" means in this engine:

1. **Copy the source into `project://models/`**, plus its sidecars — OBJ `mtllib` files and the
   `.bin` buffers a non-embedded `.gltf` needs. (`.glb` is self-contained.)
2. **`LoadOrInitMeta` on the copy**, so unit edits live with the project, not with the original.
3. **`ImportModelData`** to describe it.
4. **Generate one `.cmat` per referenced material**, staging its textures into
   `project://models/`: embedded blobs are written out (raw RGBA via `ImageIO::WritePNG`,
   compressed blobs verbatim), file references are copied from the original source directory —
   with a filename-only fallback for the absolute paths FBX exporters bake in. A texture that
   cannot be found is warned and skipped, not failed.
5. **Skip assimp's synthetic `DefaultMaterial`** so plain CAD parts keep the engine's default look.
6. **`Reload` the model and every generated `.cmat`**, then clear the resolved asset handles on
   any already-placed entity so the scene picks the change up.
7. **Spawn**: one entity for a single-mesh file; a parent plus one child per sub-mesh otherwise,
   recorded as a single undo step. A rigged source also gets an `AnimatorComponent` pointed at the
   file's first clip.

Two behaviours from that flow are worth carrying into your own tools: a re-import is just steps 1–6
again (nothing is keyed on a GUID), and **a file whose animations only move non-joint nodes yields
no clips at all** — both importer backends push a clip only when it has joint channels, so you get
`'X' contains no animation clips.` rather than an empty clip.

## Import a texture

There is no texture importer. Textures are copied into the project and loaded by path — the
Content Browser's drop handler and the importer's `StageImportedTexture` both just `copy_file` into
`project://models/` or `project://textures/` and reference the result as `project://…`.

`ImageIO` is the engine's decode/encode verb for the cases where you need pixels rather than a GPU
texture:

```cpp
// Decode any stb-supported format into RGBA8, TOP-LEFT origin.
int w = 0, h = 0;
std::vector<uint8_t> rgba;
if (Cosmic::ImageIO::ReadPixels(Cosmic::FileSystem::Resolve("project://branding/logo.png"), w, h, rgba))
{
    std::vector<uint8_t> small(64 * 64 * 4);
    Cosmic::ImageIO::ResizeRgba(rgba.data(), w, h, small.data(), 64, 64);
    Cosmic::ImageIO::WritePNG(Cosmic::FileSystem::Resolve("user://mygame/logo_small.png"),
                              64, 64, 4, small.data());
}
```

**The origin convention is the thing to get right.** `ImageIO::ReadPixels` forces stb's
flip-on-load **off** and returns top-left-origin rows — the OS icon convention. The GL texture
loader leaves stb's flip globally **on**. So `ReadPixels` and `Texture2D::Create` disagree about
row order by design; `ReadPixels` is for icons, thumbnails and CPU work, not for feeding the
renderer. `WritePNG` also expects top-left origin, so `ReadPixels → ResizeRgba → WritePNG` is
consistent end to end. `ResizeRgba` box-averages when shrinking and bilinearly interpolates when
enlarging, and is pure CPU — headless-testable.

The one convention you get for free is the app icon: drop a PNG at `<exe>/branding/icon.png` and
every Cosmic host picks it up, with `user://branding/icon.png` as a per-user override and the
project manifest's `icon` key after that (`utils/Branding.h` documents the five-step order).
Replacing the file on disk re-brands a running app with no code changes.

## Configure with TOML

`Config` is a read-only facade over the vendored toml++, with the parser kept entirely inside
`Config.cpp` so no toml type escapes the header.

```cpp
Cosmic::Ref<Cosmic::Config> cfg = Cosmic::Config::Load("project://config/vehicle.toml");
if (!cfg)
    return;   // not configured — defaults hold

const float     mass    = cfg->Get<float>("airframe.auw_kg", 1.5f);
const glm::vec3 inertia = cfg->Get<glm::vec3>("airframe.inertia_diag", { 1, 1, 1 });
const int       count   = cfg->Get<int>("motors.count", 4);

for (const Cosmic::Ref<Cosmic::Config>& motor : cfg->GetTable("motors"))
    const float kf = motor->Get<float>("kf", 0.0f);
```

- **Paths go through `Resolve`**, so `project://`, `engine://` and `user://` all work.
- **Keys are dotted paths**, and array indices work: `"motors[1].kf"`.
- **`Get<T>` supports** `float`, `double`, `bool`, `int`, `int64_t`, `uint32_t`, `std::string`,
  `glm::vec2/3/4`. Anything else is a `static_assert`. `GetFloatArray` reads a numeric array of any
  length; `GetTable` reads an array-of-tables (`[[motors]]`) — and a plain table at the key returns
  a one-element vector, so both shapes read the same way.
- **Table views share ownership of the parsed document**, so a `Ref<Config>` from `GetTable`
  outlives its parent. `tests/test_config.cpp` pins that explicitly.
- **Ints coerce to float getters.** Writing `tau = 1` for a float parameter gives you `1.0f`, not
  the fallback.
- **`Config::Parse(text, name)`** parses from memory — the path `.cmeta` uses, and the one for
  generated or unit-tested config.

**Failure behaviour, precisely:**

| Situation | Result | Log |
| --- | --- | --- |
| file does not exist | `nullptr` | `CS_CORE_TRACE` — *not* an error. An absent optional config is a normal outcome |
| file exists but is malformed | `nullptr` | `CS_CORE_ERROR` with description, line and column |
| key missing | the fallback | **nothing** |
| key present but wrong type | the fallback | **nothing** |

> `Config.h:64-65` claims a type mismatch "logs a warning … every time; fix your file". **It does
> not.** No getter in `Config.cpp` logs anything. A `speed = "fast"` where a float was expected is
> silently the fallback — check with `Has()` if you need to tell "absent" from "wrong".

The shipped pattern is the ViperSim one (`SimHub::LoadConfig`): read every value with the current
value as its own fallback, so a missing key and a missing file both degrade to the built-in design
point, and the app never has a hard dependency on the file.

```cpp
BodyParams p;                                   // built-in defaults
if (m_Config)
{
    auto& c = *m_Config;
    p.mass_kg = c.Get<float>("airframe.auw_kg",   p.mass_kg);
    p.inertia = c.Get<glm::vec3>("airframe.inertia_diag", p.inertia);
}
```

`GetSource()` returns the path a config came from (`"<string>"` for `Parse`) — worth logging next
to any value you read, because "which file did that number come from" is the question you will
actually have.

## Pick, watch, and export

### `FileDialog` — native open/save/folder

```cpp
Cosmic::FileDialogDesc dlg;
dlg.Title      = "Import model";
dlg.Filters    = { { "3D models", "*.obj;*.fbx;*.gltf;*.glb;*.stl" }, { "All files", "*.*" } };
dlg.InitialDir = "project://models";           // VFS or absolute

if (auto picked = Cosmic::FileDialog::Open(dlg))
    ImportModelFile(*picked);                  // absolute path
```

- Returns `std::optional<std::string>`; **an absolute path**, or `nullopt`. `nullopt` covers both
  cancel and error — they are indistinguishable to the caller.
- `Save(desc)` offers overwrite confirmation and honours `desc.DefaultExtension` (no dot).
  `PickFolder(title, initialDir)` picks a directory.
- `InitialDir` is resolved **only if it contains `://`**; anything else is passed through as-is.
- **Modal, on the main thread.** Call it from a UI event context — a button handler — never from a
  render callback.
- The caller decides what to do with the picked file. Starforge's importer copies it into the
  project; asset-slot "…" buttons translate a path already under the project root into its
  `project://` form and offer to copy it in otherwise.
- Non-Windows builds log `FileDialog: not implemented on this platform.` and return `nullopt`.

There is one failure mode worth naming, because it is the reason a whole gotcha exists: if the
calling thread is in the COM **multithreaded** apartment, an `IFileDialog` modal deadlocks.
`FileDialog` detects it and refuses loudly instead of hanging (`FileDialog.cpp:69-77`):

```
FileDialog::Open: calling thread is in the COM multithreaded apartment (MTA) — a modal
IFileDialog would deadlock here. Keep the UI thread STA (check MA_COINIT_VALUE / any
CoInitializeEx before this call).
```

If you ever see that, something initialised COM as MTA on the main thread before the engine did.
The full story is in [`audio.md`](audio.md#the-com-apartment-gotcha) — it was the audio subsystem.

### `FileWatcher` — react to files changing on disk

```cpp
Cosmic::FileWatcher m_Watcher;

void MyLayer::OnAttach()
{
    m_Watcher.Watch(Cosmic::FileSystem::Resolve("project://"));   // recursive by default
}

void MyLayer::OnUpdate(float ts)
{
    for (const Cosmic::FileChange& c : m_Watcher.Poll())          // main thread, per frame
        if (c.Kind == Cosmic::FileChangeKind::Modified)
            Cosmic::AssetLibrary::Reload("project://" + c.Path);
}
```

A background thread runs the platform watch (`ReadDirectoryChangesW`) and pushes into a
mutex-guarded queue; `Poll()` drains it from the main thread and returns empty when idle. `Path` is
**relative to the watched root and forward-slashed**, which is what makes the `"project://" + c.Path`
concatenation above correct. `Renamed` reports the new name in `Path` and the old one in `OldPath`.

`Watch` returns `false` and leaves `IsWatching()` false when the directory cannot be opened or the
platform has no watcher — off Windows it is a no-op, so callers still compile. `Stop()` joins the
worker and is safe to call repeatedly, including on a watcher that never started. All three
behaviours are covered in `tests/test_filewatcher.cpp`.

### `DataExport` — CSV in and out

```cpp
// Bulk: everything already in memory.
Cosmic::DataExport::WriteCSV(Cosmic::FileSystem::Resolve("user://mygame/run.csv"),
                             { "t", "alt_m", "v_mps" }, { times, alts, speeds });

// Streaming: one row per step. Opens, writes and closes every call.
Cosmic::DataExport::AppendRow(path, { t, alt, v });

// Read back: the transpose of WriteCSV.
std::vector<std::vector<double>> cols;
std::vector<std::string>         headers;
if (Cosmic::DataExport::LoadCSV(path, cols, &headers)) { /* ... */ }
```

`WriteCircularBuffer` writes ImPlot-style ring buffers in chronological order without asking you to
linearise them first — pass the `count`, `offset` and `capacity` you already track.

Two contracts to note. **All four verbs create the parent directory** if it is missing
(`DataExport.cpp:14-20`). And **none of them resolve VFS paths** — `DataExport` is one of the few
path-taking APIs in the engine that does not call `Resolve`, so wrap every path yourself. The
header says this for `LoadCSV`; it is true for all four.

`AppendRow` reopens the file on every call, which is safe but not fast. For sub-millisecond logging,
buffer rows and call `WriteCSV` at the end of the run — or use the telemetry recorder, which is
built for exactly that (see [`serial-and-telemetry.md`](serial-and-telemetry.md)).

`LoadCSV` treats the first row as a header if any cell in it is non-numeric. Ragged rows and
non-numeric cells after the header are rejected with a logged error, not silently skipped.

## Common patterns

**Resolve once, store the resolved path.** Anything you hand to a non-VFS API (`std::ifstream`,
`Shader::Create`, `Texture2D::Create`, `DataExport`, `SceneManager`) needs a resolved path.
`Resolve` on an already-resolved path is a no-op, so resolving early is free and resolving late is
a bug you find on someone else's machine.

**Read through `AssetLibrary`, not through the factories.** `Texture2D::Create("project://…")` does
not resolve VFS paths — neither does `Shader::Create`. Both take a real disk path. `AssetLibrary`
resolves *and* caches, which is why it is the recommended door.

**Write only to `user://`.** Two lines to remember: `Log::SetLogDirectory(Resolve("user://logs"))`,
and every recording/save/export path starts `user://<yourapp>/`.

**Store `project://` paths in scenes, never resolved ones.** This is what makes a project
relocatable. `SceneSerializer` writes what you put in the component, so a baked absolute path
follows the scene to another machine and breaks there. The prefab path has a live instance of this
hazard: `InstantiatePrefab` stamps the **resolved disk path** into `PrefabComponent::SourcePath`,
and Starforge immediately overwrites it with the VFS path (`Prefabs.h`). Code that instantiates
prefabs directly must do the same.

**Ship a `boot.cfg` if you want per-app user data.** It is the only route that arms
`SetAppIdentity`.

**Let a missing config be normal.** `Config::Load` returning `nullptr` logs at trace level for
exactly this reason — probe optional files freely.

## Pitfalls

**"My texture is invisible / black and the log has one error from startup."**
`Texture2D::Create` never returns null, so `AssetLibrary` cached the degraded object. Every later
`GetTexture` returns it. Fix the path, then `AssetLibrary::Reload(path)` — reloading the *scene*
will not do it.

**"The material's albedo map is missing but the shader thinks it has one."**
Same cause. `BuildMaterial` sets `u_HasAlbedoMap = 1` whenever `GetTexture` returns non-null.
Check the startup log for `Failed to load texture at …`.

**"`project://` resolves to the wrong project after opening another one."**
`SetActiveProject` and `SetActiveProjectPath` overwrite each other — last one wins, and
`SetActiveProject` additionally clears the absolute mount. There is one active project per
*process*, not per DLL.

**"It works in the dev tree and writes nothing once installed."**
Something is writing to `project://` or to a bare relative path. Both are read-only or
CWD-dependent on a real install. Three of the four shipped samples make this mistake with their log
directory — do not copy them.

**"Two of my shipped apps overwrite each other's settings."**
Neither has an app identity. `--project` and `COSMIC_STARTUP_PROJECT` boots share the root; only a
`boot.cfg` boot isolates.

**"`portable.txt` did nothing."**
It only matters when the exe directory is read-only. A writable exe dir is already portable, and a
boot with no identity ignores the flag entirely.

**"`SetAppIdentity` had no effect."**
The user root is memoised on first use. Something resolved `user://` before you called it — set the
identity in `main()`, before `Application` is constructed.

**"My model imported at 1000× the right size."**
The `.cmeta` `scale` is wrong for that source. STL presets to `0.001`, FBX to `0.01`, everything
else to `1.0`. Edit the sidecar and re-import; the edit survives re-imports.

**"The importer says my rigged file has no animation clips."**
Both backends discard clips with no joint-targeting channels. A file whose animations move
non-joint nodes produces zero clips, not empty ones.

**"`GetModel` returns null for my FBX."**
`Model` is glTF-only. Use `GetMesh`, which routes every supported format through the importer.

**"A `Config` value silently reverted to its default."**
Wrong TOML type — a quoted number, an array where a scalar was expected. Nothing is logged. Use
`Has()` to distinguish absent from mistyped.

**"`FileDialog` returned nullopt and I can't tell why."**
Cancel and error look identical. Check the log: a genuine failure logs
`could not create IFileOpenDialog` or the MTA guard message.

**"The file watcher fires three times for one save."**
Editors write-then-rename, and `ReadDirectoryChangesW` reports each step. Debounce, or make your
reload idempotent — `AssetLibrary::Reload` is.

**"`Enumerate` says an asset has 1 reference but I'm holding it."**
`Refs` includes the library's own reference. Your handle makes it 2.

## See also

- [`materials-and-shaders.md`](materials-and-shaders.md) — the `.cmat` format, the shader contract,
  and what `BuildMaterial` binds
- [`rendering-3d.md`](rendering-3d.md) — `Mesh` vs `Model` on the drawing side, and material
  read-at-flush
- [`animation.md`](animation.md) — what a clip is once `GetAnimationClip` hands you one
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — why scene files must hold
  `project://` paths, and `SceneManager`'s missing `Resolve`
- [`logging-and-diagnostics.md`](logging-and-diagnostics.md) — `Log::SetLogDirectory` and where the
  log file actually is
- [`sim-math-toolkit.md`](sim-math-toolkit.md) — `LookupTable1D::FromCSV`, the other consumer of
  `DataExport`
- [`audio.md`](audio.md#the-com-apartment-gotcha) — why `FileDialog` has an MTA guard
- [root README §1.6](../../README.md#16-the-two-engine-configurations) and
  [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what a 2D build drops
- [`../reference/assets-io.md`](../reference/assets-io.md) *(skeleton)* ·
  [`../systems/assets-vfs.md`](../systems/assets-vfs.md) *(skeleton)*
