// test_reflect.cpp — reflection registry (Phase 13 / E1). Headless: no GL.
//
// Acceptance (plan doc 11 E1): enumerate fields of a registered type; get/set
// through descriptors round-trips; entt add-by-descriptor creates a live
// component; unknown hash -> nullptr, no crash.

#include <doctest.h>

#include "reflect/TypeRegistry.h"
#include "scene/Components.h"
#include "scene/ComponentRegistry.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

using namespace Cosmic;
using namespace Cosmic::Reflect;

// A test-only component + script-like type registered through the builder to
// exercise enum/color/range/flag hints without depending on a game module.
namespace
{
    enum class Mode : int32_t { Idle = 0, Run = 1, Stop = 2 };

    struct WidgetComponent
    {
        float     Gain = 1.0f;
        glm::vec4 Tint{ 1.0f };
        Mode      State = Mode::Idle;
        bool      Enabled = true;
        std::string Label = "widget";
    };

    // T1 (reflection metadata v2): a type exercising Doc + Units on some fields
    // and leaving others bare, so the "defaults where not declared" half is
    // provable in one place.
    struct MetricComponent
    {
        float Angle    = 0.0f;
        float Distance = 0.0f;
        float Duration = 0.0f;
        float Plain    = 0.0f;
    };
}

CS_REGISTER_COMPONENT(WidgetComponent)
CS_REGISTER_COMPONENT(MetricComponent)

TEST_CASE("E1: engine components are registered with their fields")
{
    TypeRegistry& reg = GetRegistry();

    const TypeDescriptor* transform = reg.Find<TransformComponent>();
    REQUIRE(transform != nullptr);
    CHECK(transform->Name == "Transform");
    CHECK(transform->Category == "Core");

    // Enumerate fields — Position/Rotation/Scale must all be present as Vec3.
    CHECK(transform->FindField("Position") != nullptr);
    CHECK(transform->FindField("Rotation") != nullptr);
    CHECK(transform->FindField("Scale") != nullptr);
    CHECK(transform->FindField("Position")->Kind == FieldKind::Vec3);
    CHECK(transform->FindField("UseQuatRotation")->Kind == FieldKind::Bool);
    CHECK(transform->FindField("RotationQuat")->Kind == FieldKind::Quat);

    // Lookup by name and by hash resolve to the same descriptor.
    CHECK(reg.FindByName("Transform") == transform);
    CHECK(reg.Find(entt::type_hash<TransformComponent>::value()) == transform);

    // A colour-flagged vec4 deduces to Color, not Vec4.
    const TypeDescriptor* mesh = reg.Find<MeshRendererComponent>();
    REQUIRE(mesh != nullptr);
    CHECK(mesh->FindField("Color")->Kind == FieldKind::Color);
    CHECK(mesh->FindField("CastShadows")->Kind == FieldKind::Bool);
}

TEST_CASE("E1: builder deduces kinds and applies hints/flags")
{
    // Register the test type (idempotent across test runs in one process).
    Reflect::Class<WidgetComponent>("Widget", "Test")
        .Field("Gain", &WidgetComponent::Gain).Range(0.0f, 10.0f).Tooltip("loop gain")
        .Field("Tint", &WidgetComponent::Tint).Color()
        .Field("State", &WidgetComponent::State)
            .EnumValue("Idle", 0).EnumValue("Run", 1).EnumValue("Stop", 2)
        .Field("Enabled", &WidgetComponent::Enabled)
        .Field("Label", &WidgetComponent::Label).ReadOnly();

    const TypeDescriptor* d = GetRegistry().Find<WidgetComponent>();
    REQUIRE(d != nullptr);
    CHECK(d->Category == "Test");

    const FieldDescriptor* gain = d->FindField("Gain");
    REQUIRE(gain != nullptr);
    CHECK(gain->Kind == FieldKind::Float);
    CHECK(gain->Hints.HasRange);
    CHECK(gain->Hints.Min == doctest::Approx(0.0f));
    CHECK(gain->Hints.Max == doctest::Approx(10.0f));
    CHECK(gain->Hints.Tooltip == "loop gain");

    CHECK(d->FindField("Tint")->Kind == FieldKind::Color);

    const FieldDescriptor* state = d->FindField("State");
    CHECK(state->Kind == FieldKind::Enum);
    REQUIRE(state->Hints.EnumEntries.size() == 3);
    CHECK(state->Hints.EnumEntries[1].Name == "Run");
    CHECK(state->Hints.EnumEntries[1].Value == 1);

    CHECK(d->FindField("Label")->HasFlag(Field_ReadOnly));
}

