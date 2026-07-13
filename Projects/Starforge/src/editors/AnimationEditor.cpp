// AnimationEditor.cpp — see AnimationEditor.h. Starforge Animation Editor (M3).

#include "AnimationEditor.h"
#include "EditorContext.h"

#include "graphics/Skeleton.h"
#include "graphics/AnimationClip.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

using namespace Cosmic;

namespace Starforge
{
    AnimationEditor::AnimationEditor(std::string vfsPath)
        : m_Path(std::move(vfsPath))
    {
        m_Title = std::filesystem::path(m_Path).stem().string();
        if (m_Title.empty()) m_Title = "Animation";
    }

    void AnimationEditor::EnsureLoaded()
    {
        if (m_Loaded)
            return;
        m_Loaded = true;

        m_Mesh = AssetLibrary::GetMesh(m_Path);
        const Skeleton* sk = m_Mesh ? m_Mesh->GetSkeleton().get() : nullptr;
        m_HasSkin = sk && sk->JointCount() > 0;
        if (!m_HasSkin)
            return;

        // Cache parents + a children adjacency for the tree.
        const int n = (int)sk->JointCount();
        m_Parents.resize(n);
        m_Children.assign(n, {});
        for (int j = 0; j < n; ++j)
        {
            const int p = sk->Joints[j].Parent;
            m_Parents[j] = p;
            if (p >= 0 && p < n)
                m_Children[p].push_back(j);
        }

        m_ClipNames = AssetLibrary::GetAnimationClipNames(m_Path);
        if (!m_ClipNames.empty())
            SelectClip(0);
        else
            m_Timeline.Duration = 0.0f;   // static rig — bind-pose only
    }

    void AnimationEditor::SelectClip(int index)
    {
        if (index < 0 || index >= (int)m_ClipNames.size())
            return;
        m_ClipIndex = index;
        m_Clip = AssetLibrary::GetAnimationClip(m_Path + "#" + m_ClipNames[index]);
        m_Timeline.Time     = 0.0f;
        m_Timeline.Duration = m_Clip ? std::max(0.0f, m_Clip->Duration) : 0.0f;
        m_Timeline.ViewStart = 0.0f;
        RebuildTracks();
    }

    void AnimationEditor::RebuildTracks()
    {
        m_Tracks.clear();
        const Skeleton* sk = m_Mesh ? m_Mesh->GetSkeleton().get() : nullptr;
        if (!m_Clip || !sk)
            return;
        for (const AnimationChannel& ch : m_Clip->Channels)
        {
            TimelineTrack tr;
            tr.Name = (ch.JointIndex >= 0 && ch.JointIndex < (int)sk->JointCount())
                        ? sk->Joints[ch.JointIndex].Name : std::string("joint");
            // Union of the three key time sets (display-only ticks).
            tr.Keys.insert(tr.Keys.end(), ch.PosTimes.begin(), ch.PosTimes.end());
            tr.Keys.insert(tr.Keys.end(), ch.RotTimes.begin(), ch.RotTimes.end());
            tr.Keys.insert(tr.Keys.end(), ch.SclTimes.begin(), ch.SclTimes.end());
            std::sort(tr.Keys.begin(), tr.Keys.end());
            tr.Keys.erase(std::unique(tr.Keys.begin(), tr.Keys.end()), tr.Keys.end());
            m_Tracks.push_back(std::move(tr));
        }
    }

    void AnimationEditor::SamplePose()
    {
        const Skeleton* sk = m_Mesh ? m_Mesh->GetSkeleton().get() : nullptr;
        if (!sk || sk->JointCount() == 0)
            return;

        if (m_Clip && m_Timeline.Duration > 0.0f)
            m_Clip->Sample(*sk, m_Timeline.Time, m_Timeline.Loop, m_Locals);
        else
            sk->GetBindLocals(m_Locals);

        sk->ComputePalette(m_Locals, m_Palette);
        sk->ComputeGlobals(m_Locals, m_Globals);

        m_JointModels.resize(m_Globals.size());
        for (size_t j = 0; j < m_Globals.size(); ++j)
            m_JointModels[j] = sk->ImportCorrection * m_Globals[j];
    }

