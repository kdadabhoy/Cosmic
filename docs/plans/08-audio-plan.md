# Audio Plan — Engine Audio Capabilities

> **Goal:** give Cosmic a small, industry-standard audio subsystem: load sounds as `Ref<>` resources,
> play one-shots and loops, control volume/pitch, later positional audio. Same design rule as
> everything else: the engine ships generic verbs (`Play`, `SetVolume`); apps decide *what* sounds
> mean (low-battery warning, mode-change chime, motor hum).

## Technology choice: **miniaudio** (vendored single header)

- Public-domain/MIT-0 single header (`miniaudio.h`), no linker deps beyond what Windows provides
  (WASAPI backend). The de-facto standard for exactly this weight class.
- Handles device enumeration/loss, resampling, mixing, and decoding (WAV/FLAC/MP3 built in).
- Vendor at `Cosmic/dependencies/miniaudio/miniaudio.h`; one `.cpp` in the engine defines
  `MINIAUDIO_IMPLEMENTATION`.
- Rejected: FMOD/Wwise (licensing, massively over-scope), OpenAL-soft (extra DLL, aging API),
  raw WASAPI (weeks of plumbing miniaudio already does).

## Stage A1 — Core playback

| Piece | Contents |
| --- | --- |
| `Cosmic/src/audio/AudioEngine.h/.cpp` | `Init()/Shutdown()` (wired into `Application::Initialize/Shutdown`, init after JobSystem, shutdown before window teardown); wraps one `ma_engine`. `COSMIC_API`. |
| `Cosmic/src/audio/Sound.h/.cpp` | `Ref<Sound> Sound::Create(path)` (VFS-resolved — `project://sounds/...`); wraps `ma_sound`/decoded buffer; factory pattern identical to `Texture2D`. Missing file → degraded silent object + `CS_CORE_ERROR` (same policy as textures). |
| API | `AudioEngine::Play(sound)` fire-and-forget; `Play(sound, volume, pitch)`; `AudioEngine::SetMasterVolume(0..1)`. |

Acceptance: template project plays a click on a button press; missing-file path logs and stays silent.

## Stage A2 — Loops, handles, groups

- `SoundHandle AudioEngine::PlayLooping(sound, volume)` → `Stop(handle)`, `SetVolume(handle, v)`,
  `SetPitch(handle, p)` for live control (this is what an RPM-tracking motor loop needs).
- Sound groups/buses: `Master / SFX / UI / Alerts` with per-group volume (maps to `ma_sound_group`).
- Pause-awareness policy: sounds keep playing when `TimeScale == 0` by default; an app can opt a
  group into pause (`AudioEngine::PauseGroup`).

## Stage A3 — Positional & streaming (only when a consumer exists)

- 2D pan/attenuation from world position (`PlayAt(sound, vec3 pos)`) and a listener tied to the
  active camera — miniaudio's spatializer does the math; the engine just forwards listener pose.
  Works for both 2D (ortho cam pos) and 3D (perspective cam) via the S1 camera work.
- Streaming for long music/ambience (miniaudio streams natively — flag on `Sound::Create`).
- Doppler for the sim (stretch; miniaudio supports it).

## App usage (who consumes this)

- **SF_Telem:** connect/disconnect chirps, record start/stop, warning tone on ESC fault.
- **ViperSim (doc 04):** mode-change chimes, low-battery / lost-link / geofence alert tones
  (failsafe testing gets much more visceral), stall-warning, optional motor-hum loop with pitch from
  average rotor ω (A2), variometer beep in cruise (glider-style climb/sink audio) — genuinely useful
  for tuning by ear.
- **Template project:** one demo click so every new project sees the pattern.

## Sequencing

- A1 is independent of everything — slot it anywhere after the Phase 1 bugfix pass; a good
  lower-tier-AI task once miniaudio is vendored.
- A2 when ViperSim P3 (hover + failsafe tones) wants it.
- A3 only with a real consumer (motor hum / FPV immersion).

## Test note

`AudioEngine` must run headless-safe: if device init fails (CI runner, RDP session), log a warning
and become a no-op — never crash, never block startup. Unit tests cover the no-device path and
`Sound::Create` failure policy only (no audible assertions in CI).
