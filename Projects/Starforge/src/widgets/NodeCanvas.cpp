// widgets/NodeCanvas.cpp — see header.

#include "widgets/NodeCanvas.h"

namespace Starforge
{
    NodeCanvas::~NodeCanvas()
    {
        if (m_Ctx)
        {
            ed::DestroyEditor(m_Ctx);
            m_Ctx = nullptr;
        }
    }

    void NodeCanvas::Begin(const char* id, const ImVec2& size)
    {
        if (!m_Ctx)
        {
            ed::Config cfg;
            cfg.SettingsFile = nullptr;   // the document persists layout, not the library
            m_Ctx = ed::CreateEditor(&cfg);
        }
        ed::SetCurrentEditor(m_Ctx);
        ed::Begin(id, size);
    }

    void NodeCanvas::End()
    {
        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    void NodeCanvas::QueryEdits(Edits& out)
    {
        // Link-creation gestures. AcceptNewItem() is true on the release that
        // confirms the drag; both pins must exist for a meaningful edit.
        if (ed::BeginCreate())
        {
            ed::PinId a, b;
            if (ed::QueryNewLink(&a, &b))
            {
                if (a && b)
                {
                    if (ed::AcceptNewItem())
                        out.Created.push_back({ (uintptr_t)a.Get(), (uintptr_t)b.Get() });
                }
                else
                {
                    ed::RejectNewItem();
                }
            }
        }
        ed::EndCreate();

        // Deletion gestures (Del key / context action on selection).
        if (ed::BeginDelete())
        {
            ed::LinkId link;
            while (ed::QueryDeletedLink(&link))
            {
                if (ed::AcceptDeletedItem())
                    out.DeletedLinks.push_back((uintptr_t)link.Get());
            }
            ed::NodeId node;
            while (ed::QueryDeletedNode(&node))
            {
                if (ed::AcceptDeletedItem())
                    out.DeletedNodes.push_back((uintptr_t)node.Get());
            }
        }
        ed::EndDelete();
    }

    void NodeCanvas::SetNodePosition(uintptr_t nodeId, const ImVec2& pos)
    {
        ed::SetNodePosition(ed::NodeId(nodeId), pos);
    }

    ImVec2 NodeCanvas::GetNodePosition(uintptr_t nodeId) const
    {
        return ed::GetNodePosition(ed::NodeId(nodeId));
    }

    void NodeCanvas::CenterOnContent()
    {
        ed::NavigateToContent(0.25f);
    }

    uintptr_t NodeCanvas::SelectedNode() const
    {
        ed::NodeId id;
        if (ed::GetSelectedNodes(&id, 1) >= 1)
            return (uintptr_t)id.Get();
        return 0;
    }

    uintptr_t NodeCanvas::SelectedLink() const
    {
        ed::LinkId id;
        if (ed::GetSelectedLinks(&id, 1) >= 1)
            return (uintptr_t)id.Get();
        return 0;
    }
}
