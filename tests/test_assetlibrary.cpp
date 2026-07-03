// assets/AssetLibrary.h — S4.4a NormalizeKey equivalences. Purely lexical (no
// disk I/O, no GL), so this is headless-safe. The GPU cache hit/miss behavior is
// accepted via the Engine3DDemo "cache check" button, not here.

#include <doctest.h>

#include "assets/AssetLibrary.h"

using Cosmic::AssetLibrary;

TEST_CASE("NormalizeKey: raw and VFS spellings of the same file collapse to one key")
{
    // engine:// resolves to assets/<rest>; the raw equivalent with a ../ detour
    // must normalize to the same key.
    const std::string viaVfs = AssetLibrary::NormalizeKey("engine://models/duck.glb");
    const std::string viaRaw = AssetLibrary::NormalizeKey("assets/models/../models/duck.glb");
    CHECK(viaVfs == viaRaw);
}

TEST_CASE("NormalizeKey: collapses .. segments")
{
    const std::string a = AssetLibrary::NormalizeKey("assets/a/b/../c/file.png");
    const std::string b = AssetLibrary::NormalizeKey("assets/a/c/file.png");
    CHECK(a == b);
}

TEST_CASE("NormalizeKey: backslashes normalize to forward slashes")
{
    const std::string back = AssetLibrary::NormalizeKey("assets\\models\\duck.obj");
    const std::string fwd  = AssetLibrary::NormalizeKey("assets/models/duck.obj");
    CHECK(back == fwd);
    // generic_string() output uses forward slashes only.
    CHECK(fwd.find('\\') == std::string::npos);
}

TEST_CASE("NormalizeKey: is deterministic (idempotent on its own output)")
{
    const std::string once  = AssetLibrary::NormalizeKey("engine://textures/grid.png");
    const std::string twice = AssetLibrary::NormalizeKey(once);
    CHECK(once == twice);
}
