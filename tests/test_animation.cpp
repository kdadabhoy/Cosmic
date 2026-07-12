// test_animation.cpp — Phase 20 / A2: the pure CPU half of skeletal animation.
// Skeleton palette math, AnimationClip sampling (t=0/mid/end, loop wrap, slerp),
// the fixed-rate bake, and the cgltf skin/clip import path via a hand-written
// skinned glTF (no GL anywhere — geometry/palette only).

#include "doctest.h"

#include "graphics/Skeleton.h"
#include "graphics/AnimationClip.h"
#include "assets/MeshImport.h"
#include "assets/AssetLibrary.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Cosmic::Skeleton;
using Cosmic::SkeletonJoint;
using Cosmic::AnimationClip;
using Cosmic::AnimationChannel;

namespace
{
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::length(a - b) <= eps;
    }

    glm::vec3 XformPoint(const glm::mat4& m, const glm::vec3& p)
    {
        return glm::vec3(m * glm::vec4(p, 1.0f));
    }

    // Two-joint chain: root at origin, tip 1m above it (the classic arm).
    Skeleton MakeChain()
    {
        Skeleton s;
        SkeletonJoint root;
        root.Name = "root";
        s.Joints.push_back(root);

        SkeletonJoint tip;
        tip.Name        = "tip";
        tip.Parent      = 0;
        tip.LocalBind   = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, 0.0f });
        tip.InverseBind = glm::translate(glm::mat4(1.0f), { 0.0f, -1.0f, 0.0f });
        s.Joints.push_back(tip);
        return s;
    }
}

