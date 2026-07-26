# Audio — Guide

**What this covers:** loading a `Sound`, firing one-shots, holding a loop and steering its volume
and pitch live, the four mixing groups and how pausing works, what happens on a machine with no
audio device — and **the COM apartment gotcha**, which is the single most important thing on this
page even though it has nothing to do with sound.
**Source of truth:** `Cosmic/src/audio/AudioEngine.h`, `audio/Sound.h`, `audio/Audio.cpp`,
`audio/MiniaudioImpl.cpp`, `core/Application.cpp` (`Initialize`/`Shutdown`),
`utils/FileDialog.cpp` (`GuardApartment`), `Projects/ViperSim/src/SimHub.cpp`,
`Projects/Frontier/src/common/DistanceLoop.h`, `common/ProceduralAudio.{h,cpp}`,
`Projects/Starforge/src/panels/ContentBrowserPanel.cpp`,
`Cosmic/templates/ExampleProject/src/TemplateProject.cpp`, `tests/test_audio.cpp`,
`tests/test_assetlibrary.cpp`
**API Reference:** [`../reference/audio.md`](../reference/audio.md) *(skeleton — D16 unwritten;
this chapter is the client-facing source until it lands)* · **How it works:**
[`../systems/audio.md`](../systems/audio.md) *(skeleton — D32)*
**Configuration:** **both.** `audio/` is unfenced in `Cosmic.h` and compiles identically on the 2D
and 3D engines.

The engine ships **verbs**, not meaning. `Play`, `PlayLooping`, `SetVolume`, `SetPitch`,
`SetGroupVolume` — that is the whole surface. What a sound *means* (low-battery warning, mode
chime, motor hum, distance-attenuated rumble) is your app's job, and every shipped sample builds
its own thin helper on top rather than pushing domain logic into the engine.

Underneath is one vendored miniaudio `ma_engine` on the WASAPI backend. No miniaudio type appears
in any public header.

## Quick start

```cpp
#include <Cosmic.h>

void MyLayer::OnAttach()
{
    // Load once, play many times. Never returns nullptr.
    m_Click = Cosmic::Sound::Create("project://sounds/click.wav");
    m_Hum   = Cosmic::Sound::Create("project://sounds/motor.wav");

    // A held loop, started muted, with a handle for live control.
    m_HumVoice = Cosmic::AudioEngine::PlayLooping(m_Hum, 0.0f, 1.0f, Cosmic::AudioGroup::Sfx);
}

void MyLayer::OnUpdate(float ts)
{
    Cosmic::AudioEngine::SetPitch (m_HumVoice, m_Rpm / m_RpmNominal);
    Cosmic::AudioEngine::SetVolume(m_HumVoice, m_Throttle);
}

void MyLayer::OnDetach()
{
    Cosmic::AudioEngine::Stop(m_HumVoice);
}
```

Members: `Cosmic::Ref<Cosmic::Sound> m_Click, m_Hum;` and
`Cosmic::SoundHandle m_HumVoice = Cosmic::InvalidSoundHandle;`.

You never call `AudioEngine::Init()` or `Shutdown()`. `Application::Initialize` starts audio right
after the job system — **before the window is created** — and `Application::Shutdown` tears it down
before window teardown.

## Load a sound

```cpp
Cosmic::Ref<Cosmic::Sound> s = Cosmic::Sound::Create("project://sounds/click.wav");
```

`Create` decodes the whole file into memory (`MA_SOUND_FLAG_DECODE`) and keeps it as a template
voice that is never started. Playback copies that template, which is why one `Sound` drives many
overlapping voices cheaply.

