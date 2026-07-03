// MiniaudioImpl.cpp
// Last Modified 7/2/2026
//
// The ONE translation unit that compiles the vendored miniaudio implementation
// (docs/plans/08-audio-plan.md — single header, public-domain/MIT-0, WASAPI
// backend on Windows, no extra linker deps). Everything else includes
// miniaudio.h declarations only.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