TEST_SUITE("Skeletal animation (A2)")
{
    TEST_CASE("Bind pose produces an identity palette")
    {
        const Skeleton s = MakeChain();
        std::vector<glm::mat4> locals, palette;
        s.GetBindLocals(locals);
        s.ComputePalette(locals, palette);

        REQUIRE(palette.size() == 2);
        // A vertex bound to either joint must not move at bind pose.
        CHECK(Near(XformPoint(palette[0], { 0.3f, 0.2f, 0.1f }), { 0.3f, 0.2f, 0.1f }));
        CHECK(Near(XformPoint(palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("Palette follows a moved child joint (any joint order)")
    {
        // Child listed BEFORE its parent — ComputeGlobals must resolve anyway.
        Skeleton s;
        SkeletonJoint tip;
        tip.Name        = "tip";
        tip.Parent      = 1;
        tip.LocalBind   = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, 0.0f });
        tip.InverseBind = glm::translate(glm::mat4(1.0f), { 0.0f, -1.0f, 0.0f });
        s.Joints.push_back(tip);
        SkeletonJoint root;
        root.Name = "root";
        s.Joints.push_back(root);

        std::vector<glm::mat4> locals;
        s.GetBindLocals(locals);
        locals[0] = glm::translate(glm::mat4(1.0f), { 0.0f, 2.0f, 0.0f });   // tip raised 1m

        std::vector<glm::mat4> palette;
        s.ComputePalette(locals, palette);
        CHECK(Near(XformPoint(palette[0], { 0.0f, 1.0f, 0.0f }), { 0.0f, 2.0f, 0.0f }));
    }

    TEST_CASE("Import correction conjugates the palette (unit scale)")
    {
        Skeleton s = MakeChain();
        const float k = 0.01f;   // the FBX cm preset
        s.ImportCorrection    = glm::scale(glm::mat4(1.0f), glm::vec3(k));
        s.ImportCorrectionInv = glm::inverse(s.ImportCorrection);

        std::vector<glm::mat4> locals;
        s.GetBindLocals(locals);
        // Tip translated +1 source-unit above bind.
        locals[1] = glm::translate(glm::mat4(1.0f), { 0.0f, 2.0f, 0.0f });

        std::vector<glm::mat4> palette;
        s.ComputePalette(locals, palette);

        // Vertices are BAKED with the correction: bind tip (0,1,0) lives at
        // (0,0.01,0). The palette must move it exactly one corrected unit up.
        CHECK(Near(XformPoint(palette[1], { 0.0f, k, 0.0f }), { 0.0f, 2.0f * k, 0.0f }, 1e-5f));
    }

    TEST_CASE("Clip sampling: t=0 / mid / end lerp + non-loop clamp")
    {
        const Skeleton s = MakeChain();

        AnimationClip clip;
        clip.Duration = 2.0f;
        AnimationChannel ch;
        ch.JointIndex = 1;
        ch.PosTimes   = { 0.0f, 2.0f };
        ch.PosValues  = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 3.0f, 0.0f } };
        ch.RotTimes   = { 0.0f };
        ch.RotValues  = { glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };
        ch.SclTimes   = { 0.0f };
        ch.SclValues  = { glm::vec3(1.0f) };
        clip.Channels.push_back(ch);

        std::vector<glm::mat4> locals;
        clip.Sample(s, 0.0f, false, locals);
        CHECK(Near(glm::vec3(locals[1][3]), { 0.0f, 1.0f, 0.0f }));

        clip.Sample(s, 1.0f, false, locals);   // midpoint
        CHECK(Near(glm::vec3(locals[1][3]), { 0.0f, 2.0f, 0.0f }));

        clip.Sample(s, 2.0f, false, locals);   // end
        CHECK(Near(glm::vec3(locals[1][3]), { 0.0f, 3.0f, 0.0f }));

        clip.Sample(s, 5.0f, false, locals);   // past the end, non-loop -> clamp
        CHECK(Near(glm::vec3(locals[1][3]), { 0.0f, 3.0f, 0.0f }));
    }

    TEST_CASE("Loop wraps time; unanimated joints keep their bind locals")
    {
        const Skeleton s = MakeChain();

        AnimationClip clip;
        clip.Duration = 2.0f;
        AnimationChannel ch;
        ch.JointIndex = 1;
        ch.PosTimes   = { 0.0f, 2.0f };
        ch.PosValues  = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 3.0f, 0.0f } };
        clip.Channels.push_back(ch);

        CHECK(clip.ResolveTime(2.5f, true) == doctest::Approx(0.5f));
        CHECK(clip.ResolveTime(-0.5f, true) == doctest::Approx(1.5f));

        std::vector<glm::mat4> locals;
        clip.Sample(s, 2.5f, true, locals);   // wraps to 0.5 -> lerp .25 of the way
        CHECK(Near(glm::vec3(locals[1][3]), { 0.0f, 1.5f, 0.0f }));
        // Joint 0 has no channel — bind local (identity) untouched.
        CHECK(Near(glm::vec3(locals[0][3]), { 0.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("Rotation keys slerp the short way")
    {
        const Skeleton s = MakeChain();

        // 0 -> 90 deg about Z on the root, negated end quat (same rotation,
        // opposite sign) — shortest-arc handling must not spin the long way.
        const glm::quat q0 = glm::angleAxis(0.0f, glm::vec3(0, 0, 1));
        const glm::quat q1 = -glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));

        AnimationClip clip;
        clip.Duration = 1.0f;
        AnimationChannel ch;
        ch.JointIndex = 0;
        ch.RotTimes   = { 0.0f, 1.0f };
        ch.RotValues  = { q0, q1 };
        clip.Channels.push_back(ch);

        std::vector<glm::mat4> locals;
        clip.Sample(s, 0.5f, false, locals);   // expect 45 deg about Z
        const glm::vec3 rotated = glm::vec3(locals[0] * glm::vec4(1, 0, 0, 1));
        const float r22 = std::sqrt(2.0f) / 2.0f;
        CHECK(Near(rotated, { r22, r22, 0.0f }, 1e-3f));
    }

    TEST_CASE("Fixed-rate bake resamples to the same poses")
    {
        const Skeleton s = MakeChain();

        AnimationClip clip;
        clip.Duration = 1.0f;
        AnimationChannel ch;
        ch.JointIndex = 1;
        ch.PosTimes   = { 0.0f, 1.0f };
        ch.PosValues  = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 2.0f, 0.0f } };
        clip.Channels.push_back(ch);

        const AnimationClip baked = clip.BakeFixedRate(30.0f);
        CHECK(baked.Channels.size() == 1);
        CHECK(baked.Channels[0].PosTimes.size() == 31);   // 0..30/30

        std::vector<glm::mat4> a, b;
        for (float t : { 0.0f, 0.31f, 0.77f, 1.0f })
        {
            clip.Sample(s, t, false, a);
            baked.Sample(s, t, false, b);
            CHECK(Near(glm::vec3(a[1][3]), glm::vec3(b[1][3]), 1e-3f));
        }
    }

    TEST_CASE("Skinned glTF imports skeleton + weights + clip (cgltf, headless)")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_anim_gltf";
        fs::create_directories(dir);

        {
            std::ofstream bin(dir / "rig.bin", std::ios::binary);
            const uint16_t idx[4]     = { 0, 1, 2, 0 };                 // 6 bytes + pad
            const float    pos[9]     = { 0,0,0,  1,0,0,  0,1,0 };
            const uint8_t  joints[12] = { 0,0,0,0,  0,0,0,0,  1,0,0,0 };
            const float    weights[12]= { 1,0,0,0,  1,0,0,0,  1,0,0,0 };
            const float    ibm[32]    = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0, 0,0,1,     // joint 0: identity
                                          1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1 };   // joint 1: T(0,-1,0)
            const float    times[2]   = { 0.0f, 1.0f };
            const float    trans[6]   = { 0,1,0,  0,2,0 };
            bin.write((const char*)idx, 8);        // 0
            bin.write((const char*)pos, 36);       // 8
            bin.write((const char*)joints, 12);    // 44
            bin.write((const char*)weights, 48);   // 56
            bin.write((const char*)ibm, 128);      // 104
            bin.write((const char*)times, 8);      // 232
            bin.write((const char*)trans, 24);     // 240 .. 264
        }
        {
            std::ofstream out(dir / "rig.gltf");
            out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"name": "skinned", "mesh": 0, "skin": 0},
    {"name": "root", "children": [2]},
    {"name": "tip", "translation": [0, 1, 0]}
  ],
  "skins": [{"joints": [1, 2], "inverseBindMatrices": 4}],
  "meshes": [{"primitives": [{
     "attributes": {"POSITION": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
     "indices": 0}]}],
  "animations": [{
    "name": "Lift",
    "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}],
    "samplers": [{"input": 5, "output": 6, "interpolation": "LINEAR"}]
  }],
  "buffers": [{"uri": "rig.bin", "byteLength": 264}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0,   "byteLength": 6},
    {"buffer": 0, "byteOffset": 8,   "byteLength": 36},
    {"buffer": 0, "byteOffset": 44,  "byteLength": 12},
    {"buffer": 0, "byteOffset": 56,  "byteLength": 48},
    {"buffer": 0, "byteOffset": 104, "byteLength": 128},
    {"buffer": 0, "byteOffset": 232, "byteLength": 8},
    {"buffer": 0, "byteOffset": 240, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0,0,0], "max": [1,1,0]},
    {"bufferView": 2, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0], "max": [1]},
    {"bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3"}
  ]
})";
        }

        const std::string path = (dir / "rig.gltf").string();

        Cosmic::ImportedModelDesc desc;
        REQUIRE(Cosmic::MeshImport::ImportModelData(
            desc, path, Cosmic::ImportSettings::DefaultFor("gltf")));

        // Skeleton: 2 joints, tip parented to root, IBM read.
        REQUIRE(desc.Bones.Joints.size() == 2);
        CHECK(desc.Bones.Joints[0].Name == "root");
        CHECK(desc.Bones.Joints[1].Name == "tip");
        CHECK(desc.Bones.Joints[1].Parent == 0);
        CHECK(Near(glm::vec3(desc.Bones.Joints[1].InverseBind[3]), { 0.0f, -1.0f, 0.0f }));

        // Skin: vertex 2 rides joint 1 with weight 1.
        REQUIRE(desc.Meshes.size() == 1);
        REQUIRE(desc.Meshes[0].Skin.size() == 3);
        CHECK(desc.Meshes[0].Skin[2].Joints.x == doctest::Approx(1.0f));
        CHECK(desc.Meshes[0].Skin[2].Weights.x == doctest::Approx(1.0f));

        // Clip: one channel on the tip, 1s long; sampled palette moves v2 from
        // (0,1,0) at t=0 to (0,2,0) at t=1 (and halfway between at t=0.5).
        REQUIRE(desc.Clips.size() == 1);
        CHECK(desc.Clips[0].Name == "Lift");
        CHECK(desc.Clips[0].Duration == doctest::Approx(1.0f));

        std::vector<glm::mat4> locals, palette;
        for (const auto& [t, expectY] : std::vector<std::pair<float, float>>{
                 { 0.0f, 1.0f }, { 0.5f, 1.5f }, { 1.0f, 2.0f } })
        {
            desc.Clips[0].Sample(desc.Bones, t, false, locals);
            desc.Bones.ComputePalette(locals, palette);
            CHECK(Near(XformPoint(palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, expectY, 0.0f }));
        }

        // The AssetLibrary clip surface (CPU-only): by name, by index, misses.
        CHECK(Cosmic::AssetLibrary::GetAnimationClip(path + "#Lift") != nullptr);
        CHECK(Cosmic::AssetLibrary::GetAnimationClip(path + "#0") != nullptr);
        CHECK(Cosmic::AssetLibrary::GetAnimationClip(path + "#Nope") == nullptr);
        const std::vector<std::string> names = Cosmic::AssetLibrary::GetAnimationClipNames(path);
        REQUIRE(names.size() == 1);
        CHECK(names[0] == "Lift");

        fs::remove_all(dir);
    }

    TEST_CASE("Fox-class rigged glTF imports and samples (set COSMIC_FOX_GLB)")
    {
        // Optional real-asset smoke: point COSMIC_FOX_GLB at the Khronos Fox
        // sample (or any rigged .glb) and this validates the whole CPU chain —
        // skin, skeleton, every clip sampled across its duration, palettes
        // finite. Quietly passes when the variable is unset (no bundled asset).
        const char* fox = std::getenv("COSMIC_FOX_GLB");
        if (!fox || !*fox)
            return;

        Cosmic::ImportedModelDesc desc;
        REQUIRE(Cosmic::MeshImport::ImportModelData(
            desc, fox, Cosmic::ImportSettings::DefaultFor("glb")));

        REQUIRE(!desc.Meshes.empty());
        REQUIRE(desc.Bones.Joints.size() >= 2);
        REQUIRE(!desc.Clips.empty());

        // Every skinned vertex references valid joints with sane weights.
        for (const Cosmic::ImportedMeshDesc& sm : desc.Meshes)
        {
            if (sm.Skin.empty())
                continue;
            REQUIRE(sm.Skin.size() == sm.Geometry.Vertices.size());
            for (size_t i = 0; i < sm.Skin.size(); i += 97)   // sparse sweep
            {
                const Cosmic::SkinVertex& sv = sm.Skin[i];
                const float wsum = sv.Weights.x + sv.Weights.y + sv.Weights.z + sv.Weights.w;
                CHECK(wsum > 0.5f);
                CHECK(wsum < 1.5f);
                for (int k = 0; k < 4; ++k)
                    CHECK((int)(sv.Joints[k] + 0.5f) < (int)desc.Bones.Joints.size());
            }
        }

        // Every clip samples to finite palettes at start / middle / end.
        std::vector<glm::mat4> locals, palette;
        for (const AnimationClip& clip : desc.Clips)
        {
            CHECK(clip.Duration > 0.0f);
            for (float f : { 0.0f, 0.5f, 1.0f })
            {
                clip.Sample(desc.Bones, clip.Duration * f, true, locals);
                desc.Bones.ComputePalette(locals, palette);
                REQUIRE(palette.size() == desc.Bones.Joints.size());
                bool finite = true;
                for (const glm::mat4& m : palette)
                    for (int c = 0; c < 4 && finite; ++c)
                        for (int r = 0; r < 4 && finite; ++r)
                            finite = std::isfinite(m[c][r]);
                CHECK(finite);
            }
        }
    }
}