- **The path goes through `FileSystem::Resolve`**, so `project://`, `engine://` and `user://` all
  work. (`Sound.h`'s note telling you to resolve in the calling DLL first is stale — see
  [`assets-and-vfs.md`](assets-and-vfs.md#mount-project-name-mode-versus-path-mode). Doing it
  anyway is harmless.)
- **`Create` never returns `nullptr`.** A missing or unreadable file logs `CS_CORE_ERROR` and
  returns a **degraded silent object**. `IsValid()` tells you which one you got; `GetDuration()`
  is `0.0f` on a degraded sound. This is the same policy as `Texture2D::Create`, and it means call
  sites do not need null branches.
- **A sound created when audio is unavailable is also degraded** — headless CI, an RDP session, or
  simply before `Init` / after `Shutdown`. `tests/test_audio.cpp` pins all three cases.

**Formats: WAV, FLAC and MP3.** Those are miniaudio's built-in decoders (dr_wav, dr_flac, dr_mp3).
**Ogg Vorbis is not compiled in** — miniaudio gates Vorbis behind `STB_VORBIS_INCLUDE_STB_VORBIS_H`
and `MiniaudioImpl.cpp` does not include stb_vorbis. Note that Starforge's Content Browser *does*
list `.ogg` in its audio row (`AssetTypes.cpp`), so an `.ogg` gets a proper audio tile and a
preview button — and then `Sound::Create` fails on it and you get silence plus one error line.
Convert to WAV or FLAC.

`Sound` is move-only in spirit — copy and assignment are deleted — and is always held by `Ref`.

### Waveform previews

```cpp
std::vector<float> envelope;
const size_t n = sound->CopyPcm(envelope, 512);   // 512 peak-decimated samples, [-1, 1]
```

`CopyPcm` peak-decimates the **whole** file into at most `maxSamples` buckets (each bucket holds
the largest-magnitude sample in its span), so a five-minute track previews as cheaply as a
click. It decodes through a standalone decoder, so it is **device-independent**: it works in a
headless session and never disturbs live playback. Returns `0` for a missing file or
`maxSamples == 0`. This is what the Content Browser's audio preview draws.

## Fire a one-shot

```cpp
Cosmic::AudioEngine::Play(m_Chime, 0.8f, 1.0f, Cosmic::AudioGroup::Ui);
```

Fire-and-forget. `volume` is a linear multiplier, `pitch` is a **playback-rate** multiplier
(`1.0f` = as recorded; a non-positive value is coerced to `1.0f`). The default group is `Sfx`.

`Play` returns `void` — there is deliberately no handle, so a one-shot cannot be stopped or
retuned. If you need that, use `PlayLooping` and stop it yourself. Playing a null or degraded
`Ref<Sound>` is a silent no-op; the error was already logged at `Create` time, and the template
project demonstrates that on purpose by loading a file it knows is missing.

ViperSim's alert layer is the canonical shape — three sounds, three severities, two groups:

```cpp
Cosmic::AudioEngine::Play(m_SndChime,    0.8f, 1.0f, Cosmic::AudioGroup::Ui);
Cosmic::AudioEngine::Play(m_SndWarning,  1.0f, 1.0f, Cosmic::AudioGroup::Alerts);
Cosmic::AudioEngine::Play(m_SndCritical, 1.0f, 1.0f, Cosmic::AudioGroup::Alerts);
```

Routing alerts to their own group is what later lets a user turn ambience down without losing
warnings.

## Hold a loop and steer it

```cpp
Cosmic::SoundHandle h = Cosmic::AudioEngine::PlayLooping(m_Wind, 0.0f, 1.0f,
                                                          Cosmic::AudioGroup::Sfx);
...
Cosmic::AudioEngine::SetVolume(h, gust);
Cosmic::AudioEngine::SetPitch (h, 1.0f + gust * 0.2f);
if (Cosmic::AudioEngine::IsPlaying(h)) { /* ... */ }
...
Cosmic::AudioEngine::Stop(h);
```

`SoundHandle` is a `uint32_t`. **`0` (`InvalidSoundHandle`) is never a live voice**, and handles
are never reused within a session, so a stale handle is inert rather than dangerous — every control
verb on an unknown handle is a harmless no-op and `IsPlaying` returns `false`.

`PlayLooping` returns `InvalidSoundHandle` when audio is unavailable, when the sound is null or
degraded, or when the voice copy fails. **That is the idiom to lean on**: start the voice, keep the
handle, and let every subsequent call no-op if it came back invalid. Frontier's `DistanceLoop`
(`Projects/Frontier/src/common/DistanceLoop.h`) is a 60-line class built entirely on that property
— it never branches on "is audio working", only on "is my handle valid":

```cpp
void Start(const Cosmic::Ref<Cosmic::Sound>& sound,
           Cosmic::AudioGroup group = Cosmic::AudioGroup::Sfx)
{
    if (m_Handle != Cosmic::InvalidSoundHandle)
        return;                                      // idempotent
    m_Handle = Cosmic::AudioEngine::PlayLooping(sound, 0.0f, 1.0f, group);
}

void Update(const glm::vec3& listener, const glm::vec3& source, float radius, float maxVol)
{
    if (m_Handle == Cosmic::InvalidSoundHandle)
        return;
    const float dist = glm::length(listener - source);
    const float t    = radius > 1e-3f ? std::clamp(1.0f - dist / radius, 0.0f, 1.0f) : 0.0f;
    Cosmic::AudioEngine::SetVolume(m_Handle, maxVol * t * t);   // quadratic falloff
}
```

Starting a loop at volume `0` and letting the first `Update` set its real level avoids a one-frame
blast at full volume — worth copying.

`StopAll()` stops every live voice, loops included. Loaded `Sound`s stay valid, so this is the
right call on a scene change or a "stop everything" panic button.

## Groups, volume, and pausing

```cpp
enum class AudioGroup : int { Master = 0, Sfx, Ui, Alerts };
```

`Master` is the engine endpoint itself; `Sfx`, `Ui` and `Alerts` are real mixing groups routed
through it. There are exactly these four, and the set is not extensible from client code.

```cpp
Cosmic::AudioEngine::SetMasterVolume(0.8f);                            // [0, 1+]
Cosmic::AudioEngine::SetGroupVolume(Cosmic::AudioGroup::Sfx, 0.5f);
float ui = Cosmic::AudioEngine::GetGroupVolume(Cosmic::AudioGroup::Ui);
```

`SetGroupVolume(AudioGroup::Master, v)` and `SetMasterVolume(v)` are the same call. Volumes are
linear multipliers and are **not clamped** — values above `1.0f` are allowed and will clip.

**Pausing is opt-in and has nothing to do with `TimeScale`.** Sounds keep playing when the timeline
is paused or `TimeScale` is `0`; if you want audio to stop with the sim, pause a group explicitly:

```cpp
Cosmic::AudioEngine::PauseGroup(Cosmic::AudioGroup::Sfx, true);        // halts its voices
...
Cosmic::AudioEngine::PauseGroup(Cosmic::AudioGroup::Sfx, false);       // resumes where they stopped
```

Pausing `Master` stops the whole device graph, not just a bus. `IsGroupPaused` reads a cached flag
that only these calls set, so it reflects what you asked for rather than what the device is doing.

The usual layout: ambience and world sound on `Sfx` so a user can turn it down; UI feedback on `Ui`
so it stays audible; anything safety-relevant on `Alerts` so it survives both.

## Headless and device-less behaviour

This is a design property, not a fallback. Cosmic runs on CI runners and over RDP, and audio must
never crash or block startup there.

If `ma_engine_init` fails, `AudioEngine::Init` logs

```
AudioEngine: device init failed (miniaudio result -N) — audio disabled for this session.
```

and **the entire subsystem becomes a no-op**. Concretely:

| Call | With no device |
| --- | --- |
| `IsInitialized()` | `false` |
| `Sound::Create(path)` | non-null degraded object, `IsValid() == false`, one warn line |
| `Play(...)` | no-op |
| `PlayLooping(...)` | `InvalidSoundHandle` |
| `Stop` / `SetVolume` / `SetPitch` / `StopAll` / `PauseGroup` | no-op |
| `IsPlaying(h)` | `false` |
| `GetMasterVolume()` / `GetGroupVolume(g)` | **`0.0f`**, not the last value you set |
| `Sound::CopyPcm(...)` | **still works** — standalone decoder, no device needed |
| `Shutdown()`, called twice | safe |

That `0.0f` is the one asymmetry worth knowing: a settings panel that reads
`GetMasterVolume()` on a device-less machine shows a zeroed slider, and writing that back does
nothing. `TemplateProject` guards its whole audio panel with `if (!AudioEngine::IsInitialized())`
for exactly this reason.

Because nothing branches at the call site, your gameplay code does not need `#ifdef`s or null
checks. `tests/test_audio.cpp` walks the entire control surface with no device and asserts it is
harmless.

## The COM apartment gotcha

> **Read this before you touch audio initialisation.** The audio subsystem sets a COM apartment
> mode, and it is doing so on purpose to keep *file dialogs* working. Changing it breaks every
> native dialog in the app in a way that looks nothing like an audio bug.

`Cosmic/src/audio/MiniaudioImpl.cpp` opens with:

```cpp
#ifdef _WIN32
#define MA_COINIT_VALUE 0x6   // COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
```

**Why.** `ma_context_init` calls `CoInitializeEx` **on the calling thread**, and miniaudio's default
`MA_COINIT_VALUE` is `COINIT_MULTITHREADED`. The calling thread here is
`Application::Initialize` → `AudioEngine::Init` — the **main thread, at boot, before the window
exists**. A thread's COM apartment is first-call-wins and cannot be changed afterwards. So the
default silently dropped the UI thread into the **MTA**.

**What broke.** `IFileDialog` is an STA object. Created from an MTA thread it lands on a COM
host-STA thread, and the marshalled `Show()` call `SendMessage`s to its owner window — whose thread
is the main thread, which is sitting blocked inside that very `Show()` call and can no longer pump
messages. Deadlock. Not the audio: the *dialog*. Every native modal in the app — the telemetry
replay **Browse** button, project open, model import, save-as — froze the application solid.

**Why it survived so long.** The regression is completely silent from the audio side: audio kept
working perfectly. The only symptom was a frozen UI on a button that had nothing to do with sound,
so it lived from 7/02 until it was tracked down about two weeks later.

**Why STA is right.** WASAPI is apartment-agnostic — miniaudio's device thread `CoInitialize`s
itself — so the audio backend does not care which apartment the *main* thread is in. The thread
that owns windows and shows modal dialogs does care, and for that thread STA is the only correct
model. `0x2 | 0x4` is written numerically because the engine builds with `WIN32_LEAN_AND_MEAN`,
which strips `objbase.h` and its named constants out of `windows.h`.

**Two tripwires now guard it.**

1. `tests/test_audio.cpp`, *"AudioEngine::Init leaves the calling thread in the STA (file-dialog
   safety)"* — calls `AudioEngine::Init()` on doctest's main thread and then asks COM what
   apartment it ended up in:

   ```cpp
   const long hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
   CHECK(hr != RPC_E_CHANGED_MODE);   // "this thread is already in the OTHER apartment"
   ```

   That is precisely the question `IFileDialog` asks. Revert `MA_COINIT_VALUE` and this test fails.

