# API Reference — Audio

> **STATUS: SKELETON** — to be filled by work order **D16** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/audio/AudioEngine.h`, `audio/Sound.h`.

**Read first:** [`docs/plans/archive/08-audio-plan.md`](../plans/archive/08-audio-plan.md) (A1/A2 shipped;
A3 positional is parked — do not document unshipped API); systems explainer
[audio](../systems/audio.md).

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `AudioEngine` — init/shutdown (engine-managed? verify), one-shot play (A1), loops + groups (A2), volume controls (master/group), headless-safe behavior (tests run without a device — pin this)
- [ ] `Sound` — load/create, lifetime (`Ref<Sound>`), formats supported (WAV/MP3/… — verify against miniaudio config), streaming vs memory
- [ ] Failure behavior — missing file, no output device (must not crash — cite the headless-safe tests)

## Sections to write

1. Entries per checklist. <!-- TODO(D16) -->
2. Example: alert-tone pattern from ViperSim (play loop on failsafe, stop on clear). <!-- TODO(D16) -->
3. Note on app-side synthesis: `Projects/Frontier` generates WAVs procedurally (F10 `ProceduralAudio`, app-side, not engine API) — mention as a pattern, don't document as engine surface. <!-- TODO(D16) -->

---
*Changelog:*