TEST_CASE("T1: reflection metadata v2 — Doc + Units reported where declared, defaulted where not")
{
    using Cosmic::Reflect::FieldUnits;

    Reflect::Class<MetricComponent>("Metric", "Test")
        .Field("Angle",    &MetricComponent::Angle).Doc("swept angle").Degrees()
        .Field("Distance", &MetricComponent::Distance).Meters()
        .Field("Duration", &MetricComponent::Duration).Seconds()
        .Field("Plain",    &MetricComponent::Plain);   // no Doc, no Units

    const TypeDescriptor* d = GetRegistry().Find<MetricComponent>();
    REQUIRE(d != nullptr);

    // Declared metadata is reported.
    const FieldDescriptor* angle = d->FindField("Angle");
    REQUIRE(angle != nullptr);
    CHECK(angle->Hints.Units == FieldUnits::Degrees);
    CHECK(angle->Hints.Tooltip == "swept angle");   // .Doc shares Tooltip storage
    CHECK(d->FindField("Distance")->Hints.Units == FieldUnits::Meters);
    CHECK(d->FindField("Duration")->Hints.Units == FieldUnits::Seconds);

    // Absent metadata defaults — a field with no hint call is byte-identical to
    // pre-T1 behavior (no units, empty doc, no range).
    const FieldDescriptor* plain = d->FindField("Plain");
    REQUIRE(plain != nullptr);
    CHECK(plain->Hints.Units == FieldUnits::None);
    CHECK(plain->Hints.Tooltip.empty());
    CHECK_FALSE(plain->Hints.HasRange);

    // Back-filled engine components carry the expected units.
    const TypeDescriptor* cam = GetRegistry().Find<CameraComponent>();
    REQUIRE(cam != nullptr);
    CHECK(cam->FindField("FovDeg")->Hints.Units == FieldUnits::Degrees);
    CHECK(cam->FindField("Near")->Hints.Units   == FieldUnits::Meters);

    const TypeDescriptor* emitter = GetRegistry().Find<ParticleEmitterComponent>();
    REQUIRE(emitter != nullptr);
    CHECK(emitter->FindField("LifeMin")->Hints.Units      == FieldUnits::Seconds);
    CHECK(emitter->FindField("ConeAngleDeg")->Hints.Units == FieldUnits::Degrees);

    // Untouched fields still default (no silent metadata churn).
    CHECK(cam->FindField("Primary")->Hints.Units == FieldUnits::None);
}

TEST_CASE("E1: get/set through descriptors round-trips")
{
    entt::registry registry;
    entt::entity e = registry.create();

    const TypeDescriptor* d = GetRegistry().Find<TransformComponent>();
    REQUIRE(d != nullptr);

    // Add-by-descriptor creates a LIVE component in the entt registry.
    void* comp = d->Add(registry, e);
    REQUIRE(comp != nullptr);
    CHECK(registry.all_of<TransformComponent>(e));

    // Set Position through the field's type-erased Write, read it back both via
    // the descriptor and via the real component — they must agree.
    const FieldDescriptor* pos = d->FindField("Position");
    REQUIRE(pos != nullptr);
    pos->Set(comp, FieldValue{ glm::vec3(3.0f, -2.0f, 7.5f) });

    FieldValue read = pos->Get(comp);
    glm::vec3 v = std::get<glm::vec3>(read);
    CHECK(v.x == doctest::Approx(3.0f));
    CHECK(v.y == doctest::Approx(-2.0f));
    CHECK(v.z == doctest::Approx(7.5f));
    CHECK(registry.get<TransformComponent>(e).Position.z == doctest::Approx(7.5f));

    // Enum round-trip (boxed as int32).
    Reflect::Class<WidgetComponent>("Widget", "Test");   // ensure registered
    const TypeDescriptor* wd = GetRegistry().Find<WidgetComponent>();
    void* wc = wd->Add(registry, e);
    wd->FindField("State")->Set(wc, FieldValue{ int32_t(2) });
    CHECK(std::get<int32_t>(wd->FindField("State")->Get(wc)) == 2);
    CHECK(registry.get<WidgetComponent>(e).State == Mode::Stop);
}

TEST_CASE("E1: entt glue — has/copy/remove and component enumeration")
{
    entt::registry registry;
    entt::entity src = registry.create();
    entt::entity dst = registry.create();

    const TypeDescriptor* td = GetRegistry().Find<TransformComponent>();
    void* s = td->Add(registry, src);
    td->FindField("Position")->Set(s, FieldValue{ glm::vec3(1.0f, 2.0f, 3.0f) });

    // Copy the component onto a different entity by descriptor.
    CHECK_FALSE(td->Has(registry, dst));
    td->Copy(registry, dst, s);
    CHECK(td->Has(registry, dst));
    CHECK(registry.get<TransformComponent>(dst).Position.y == doctest::Approx(2.0f));

    // Enumerate: add a light too, expect exactly the two descriptors back.
    const TypeDescriptor* ld = GetRegistry().Find<PointLightComponent>();
    ld->Add(registry, src);
    auto comps = GetRegistry().ComponentsOf(registry, src);
    CHECK(comps.size() == 2);

    // Remove is a no-op when absent, and clears when present.
    td->Remove(registry, dst);
    CHECK_FALSE(td->Has(registry, dst));
    td->Remove(registry, dst);   // must not crash
    CHECK_FALSE(td->Has(registry, dst));
}

TEST_CASE("E1: unknown type hash returns nullptr without crashing")
{
    TypeRegistry& reg = GetRegistry();
    CHECK(reg.Find(entt::id_type{ 0xDEADBEEF }) == nullptr);
    CHECK(reg.FindByName("NoSuchComponent") == nullptr);
}
