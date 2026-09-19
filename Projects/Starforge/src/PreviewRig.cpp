// PreviewRig.cpp — see PreviewRig.h.

#include "PreviewRig.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <entt/entt.hpp>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace Starforge
{
    namespace
    {
        // FNV-1a 64 — cache-file fingerprint of "source path + write times".
        uint64_t Fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull)
        {
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        std::string SanitizeCacheName(const std::string& in)
        {
            std::string out;
            out.reserve(in.size());
            for (char c : in)
                out += (std::isalnum((unsigned char)c)) ? c : '_';
            return out;
        }

        std::string ExtLower(const std::string& path)
        {
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos)
                return {};
            std::string e = path.substr(dot + 1);
            std::transform(e.begin(), e.end(), e.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return e;
        }

        // mtime (+ the .cmeta sibling's, for meshes) folded into the hash so an
        // edited source or changed import settings invalidates the disk cache.
        std::string SourceStamp(const std::string& resolved)
        {
            std::error_code ec;
            std::string stamp;
            const auto t = fs::last_write_time(resolved, ec);
            if (!ec)
                stamp += std::to_string(t.time_since_epoch().count());
            const auto tc = fs::last_write_time(resolved + ".cmeta", ec);
            if (!ec)
                stamp += ":" + std::to_string(tc.time_since_epoch().count());
            return stamp;
        }
    }

    bool PreviewRig::IsMeshExtension(const std::string& e)
    {
        return e == "obj" || e == "fbx" || e == "stl" || e == "dae" || e == "ply" ||
               e == "gltf" || e == "glb";
    }

    void PreviewRig::EnsureResources()
    {
    }

    void PreviewRig::Orbit(float dx, float dy)
    {
        m_Yaw   += dx * 0.012f;
        m_Pitch  = glm::clamp(m_Pitch - dy * 0.012f, glm::radians(-85.0f), glm::radians(85.0f));
    }

    void PreviewRig::Zoom(float wheelSteps)
    {
        m_Zoom = glm::clamp(m_Zoom * std::pow(0.88f, wheelSteps), 0.3f, 4.0f);
    }

    void PreviewRig::ResetView()
    {
        m_Yaw   = 0.6109f;
        m_Pitch = -0.3491f;
        m_Zoom  = 1.0f;
    }

    // ---- Batch thumbnails ---------------------------------------------------

    void PreviewRig::SetCacheDirectory(const std::string& dir)
    {
        m_CacheDir = dir;
        m_Queue.clear();
        m_Queued.clear();
        m_Failed.clear();
        m_Ready.clear();
    }

    std::string PreviewRig::CacheFileFor(const std::string& vfs) const
    {
        if (m_CacheDir.empty())
            return {};
        const std::string resolved = FileSystem::Resolve(vfs);
        const std::string stamp    = SourceStamp(resolved);
        if (stamp.empty())
            return {};   // source missing — nothing to fingerprint
        char hex[24];
        std::snprintf(hex, sizeof(hex), "%016llx",
                      (unsigned long long)Fnv1a(vfs + "|" + stamp));
        return m_CacheDir + "/" + SanitizeCacheName(vfs) + "-" + hex + ".png";
    }

    Ref<Texture2D> PreviewRig::Thumbnail(const std::string& vfs)
    {
        if (auto it = m_Ready.find(vfs); it != m_Ready.end())
            return it->second;
        if (m_Failed.count(vfs))
            return nullptr;
        if (m_Queued.insert(vfs).second)
            m_Queue.push_back(vfs);
        return nullptr;
    }

    void PreviewRig::Invalidate(const std::string& vfs)
    {
        m_Ready.erase(vfs);
        m_Failed.erase(vfs);
        // Stale disk entries are dropped when the replacement generates (the
        // fingerprinted name changes); memory eviction is what matters here.
    }

    void PreviewRig::PumpThumbnails(int budget)
    {
        while (budget-- > 0 && !m_Queue.empty())
        {
            const std::string vfs = m_Queue.front();
            m_Queue.pop_front();
            m_Queued.erase(vfs);
            if (m_Ready.count(vfs) || m_Failed.count(vfs))
                continue;
            if (!Generate(vfs))
                m_Failed.insert(vfs);
        }
    }

    bool PreviewRig::Generate(const std::string& vfs)
    {
        // W7 — both thumbnail sources (a .cmat sphere and a model file) need the
        // 3D pass. Report failure so the caller caches the miss ONCE and the
        // Content Browser draws its generic tile from then on.
        (void)vfs;
        return false;
    }
}
