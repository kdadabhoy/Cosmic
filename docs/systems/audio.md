# Audio — How It Works

> **STATUS: SKELETON** — to be filled by work order **D32** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a thin engine facade over miniaudio — load a `Sound`, fire one-shots or
managed loops in named groups, and it all degrades safely to silence on machines with no
audio device (so tests and headless runs never crash).
**Source:** `Cosmic/src/audio/AudioEngine.h`, `audio/Sound.h`, `audio/Audio.cpp`, `audio/MiniaudioImpl.cpp`
**API Reference:** [../reference/audio.md](../reference/audio.md) · **Guide:** [../guide/audio.md](../guide/audio.md) (D58) · **Plan record:** [`../plans/archive/08-audio-plan.md`](../plans/archive/08-audio-plan.md)

## Section plan

1. **Overview** — what the engine adds over "just play a file" (lifetime, groups, volumes, device-loss safety). <!-- TODO(D32) -->
2. **Mental model** — mixer-desk sketch: sounds → group faders → master. <!-- TODO(D32) -->
3. **Step-by-step** — one-shot fire-and-forget vs held loop handle (alert-tone pattern). <!-- TODO(D32) -->
4. **Technical implementation** — miniaudio integration (single impl TU), init/shutdown timing in `Application`, headless-safe design (A1 acceptance), threading notes, formats. <!-- TODO(D32) -->
5. **Design decisions** — miniaudio choice (doc 08 rationale), A3 positional parked; app-side synthesis pattern (Frontier F10 `ProceduralAudio` WAV synth + `DistanceLoop`) as the approved way to do domain audio without engine bloat. <!-- TODO(D32) -->
6. **Limits & future work** — positional/3D audio (A3, parked until a consumer exists). <!-- TODO(D32) -->

**Truth sources:** doc 08 (A1/A2 banners), `AudioEngine.h`, the headless-safe doctests.

> **Section 4 must carry the `MA_COINIT_VALUE` story** — `audio/MiniaudioImpl.cpp:9-24` pins the
> main thread to the COM **STA** because `ma_context_init` calls `CoInitializeEx` on the calling
> thread and miniaudio's default is `COINIT_MULTITHREADED`, which deadlocked every native
> `IFileDialog` in the app for two weeks while audio kept working perfectly. It is written up
> client-side in [`../guide/audio.md`](../guide/audio.md#the-com-apartment-gotcha); this explainer
> owns the *why it is safe* half (WASAPI is apartment-agnostic; its device thread `CoInitialize`s
> itself) and the two tripwires (`tests/test_audio.cpp`'s apartment check and `FileDialog`'s
> `GuardApartment`).
