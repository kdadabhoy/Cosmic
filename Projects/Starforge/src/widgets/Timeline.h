#pragma once

// widgets/Timeline.h
//
// ============================================================================
// Starforge — reusable timeline widget (Phase 24 / M2, gap §8.2 part).
// ============================================================================
//
// A pure-ImGui timeline: a time ruler with zoom/pan, a transport (play / pause /
// stop / loop), a draggable scrub head, and N labeled tracks whose per-track
// tick marks (keyframes) show hover info. DISPLAY + SCRUB ONLY — v1 has no key
// editing (drag/insert/delete). Key *editing* arrives with C6 (the sequencer),
// which reuses this exact widget; the Animation Editor (M3) is the first
// consumer and only needs to scrub + read.
//
// The transport/clock lives in TimelineState — a pure struct with GL-free,
// side-effect-free math (Advance / Scrub / Normalized). The Animation Editor
// owns one and feeds its play head into AnimationClip::Sample; a caller can drive
// TimelineState from a fake clock in a test (M2 acceptance) without ImGui.
// ============================================================================

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Starforge
{
    // ------------------------------------------------------------------------
    // TimelineState — the transport + view. Pure math (no ImGui); the widget
    // reads/writes it, and a test can drive Advance()/Scrub() directly.
    // ------------------------------------------------------------------------
    struct TimelineState
    {
        // Transport (seconds).
        float Time     = 0.0f;    // play head, kept in [0, Duration]
        float Duration = 1.0f;    // clip length (clamped >= 0 by the setters)
        bool  Playing  = false;
        bool  Loop     = true;
        float Speed    = 1.0f;    // playback multiplier (may be negative)

        // View (zoom / pan): horizontal pixels per second and the time at the
        // ruler's left edge. The widget clamps these to sane ranges.
        float PixelsPerSecond = 140.0f;
        float ViewStart       = 0.0f;

        // Advance the play head by real `dt` when Playing. Loop wraps into
        // [0, Duration); non-loop clamps to an end and stops. Duration <= 0
        // pins Time at 0. Returns the new Time. Pure — headless-drivable.
        float Advance(float dt)
        {
            if (Duration <= 0.0f) { Time = 0.0f; return Time; }
            if (!Playing)         { return Time; }

            Time += dt * Speed;
            if (Loop)
            {
                Time = std::fmod(Time, Duration);
                if (Time < 0.0f)
                    Time += Duration;              // negative Speed wraps too
            }
            else if (Time >= Duration) { Time = Duration; Playing = false; }
            else if (Time <  0.0f)     { Time = 0.0f;     Playing = false; }
            return Time;
        }

        // Explicit scrub (the head drag / paused re-pose) — clamps to the clip.
        void Scrub(float t)
        {
            Time = (Duration > 0.0f) ? std::clamp(t, 0.0f, Duration) : 0.0f;
        }

        float Normalized() const { return (Duration > 0.0f) ? Time / Duration : 0.0f; }
        void  SetNormalized(float n) { Scrub(n * Duration); }

        void Stop()  { Playing = false; Time = 0.0f; }
        void Pause() { Playing = false; }
        void Play()  { if (!Loop && Time >= Duration) Time = 0.0f; Playing = true; }
    };

    // One labeled track: a name + its keyframe times (display-only ticks).
    struct TimelineTrack
    {
        std::string        Name;
        std::vector<float> Keys;    // seconds; drawn as ticks, hover shows the time
        ImU32              Color = IM_COL32(255, 190, 90, 255);
    };

    struct TimelineOptions
    {
        float Height        = 0.0f;    // 0 = fill the available content region
        bool  ShowTransport = true;    // play/pause/stop/loop + time readout row
        bool  Snap          = false;   // snap scrubbing to the nearest keyframe
        float SnapSeconds   = 0.0f;    // > 0 = snap to this grid instead of keys
    };

    struct TimelineResult
    {
        bool Scrubbed = false;   // the play head moved by user drag this frame
        bool Toggled  = false;   // a transport button changed state this frame
    };

    class Timeline
    {
    public:
        // Draw the timeline for `id`, mutating `st` (transport + scrub). `tracks`
        // are display-only (labels + key ticks). Returns what the user did.
        static TimelineResult Draw(const char* id, TimelineState& st,
                                   const std::vector<TimelineTrack>& tracks,
                                   const TimelineOptions& opts = {});
    };
}