2. `FileDialog` itself refuses rather than hangs (`utils/FileDialog.cpp:69-77`). If it ever finds
   the calling thread in the MTA it logs and returns `nullopt`:

   ```
   FileDialog::Open: calling thread is in the COM multithreaded apartment (MTA) — a modal
   IFileDialog would deadlock here. Keep the UI thread STA (check MA_COINIT_VALUE / any
   CoInitializeEx before this call).
   ```

**The general rule this leaves you with:** nothing may put the main thread in the MTA. If you add a
library that calls `CoInitializeEx` during startup — an ASIO backend, a video decoder, a device SDK
— check its apartment default before you wire it into `Application::Initialize`, and add a
tripwire like the one above.

## Common patterns

**Load in `OnAttach`, hold `Ref<Sound>` as members.** Decoding happens once at `Create`; playback is
a cheap copy. Loading inside `OnUpdate` decodes a file every frame.

**Keep the handle, not the state.** `PlayLooping` → store the `SoundHandle` → `SetVolume`/`SetPitch`
each frame → `Stop` on teardown. Do not track "am I playing" yourself; `InvalidSoundHandle` and
`IsPlaying` already say.

**Own audio meaning in your app, not in the engine.** Both Frontier helpers are the shape to copy:
`DistanceLoop` for attenuation, `ProceduralAudio` for content. The latter is worth stealing wholesale
if you want atmosphere without committing audio assets — it prefers a user-dropped
`project://sounds/<name>.wav`, and otherwise synthesises the recipe once into
`user://frontier/audio/<name>.wav` and loads that. Note both halves of the VFS rule there:
authored content reads from `project://`, generated content is written to `user://`.

