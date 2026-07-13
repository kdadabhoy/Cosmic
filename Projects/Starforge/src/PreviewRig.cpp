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
        if (!m_Fbo)
        {
            FramebufferSpecification spec;
            spec.Width  = 128;
            spec.Height = 128;
            spec.Attachments = { FramebufferTextureFormat::RGBA8,
                                 FramebufferTextureFormat::DEPTH24STENCIL8 };
            m_Fbo = FrameBuffer::Create(spec);
        }
        if (!m_Sphere)
            m_Sphere = Mesh::CreateUVSphere(0.5f, 24, 48);
        if (!m_DefaultMaterial)
        {
            MaterialAsset neutral;
            neutral.Albedo    = { 0.72f, 0.72f, 0.74f, 1.0f };
            neutral.Roughness = 0.55f;
            m_DefaultMaterial = AssetLibrary::BuildMaterial(neutral, "PreviewDefault");
        }
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

    uint32_t PreviewRig::Draw(const Ref<Mesh>& mesh, const Ref<Material>& material,
                              const glm::vec4& color, uint32_t w, uint32_t h)
    {
        if (!mesh || w == 0 || h == 0)
            return 0;
        EnsureResources();
        if (!m_Fbo)
            return 0;

        // §0.5 state-restore contract: remember what we replace, restore after.
        const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();

        if (m_Fbo->GetWidth() != w || m_Fbo->GetHeight() != h)
            m_Fbo->Resize(w, h);
        m_Fbo->Bind();
        RenderCommand::SetViewport(0, 0, w, h);
        RenderCommand::SetClearColor({ 0.118f, 0.129f, 0.157f, 1.0f });
        RenderCommand::Clear();

        // Frame the mesh's local bounds with the rig's orbit camera.
        const glm::vec3 center = mesh->GetLocalCenter();
        const float radius = std::max(0.01f,
            0.5f * glm::length(mesh->GetLocalMax() - mesh->GetLocalMin()));
        const float dist = radius * 2.4f * m_Zoom;

        const glm::vec3 offset{
            dist * std::cos(m_Pitch) * std::sin(m_Yaw),
            dist * std::sin(-m_Pitch),
            dist * std::cos(m_Pitch) * std::cos(m_Yaw) };
        const glm::vec3 eye = center + offset;

        const glm::mat4 view = glm::lookAt(eye, center, { 0.0f, 1.0f, 0.0f });
        const glm::mat4 proj = glm::perspective(glm::radians(38.0f), (float)w / (float)h,
                                                std::max(0.005f, dist - radius * 2.5f),
                                                dist + radius * 2.5f);

        // Key light from the camera's upper-left (travels down-right into the
        // shot). The viewport re-uploads scene lights next frame, so this UBO
        // write never outlives the requesting frame.
        const glm::vec3 fwd   = glm::normalize(center - eye);
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        const glm::vec3 up    = glm::cross(right, fwd);
        Renderer3D::SceneLightsDesc lights;
        lights.SunDirection = glm::normalize(fwd - 0.55f * up - 0.35f * right);
        lights.SunIntensity = 1.1f;
        lights.Ambient      = 0.30f;
        Renderer3D::SetLights(lights);

        Renderer3D::BeginScene(proj * view, eye);
        if (material)
            Renderer3D::DrawMesh(mesh, glm::mat4(1.0f), material);
        else
            Renderer3D::DrawMesh(mesh, glm::mat4(1.0f), color);
        Renderer3D::EndScene();

        // Restore the replaced framebuffer + the engine render-state defaults
        // (depth ON/ON, cull None, blend Alpha, fill) — doc 13 §0.5.
        RenderCommand::BindFramebufferHandle(prevFbo);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetCullMode(RenderCommand::CullMode::None);
        RenderCommand::SetBlendMode(RenderCommand::BlendMode::Alpha);
        RenderCommand::SetPolygonMode(RenderCommand::PolygonMode::Fill);

        return m_Fbo->GetColorAttachmentRendererID(0);
    }

    uint32_t PreviewRig::RenderMesh(const Ref<Mesh>& mesh, const Ref<Material>& material,
                                    const glm::vec4& lambertColor, uint32_t w, uint32_t h)
    {
        return Draw(mesh, material, lambertColor, w, h);
    }

    uint32_t PreviewRig::RenderSkeletal(const Ref<Mesh>& mesh, const Ref<Material>& material,
                                        const glm::mat4* palette, uint32_t jointCount,
                                        const std::vector<glm::mat4>* jointModels,
                                        const std::vector<int>* parents,
                                        int selected, bool showBones,
                                        uint32_t w, uint32_t h)
    {
        if (!mesh || w == 0 || h == 0)
            return 0;
        EnsureResources();
        if (!m_Fbo)
            return 0;

        // §0.5 state-restore contract: remember what we replace, restore after.
        const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();

        if (m_Fbo->GetWidth() != w || m_Fbo->GetHeight() != h)
            m_Fbo->Resize(w, h);
        m_Fbo->Bind();
        RenderCommand::SetViewport(0, 0, w, h);
        RenderCommand::SetClearColor({ 0.118f, 0.129f, 0.157f, 1.0f });
        RenderCommand::Clear();

        // Frame the mesh's bind-pose bounds with the rig's orbit camera (the pose
        // can push geometry outside; the framing on bind bounds is close enough).
        const glm::vec3 center = mesh->GetLocalCenter();
        const float radius = std::max(0.01f,
            0.5f * glm::length(mesh->GetLocalMax() - mesh->GetLocalMin()));
        const float dist = radius * 2.4f * m_Zoom;

        const glm::vec3 offset{
            dist * std::cos(m_Pitch) * std::sin(m_Yaw),
            dist * std::sin(-m_Pitch),
            dist * std::cos(m_Pitch) * std::cos(m_Yaw) };
        const glm::vec3 eye = center + offset;

        const glm::mat4 view = glm::lookAt(eye, center, { 0.0f, 1.0f, 0.0f });
        const glm::mat4 proj = glm::perspective(glm::radians(38.0f), (float)w / (float)h,
                                                std::max(0.005f, dist - radius * 2.5f),
                                                dist + radius * 2.5f);
        m_LastViewProj = proj * view;

        const glm::vec3 fwd   = glm::normalize(center - eye);
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        const glm::vec3 up    = glm::cross(right, fwd);
        Renderer3D::SceneLightsDesc lights;
        lights.SunDirection = glm::normalize(fwd - 0.55f * up - 0.35f * right);
        lights.SunIntensity = 1.1f;
        lights.Ambient      = 0.30f;
        Renderer3D::SetLights(lights);

        const Ref<Material>& mat = material ? material : m_DefaultMaterial;

        Renderer3D::BeginScene(m_LastViewProj, eye);
        if (palette && jointCount > 0 && mesh->IsSkinned())
            Renderer3D::DrawMeshSkinned(mesh, glm::mat4(1.0f), mat, palette, jointCount);
        else
            Renderer3D::DrawMesh(mesh, glm::mat4(1.0f), mat);   // bind pose / static
        Renderer3D::Flush();   // draw the mesh now (depth ON — the engine default)

        // Bone overlay ON TOP of the mesh (depth test off for the line flush).
        if (showBones && jointModels && !jointModels->empty())
        {
            const glm::vec4 boneCol{ 0.35f, 0.85f, 1.0f, 1.0f };
            const glm::vec4 selCol { 1.0f, 0.75f, 0.20f, 1.0f };
            const float cross = radius * 0.02f + 0.01f;
            const auto& JM = *jointModels;
            for (size_t j = 0; j < JM.size(); ++j)
            {
                const glm::vec3 p = glm::vec3(JM[j][3]);
                const int par = (parents && j < parents->size()) ? (*parents)[j] : -1;
                const bool onSel = ((int)j == selected) ||
                                   (par == selected && selected >= 0);
                const glm::vec4 col = onSel ? selCol : boneCol;
                if (par >= 0 && (size_t)par < JM.size())
                    Renderer3D::DrawLine(glm::vec3(JM[par][3]), p, col);
                Renderer3D::DrawLine(p - glm::vec3(cross, 0, 0), p + glm::vec3(cross, 0, 0), col);
                Renderer3D::DrawLine(p - glm::vec3(0, cross, 0), p + glm::vec3(0, cross, 0), col);
                Renderer3D::DrawLine(p - glm::vec3(0, 0, cross), p + glm::vec3(0, 0, cross), col);
            }
            if (selected >= 0 && (size_t)selected < JM.size())
                Renderer3D::DrawAxes(JM[selected], radius * 0.15f + 0.05f);   // inspect-only tripod
        }
        RenderCommand::SetDepthTest(false);   // overlay lines draw over the mesh
        Renderer3D::EndScene();

        // Restore the replaced framebuffer + engine render-state defaults (§0.5).
        RenderCommand::BindFramebufferHandle(prevFbo);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetCullMode(RenderCommand::CullMode::None);
        RenderCommand::SetBlendMode(RenderCommand::BlendMode::Alpha);
        RenderCommand::SetPolygonMode(RenderCommand::PolygonMode::Fill);

        return m_Fbo->GetColorAttachmentRendererID(0);
    }

    bool PreviewRig::ProjectPoint(const glm::vec3& p, uint32_t w, uint32_t h,
                                  glm::vec2& outPx) const
    {
        const glm::vec4 clip = m_LastViewProj * glm::vec4(p, 1.0f);
        if (clip.w <= 1e-5f)
            return false;   // behind the camera
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        // The image is drawn flipped-V (uv0=(0,1), uv1=(1,0)) so ndc.y=+1 is the
        // image's top row (py=0).
        outPx.x = (ndc.x * 0.5f + 0.5f) * (float)w;
        outPx.y = (0.5f - 0.5f * ndc.y) * (float)h;
        return true;
    }

    uint32_t PreviewRig::RenderMaterial(const MaterialAsset& asset, uint32_t w, uint32_t h)
    {
        EnsureResources();

        // Rebuild the live material only when the asset's fields changed (the
        // reflected JSON doubles as a cheap content key).
        const std::string key = SceneSerializer::SaveReflectedToString(
            entt::type_hash<MaterialAsset>::value(), &asset);
        if (!m_LiveMaterial || key != m_LiveMaterialKey)
        {
            m_LiveMaterial    = AssetLibrary::BuildMaterial(asset, "PreviewLive");
            m_LiveMaterialKey = key;
        }
        return Draw(m_Sphere, m_LiveMaterial, glm::vec4(1.0f), w, h);
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
        EnsureResources();

        const std::string cacheFile = CacheFileFor(vfs);

        // Disk hit: a previous session already rendered this exact source state.
        std::error_code ec;
        if (!cacheFile.empty() && fs::exists(cacheFile, ec))
        {
            if (Ref<Texture2D> tex = Texture2D::Create(cacheFile))
            {
                m_Ready[vfs] = tex;
                return true;
            }
        }

        // Render at thumbnail size.
        const std::string ext = ExtLower(vfs);
        uint32_t texId = 0;
        if (ext == "cmat")
        {
            MaterialAsset asset;
            if (!AssetLibrary::LoadMaterialAsset(asset, vfs))
                return false;
            Ref<Material> mat = AssetLibrary::BuildMaterial(asset, "Thumb " + vfs);
            texId = Draw(m_Sphere, mat, glm::vec4(1.0f), 128, 128);
        }
        else if (IsMeshExtension(ext))
        {
            Ref<Mesh> mesh = AssetLibrary::GetMesh(vfs);
            if (!mesh)
                return false;
            texId = Draw(mesh, m_DefaultMaterial, glm::vec4(0.75f, 0.75f, 0.78f, 1.0f), 128, 128);
        }
        if (texId == 0)
            return false;

        // Read the render back and persist + upload it. Going through the PNG
        // keeps the browser's existing image-display convention (same loader,
        // same V-flip) — thumbnails behave exactly like any other image tile.
        m_Fbo->Bind();
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        const bool read = m_Fbo->ReadPixels(0, rgba, w, h);
        m_Fbo->Unbind();
        if (!read || w == 0 || h == 0)
            return false;

        Ref<Texture2D> tex;
        if (!cacheFile.empty())
        {
            fs::create_directories(fs::path(cacheFile).parent_path(), ec);

            // Drop stale fingerprints of the same asset before writing the new one.
            const std::string prefix = SanitizeCacheName(vfs) + "-";
            for (const auto& entry : fs::directory_iterator(fs::path(cacheFile).parent_path(), ec))
                if (entry.is_regular_file(ec) &&
                    entry.path().filename().string().rfind(prefix, 0) == 0)
                    fs::remove(entry.path(), ec);

            if (ImageIO::WritePNG(cacheFile, (int)w, (int)h, 4, rgba.data()))
                tex = Texture2D::Create(cacheFile);
        }
        if (!tex)
        {
            // No disk cache (or the write failed): upload the pixels directly.
            // ReadPixels returns top-left rows; the file loader path arrives
            // bottom-up (stb flip-on-load), and the browser draws images with
            // flipped V — so flip here to match that display convention.
            std::vector<uint8_t> flipped((size_t)w * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(flipped.data() + (size_t)(h - 1 - y) * w * 4,
                            rgba.data() + (size_t)y * w * 4, (size_t)w * 4);
            tex = Texture2D::Create(w, h);
            if (tex)
                tex->SetData(flipped.data(), (uint32_t)flipped.size());
        }
        if (!tex)
            return false;

        m_Ready[vfs] = tex;
        return true;
    }
}
