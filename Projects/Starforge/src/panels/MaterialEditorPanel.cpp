// panels/MaterialEditorPanel.cpp — see MaterialEditorPanel.h.

#include "panels/MaterialEditorPanel.h"
#include "EditorContext.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>

#include <memory>

using namespace Cosmic;

namespace Starforge
{
    namespace
    {
        // A4 — one undoable field edit on the panel's working MaterialAsset
        // copy. The panel outlives the command stack (StarforgeApp member), so
        // targeting its asset by pointer is stable; the field is re-resolved by
        // name on every Do/Undo so registry storage can move freely. If the
        // user loaded a DIFFERENT material since, undo still edits the panel's
        // current asset — same "acts on what the panel holds" semantics as the
        // panel's own widgets.
        class MaterialFieldEdit final : public ICommand
        {
        public:
            MaterialFieldEdit(MaterialAsset* target, std::string fieldName,
                              Reflect::FieldValue before, Reflect::FieldValue after)
                : m_Target(target), m_Field(std::move(fieldName)),
                  m_Before(std::move(before)), m_After(std::move(after)) {}

            void Do() override   { Apply(m_After); }
            void Undo() override { Apply(m_Before); }
            std::string Name() const override { return "Material " + m_Field; }

        private:
            void Apply(const Reflect::FieldValue& v)
            {
                const auto* desc = Reflect::GetRegistry().Find<MaterialAsset>();
                if (!desc)
                    return;
                for (const auto& f : desc->Fields)
                    if (f.Name == m_Field)
                    {
                        f.Set(m_Target, v);
                        return;
                    }
            }

            MaterialAsset*      m_Target;
            std::string         m_Field;
            Reflect::FieldValue m_Before, m_After;
        };
    }

    void MaterialEditorPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        if (ImGui::Begin("Material Editor", pOpen))
        {
            if (!ctx.ProjectOpen)
            {
                ImGui::TextDisabled("Open a project to author materials.");
                ImGui::End();
                return;
            }

            // --- Toolbar: New / name / Save --------------------------------
            if (ImGui::Button("New"))
            {
                m_Asset = MaterialAsset{};
                m_Path.clear();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputText("##matname", m_SaveName, sizeof(m_SaveName));
            ImGui::SameLine();
            if (ImGui::Button("Save .cmat") && m_SaveName[0])
            {
                const std::string vfs = std::string("project://materials/") + m_SaveName + ".cmat";
                if (AssetLibrary::SaveMaterialAsset(m_Asset, vfs))
                {
                    m_Path = vfs;
                    AssetLibrary::Reload(vfs);   // drop any cached build so re-resolve picks up edits
                    ctx.Preview.Invalidate(vfs); // A4 — the browser thumbnail is stale now
                    ctx.Log("[Material] Saved " + vfs);
                }
                else
                {
                    ctx.Log("[Material] Save failed: " + vfs, LogSeverity::Error);
                }
            }
            if (!m_Path.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_Path.c_str());
            }

            // Empty-state hint (E21): the New -> tune -> Save -> Assign flow.
            if (m_Path.empty())
                ImGui::TextDisabled("New material — tune below, Save .cmat, then Assign to a selected mesh.");

            // --- Live preview sphere (A4 — the rig's interactive mode) -----
            // W7: the preview draws a lit UV sphere through Renderer3D, so the
            // 2D build has no sphere. The reflected field editor below — which
            // IS the material authoring surface, and matters in 2D because a
            // SpriteRenderer can carry a material — is untouched.

            // --- Reflected material fields (auto-UI, undoable — A4) --------
            // The capture-on-activate / push-on-commit idiom the Inspector uses:
            // drags mutate m_Asset live, and ONE command lands per completed
            // edit (Push — the change is already applied).
            if (const auto* desc = Reflect::GetRegistry().Find<MaterialAsset>())
            {
                for (const auto& f : desc->Fields)
                {
                    PropertyRows::SlotContext slot{ &ctx.Preview, &ctx.PendingRevealAsset };
                    const auto r = PropertyRows::DrawField(f, &m_Asset, /*mixed*/ false, &slot);
                    if (r.Activated)
                    {
                        m_EditField  = f.Name;
                        m_EditBefore = r.PreValue;
                    }
                    if (r.Committed)
                    {
                        const Reflect::FieldValue before =
                            (m_EditField == f.Name) ? m_EditBefore : r.PreValue;
                        ctx.Commands.Push(std::make_unique<MaterialFieldEdit>(
                            &m_Asset, f.Name, before, r.PostValue));
                        m_EditField.clear();
                    }
                }
            }

            ImGui::Separator();

            // --- Assign / load to the selected MeshRenderer ----------------
            // MeshRenderer is 3D-only; a 2D material is assigned from the
            // Inspector's SpriteRenderer material slot instead.
            ImGui::TextDisabled("Edits are undoable (Ctrl+Z). Save the .cmat, then assign it");
            ImGui::TextDisabled("from a SpriteRenderer's material slot in the Inspector.");
        }
        ImGui::End();
    }
}