    void AnimationEditor::OnUpdate(EditorContext& /*ctx*/, float ts)
    {
        EnsureLoaded();
        if (m_HasSkin && m_Clip && m_Timeline.Playing)
            m_Timeline.Advance(ts);
    }

    void AnimationEditor::OnImGuiRender(EditorContext& ctx)
    {
        EnsureLoaded();

        if (!m_Mesh)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f),
                               "Could not load model: %s", m_Path.c_str());
            return;
        }
        if (!m_HasSkin)
        {
            ImGui::TextDisabled(ICON_LC_BONE " This model has no skeleton.");
            ImGui::TextWrapped("The Animation Editor previews rigged models (glTF/GLB/FBX with a "
                               "skin). Static meshes have nothing to animate.");
            return;
        }

        SamplePose();

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float timelineH = std::min(200.0f, std::max(120.0f, avail.y * 0.34f));
        const float topH = std::max(140.0f, avail.y - timelineH - style.ItemSpacing.y * 2.0f);

        // ---- Top: skeleton tree | preview | details --------------------------
        if (ImGui::BeginTable("##animtop", 3,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                              ImVec2(avail.x, topH)))
        {
            ImGui::TableSetupColumn("Skeleton", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Preview",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Details",  ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            DrawSkeletonTree(ctx);

            ImGui::TableSetColumnIndex(1);
            DrawPreview(ctx);

            ImGui::TableSetColumnIndex(2);
            DrawDetails(ctx);

            ImGui::EndTable();
        }

        // ---- Bottom: clip selector + timeline --------------------------------
        DrawTimeline(ctx);
    }

    void AnimationEditor::DrawSkeletonTree(EditorContext& /*ctx*/)
    {
        ImGui::TextUnformatted("Skeleton");
        ImGui::SameLine();
        const Skeleton* sk = m_Mesh->GetSkeleton().get();
        ImGui::TextDisabled("(%d joints)", (int)sk->JointCount());
        ImGui::Separator();

        if (ImGui::BeginChild("##tree", ImVec2(0, 0), false))
        {
            for (int j = 0; j < (int)sk->JointCount(); ++j)
                if (m_Parents[j] < 0)          // roots
                    DrawJointNode(j);
            // Defensive: joints with an out-of-range parent still show at root.
            for (int j = 0; j < (int)sk->JointCount(); ++j)
                if (m_Parents[j] >= (int)sk->JointCount())
                    DrawJointNode(j);
        }
        ImGui::EndChild();
    }

    void AnimationEditor::DrawJointNode(int j)
    {
        const Skeleton* sk = m_Mesh->GetSkeleton().get();
        ImGui::PushID(j);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth
                                 | ImGuiTreeNodeFlags_DefaultOpen;
        if (m_Children[j].empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_SelJoint == j)
            flags |= ImGuiTreeNodeFlags_Selected;

        const std::string label = std::string(ICON_LC_BONE) + " " + sk->Joints[j].Name;
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_SelJoint = j;

        if (open)
        {
            for (int c : m_Children[j])
                DrawJointNode(c);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void AnimationEditor::DrawPreview(EditorContext& /*ctx*/)
    {
        const ImVec2 region = ImGui::GetContentRegionAvail();
        m_PreviewW = (uint32_t)std::max(32.0f, region.x);
        m_PreviewH = (uint32_t)std::max(32.0f, region.y - 2.0f);

        const uint32_t tex = m_Preview.RenderSkeletal(
            m_Mesh, nullptr,
            m_Palette.empty() ? nullptr : m_Palette.data(), (uint32_t)m_Palette.size(),
            &m_JointModels, &m_Parents, m_SelJoint, m_ShowBones,
            m_PreviewW, m_PreviewH);

        if (tex == 0)
        {
            ImGui::TextDisabled("Preview unavailable.");
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        const bool clicked = ImGui::ImageButton("##animvp", (ImTextureID)(intptr_t)tex,
                                                ImVec2((float)m_PreviewW, (float)m_PreviewH),
                                                ImVec2(0, 1), ImVec2(1, 0));
        ImGui::PopStyleVar();

        const ImVec2 imgMin = ImGui::GetItemRectMin();
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsItemActivated())
            m_Dragged = false;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
        {
            m_Preview.Orbit(io.MouseDelta.x, io.MouseDelta.y);
            m_Dragged = true;
        }
        if (ImGui::IsItemHovered())
        {
            if (const float wheel = io.MouseWheel; wheel != 0.0f)
                m_Preview.Zoom(wheel);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_Preview.ResetView();
        }

        // Click (no drag) → select the nearest projected joint.
        if (clicked && !m_Dragged)
        {
            const glm::vec2 mouse{ io.MousePos.x - imgMin.x, io.MousePos.y - imgMin.y };
            int best = -1;
            float bestD = 12.0f;   // pixel threshold
            for (size_t j = 0; j < m_JointModels.size(); ++j)
            {
                glm::vec2 px;
                if (m_Preview.ProjectPoint(glm::vec3(m_JointModels[j][3]),
                                           m_PreviewW, m_PreviewH, px))
                {
                    const float d = glm::length(px - mouse);
                    if (d < bestD) { bestD = d; best = (int)j; }
                }
            }
            if (best >= 0)
                m_SelJoint = best;
        }
    }

    void AnimationEditor::DrawDetails(EditorContext& ctx)
    {
        if (ImGui::BeginChild("##details", ImVec2(0, 0), false))
        {
            ImGui::Checkbox("Show bones", &m_ShowBones);
            ImGui::Separator();

            const Skeleton* sk = m_Mesh->GetSkeleton().get();
            if (m_SelJoint < 0 || m_SelJoint >= (int)sk->JointCount())
            {
                ImGui::TextDisabled("Select a joint in the tree or click one\nin the preview.");
            }
            else
            {
                const SkeletonJoint& jt = sk->Joints[m_SelJoint];
                ImGui::Text("Joint: %s", jt.Name.c_str());
                ImGui::Text("Index: %d", m_SelJoint);
                if (jt.Parent >= 0 && jt.Parent < (int)sk->JointCount())
                    ImGui::Text("Parent: %s", sk->Joints[jt.Parent].Name.c_str());
                else
                    ImGui::TextDisabled("Parent: (root)");

                const glm::vec3 bind = glm::vec3(jt.LocalBind[3]);
                ImGui::Separator();
                ImGui::TextDisabled("Bind local translation");
                ImGui::Text("  %.3f, %.3f, %.3f", bind.x, bind.y, bind.z);

                if (m_SelJoint < (int)m_JointModels.size())
                {
                    const glm::vec3 pos = glm::vec3(m_JointModels[m_SelJoint][3]);
                    ImGui::TextDisabled("Posed model position");
                    ImGui::Text("  %.3f, %.3f, %.3f", pos.x, pos.y, pos.z);
                }

                // Socket authoring (M4) fills this section — see DrawSocketSection.
                DrawSocketSection(ctx);
            }
        }
        ImGui::EndChild();
    }

    void AnimationEditor::DrawSocketSection(EditorContext& /*ctx*/)
    {
        // M4 expands this into "attach a prop to <joint>". For now it names the
        // socket target so an author can add a SocketComponent in the Inspector.
        ImGui::Separator();
        ImGui::TextDisabled("Socket target");
        const Skeleton* sk = m_Mesh->GetSkeleton().get();
        if (m_SelJoint >= 0 && m_SelJoint < (int)sk->JointCount())
        {
            const std::string& name = sk->Joints[m_SelJoint].Name;
            ImGui::TextWrapped("Add a Socket component (Inspector ▸ Add Component) to a child of "
                               "this rig and set Joint = \"%s\".", name.c_str());
            if (ImGui::SmallButton("Copy joint name"))
                ImGui::SetClipboardText(name.c_str());
        }
    }

    void AnimationEditor::DrawTimeline(EditorContext& /*ctx*/)
    {
        // Clip selector.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Clip");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const char* preview = (m_ClipIndex >= 0 && m_ClipIndex < (int)m_ClipNames.size())
                                ? m_ClipNames[m_ClipIndex].c_str() : "(none)";
        if (ImGui::BeginCombo("##clip", preview))
        {
            for (int i = 0; i < (int)m_ClipNames.size(); ++i)
                if (ImGui::Selectable(m_ClipNames[i].c_str(), i == m_ClipIndex))
                    SelectClip(i);
            ImGui::EndCombo();
        }
        if (m_ClipNames.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(no clips — bind pose)");
        }

        Timeline::Draw("##animtimeline", m_Timeline, m_Tracks);
    }
}
