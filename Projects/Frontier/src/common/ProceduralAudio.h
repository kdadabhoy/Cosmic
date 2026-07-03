#pragma once

// ProceduralAudio.h
//
// Frontier ambience helper (Phase 11, doc 10 F10). Returns a cached Sound for a
// named recipe so the worlds get atmosphere with zero committed audio assets:
//
//   - A user-dropped project://sounds/<name>.wav WINS (CC0 assets a user drops
//     into Projects/Frontier/assets/sounds/ override the synth after a rebuild).
//   - Otherwise the recipe is SYNTHESIZED once into user://frontier/audio/<name>.wav
//     (16-bit PCM mono 44.1 kHz) and loaded from there — delete that folder to
//     regenerate.
//
// Recipes: "rumble" / "wind" / "water" are seamless loops (crossfaded); "thunder"
// is a one-shot decay. Never returns null (Sound::Create is degraded-silent).
// App-side: Frontier owns audio MEANING; the engine ships only generic verbs.

#include <Cosmic.h>

#include <string>

namespace Frontier
{
    class ProceduralAudio
    {
    public:
        /** Cached Sound for `name` (see the recipe list above). Synthesizes + caches
         *  on first use; subsequent calls return the same Ref. */
        static Cosmic::Ref<Cosmic::Sound> Ensure(const char* name);
    };
}
