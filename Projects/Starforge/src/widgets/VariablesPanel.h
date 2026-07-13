#pragma once

// widgets/VariablesPanel.h — the typed-variables blackboard editor (Phase 25 /
// Q2), shared by the Flow editor and the Story Graph editor. Draws a
// FlowVariable list (add/remove, group, per-type default incl. enum options);
// each row is a "FLOW_VAR" drag source (drop onto a guard's variable field).
//
// `beforeEdit` is invoked JUST BEFORE any mutation so the owning document can
// snapshot for undo. Pure ImGui; no document coupling beyond that callback.

#include "scene/FlowMachine.h"   // FlowVariable / FlowValue

#include <functional>
#include <vector>

namespace Starforge
{
    void DrawFlowVariablesPanel(const char* id, std::vector<Cosmic::FlowVariable>& vars,
                                const std::function<void()>& beforeEdit);

    // Draws a FlowGuard's SOURCE toggle (reflected Field vs a flow Variable — the
    // variable field is a FLOW_VAR drop target) + comparison op + typed value.
    // The caller owns the enclosing "has guard" checkbox; `beforeEdit` snapshots
    // just before any change. Shared by flow transitions + story options.
    void DrawFlowGuardFields(const char* id, Cosmic::FlowGuard& g,
                             const std::function<void()>& beforeEdit);
}
