// TelemetryRecording.cpp — see TelemetryRecording.h.

#include "TelemetryRecording.h"
#include "EditorContext.h"

#include <algorithm>

using Cosmic::Reflect::FieldDescriptor;
using Cosmic::Reflect::FieldKind;

namespace Starforge::Telemetry
{
    bool IsRecordable(FieldKind k)
    {
        switch (k)
        {
            case FieldKind::Bool:
            case FieldKind::Int32:
            case FieldKind::UInt32:
            case FieldKind::Float:
            case FieldKind::Vec2:
            case FieldKind::Vec3:
            case FieldKind::Vec4:
            case FieldKind::Quat:
            case FieldKind::Color:
            case FieldKind::Enum:
                return true;
            default:                // String, AssetPath, EntityRef
                return false;
        }
    }

    int ComponentCount(FieldKind k)
    {
        switch (k)
        {
            case FieldKind::Vec2:  return 2;
            case FieldKind::Vec3:  return 3;
            case FieldKind::Vec4:
            case FieldKind::Color:
            case FieldKind::Quat:  return 4;
            default:               return 1;
        }
    }

    const char* AxisSuffix(FieldKind k, int comp)
    {
        if (ComponentCount(k) == 1 || comp < 0)
            return "";
        // Quats are stored (w,x,y,z) in the FieldValue box; the reader mirrors this.
        static const char* kVec[]  = { "x", "y", "z", "w" };
        static const char* kQuat[] = { "w", "x", "y", "z" };
        const int i = std::clamp(comp, 0, 3);
        return (k == FieldKind::Quat) ? kQuat[i] : kVec[i];
    }

    bool IsRecorded(const EditorContext& ctx, uint64_t uuid,
                    entt::id_type typeId, const std::string& field)
    {
        for (const RecordedChannel& c : ctx.Recorded)
            if (c.Entity == uuid && c.TypeId == typeId && c.Field == field)
                return true;
        return false;
    }

    void ToggleRecorded(EditorContext& ctx, uint64_t uuid, entt::id_type typeId,
                        const FieldDescriptor& field)
    {
        const bool present = IsRecorded(ctx, uuid, typeId, field.Name);

        // Always clear any existing entries for this field first.
        auto& v = ctx.Recorded;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const RecordedChannel& c)
                {
                    return c.Entity == uuid && c.TypeId == typeId && c.Field == field.Name;
                }), v.end());

        if (present)
            return;   // was on -> now removed

        const int n = ComponentCount(field.Kind);
        for (int comp = 0; comp < n; ++comp)
            v.push_back({ uuid, typeId, field.Name, (n == 1) ? -1 : comp });
    }

    void RecordAllFields(EditorContext& ctx, Cosmic::Entity entity)
    {
        if (!entity || !ctx.Scene) return;
        if (!entity.HasComponent<Cosmic::IDComponent>()) return;
        const uint64_t uuid = (uint64_t)entity.GetComponent<Cosmic::IDComponent>().ID;

        auto& reg = ctx.Scene->GetRegistry();
        for (const auto* d : Cosmic::Reflect::GetRegistry().ComponentsOf(reg, (entt::entity)entity))
        {
            for (const FieldDescriptor& f : d->Fields)
            {
                if (!IsRecordable(f.Kind)) continue;
                if (f.HasFlag(Cosmic::Reflect::Field_HideInInspector)) continue;
                if (!IsRecorded(ctx, uuid, d->TypeId, f.Name))
                    ToggleRecorded(ctx, uuid, d->TypeId, f);
            }
        }
    }
}