**Route by intent, not by sound.** `Ui` for feedback, `Sfx` for the world, `Alerts` for things a
user must hear. It costs nothing now and is the only way to offer a usable volume panel later.

**Guard settings UI on `IsInitialized()`.** Not playback — playback already no-ops — but any widget
that *reads* a volume back, because the getters return `0.0f` with no device.

**Stop what you start.** A `Ref<Sound>` going out of scope does not stop voices playing from it.
`~Sound` uninitialises the template, not the copies. Call `Stop(handle)` (or `StopAll()`) in
`OnDetach` — `DistanceLoop` does it in its destructor.

## Pitfalls

**"Every native dialog in the app freezes."**
Something put the main thread in the COM MTA. Check `MA_COINIT_VALUE` in
`audio/MiniaudioImpl.cpp` and any `CoInitializeEx` that runs before it. See
[the gotcha](#the-com-apartment-gotcha).

**"My `.ogg` is silent and there is one error at load."**
Vorbis is not compiled into miniaudio. The Content Browser lists `.ogg` as audio anyway. Convert
to WAV or FLAC.

**"No sound, no crash, and nothing obviously wrong."**
Check `AudioEngine::IsInitialized()`. On CI, over RDP, or with no output device the whole
subsystem is a no-op by design and only logs one warning at boot.

**"`Sound::Create` returned an object but nothing plays."**
`IsValid()` is `false` — a degraded sound. The real error was logged at `Create` time, not at
`Play` time, so look at startup, not at the moment of silence.

**"I want to stop a one-shot."**
You cannot: `Play` returns `void` by design. Use `PlayLooping` and `Stop` it, or `StopAll()`.

**"My volume slider reads 0 on a machine with no sound card."**
`GetMasterVolume()`/`GetGroupVolume()` return `0.0f` when uninitialised rather than the last value
set. Cache your own setting and only push it into the engine.

**"Audio keeps playing when I pause the game."**
That is the default. Pause a group explicitly — `TimeScale` does not touch audio.

**"Pitch is doing something odd at low values."**
`pitch` is a playback-rate multiplier and is coerced to `1.0f` if it is `<= 0`. It re-pitches by
resampling, so a hum at `0.25f` is also four times longer.

**"Voices seem to linger after they finish."**
Finished one-shot voices are only swept when the *next* voice starts (`CollectFinishedVoices` runs
inside the shared start path). An app that fires a thousand one-shots and then goes quiet keeps
those `ma_sound` objects until something else plays or the engine shuts down. Harmless, but it
explains a memory profile that looks like a leak.

**"Handles from a previous session don't work."**
They shouldn't. Handles are per-session and monotonically increasing; nothing is reused and nothing
is persisted.

## See also

- [`assets-and-vfs.md`](assets-and-vfs.md) — where `project://sounds/…` resolves, why generated
  audio belongs in `user://`, and `FileDialog`'s MTA guard from the other side
- [`time-and-ticks.md`](time-and-ticks.md) — why `TimeScale` and audio pausing are unrelated
- [`project-anatomy.md`](project-anatomy.md) — the `Application` lifecycle that owns `Init`/`Shutdown`
- [`logging-and-diagnostics.md`](logging-and-diagnostics.md) — where the boot-time audio warning
  goes (Release has no console; the log file is the only output)
- [`../reference/audio.md`](../reference/audio.md) *(skeleton)* ·
  [`../systems/audio.md`](../systems/audio.md) *(skeleton)*
