# App Platform — design contracts

Status: contract of record, 2026-09-18, against `main` `8da533c`. Every name, field, signature and
ordering below is **fixed** for the parallel lanes. A work order that needs a deviation records it in
its report under "Contract deviations" and the integrator (AP-Q1) updates this file; nobody edits another
lane's files to make a deviation fit. Section anchors are the ones the prompts cite (`§1` … `§14`).

Design rule inherited from the roadmap: **the engine ships generic verbs; apps own domain logic.** Nothing
below knows what a pendulum, a rocket or an ESC is.

---

## §1 DataBus — `Cosmic/src/data/DataBus.{h,cpp}` (new, AP-01)

The host-owned, GL-free store that connects app logic to screens. It is a member of the **host**
(`PlayerLayer`, and `StarforgeApp` for editor Play), never of the module DLL, so it survives a module
reload. Included by `Cosmic.h`; manifest row `data/DataBus.h → ../guide/app-authoring.md` (AP-D2 may
re-point it to `reference/app-services.md`).

```cpp
namespace Cosmic
{
    struct COSMIC_API DataValue
    {
        enum class Kind : int32_t { Number, Bool, String } ValueKind = Kind::Number;
        double      Number = 0.0;
        bool        Bool   = false;
        std::string String;
        static DataValue MakeNumber(double v);
        static DataValue MakeBool(bool b);
        static DataValue MakeString(std::string s);
        double      AsNumber() const;   // Bool -> 0/1; String -> strtod or 0.0
        std::string AsString() const;   // Number -> "%g"; Bool -> "true"/"false"
    };

    struct DataSample { double Time; double Value; };     // ring-history entry (numeric channels only)

    class COSMIC_API DataBus
    {
    public:
        using Handle  = uint64_t;                                   // 0 == invalid
        using Handler = std::function<void(const std::string& channel, const DataValue& value)>;
        static constexpr size_t kDefaultHistory = 1024;

        // ---- writes (MAIN THREAD ONLY in v1; document, do not lock) ----
        void Set(const std::string& channel, double v);
        void SetBool(const std::string& channel, bool v);
        void SetString(const std::string& channel, std::string v);
        void Set(const std::string& channel, const DataValue& v);

        // ---- reads ----
        bool        Has(const std::string& channel) const;
        DataValue   Get(const std::string& channel) const;                       // default DataValue when missing
        double      GetNumber(const std::string& channel, double fallback = 0.0) const;
        bool        GetBool(const std::string& channel, bool fallback = false) const;
        std::string GetString(const std::string& channel, const std::string& fallback = "") const;
        double      Age(const std::string& channel) const;                       // Now() - last write; +inf when missing
        double      LastWriteTime(const std::string& channel) const;             // -1 when missing

        // ---- history (numeric channels; allocated lazily on first Set) ----
        void   SetHistoryCapacity(const std::string& channel, size_t samples);   // 0 = keep no history
        size_t HistoryCapacity(const std::string& channel) const;                // kDefaultHistory unless set
        // Copies oldest->newest into `out`; windowSeconds > 0 keeps only samples with Time >= Now()-window.
        size_t History(const std::string& channel, std::vector<DataSample>& out, double windowSeconds = 0.0) const;

        // ---- time: the host calls Advance once per frame with the UNSCALED frame delta ----
        void   Advance(double dt);          // Now() += max(dt, 0); starts at 0 for the bus lifetime
        double Now() const;

        // ---- producers (D-LINKS): ServiceHost brackets each service tick with SetProducer(name) ----
        void        SetProducer(const std::string& producer);                    // "" = none
        std::string Producer(const std::string& channel) const;                  // last writer's producer tag ("" if none)

        // ---- subscriptions: synchronous, main thread, fired inside Set after the value is stored ----
        Handle Subscribe(const std::string& channel, Handler fn);
        Handle SubscribeAny(Handler fn);
        void   Unsubscribe(Handle h);                                            // no-op on unknown

        // ---- maintenance ----
        std::vector<std::string> Channels() const;                                // sorted
        void   Remove(const std::string& channel);
        void   Clear();                                                          // channels + history + producers; subscriptions kept
        size_t ChannelCount() const;
    };
}
```

Semantics: a `Set` on a missing channel creates it (like `FlowMachine::SetVar`). Non-finite numbers
are stored as written (channels are data); widgets display them as the placeholder, plots skip them,
flow channel guards evaluate false. Dispatch uses the `EventBus` discipline (snapshot the handler list,
liveness-check each, disconnect-during-dispatch prevents the call, connect-during-dispatch does not
receive the in-flight value). A handler may call `Set` on another channel; nesting deeper than 64
logs one warning and drops the nested dispatch. Every method is safe on an empty bus. Complexity: one
hash lookup per call; `History` copies. No engine header other than `core/Core.h` is included.

## §2 App services — `Cosmic/src/scripting/AppService.h`, `ServiceHost.{h,cpp}` (new, AP-01)

```cpp
namespace Cosmic
{
    class DataBus; class Scene; class FlowMachine; class Entity; class Event; struct UiRect;

    class COSMIC_API PanelRegistry                      // hosted-panel draw callbacks (D-UI, D-LINKS)
    {
    public:
        using DrawFn = std::function<void(const UiRect& rect)>;   // called INSIDE an ImGui window sized to rect
        struct Source { std::string File; int Line = 0; };
        void Register(const std::string& name, DrawFn fn, const char* file = nullptr, int line = 0);   // re-register replaces
        void Unregister(const std::string& name);
        bool Has(const std::string& name) const;
        bool Draw(const std::string& name, const UiRect& rect) const;   // false when unknown; the host wraps Begin/End
        Source SourceOf(const std::string& name) const;
        std::vector<std::string> Names() const;                         // sorted
        void Clear();
    };

    struct AppContext
    {
        DataBus&       Bus;
        PanelRegistry& Panels;
        Scene*         ActiveScene = nullptr;   // the flow's top scene / the played scene; updated by ServiceHost::BindScene
        FlowMachine*   Flow        = nullptr;   // null when the project has no startup flow
        std::string    ProjectName;
        bool           InEditor    = false;     // true under Starforge Play
    };

    class COSMIC_API AppService
    {
    public:
        virtual ~AppService() = default;
    protected:
        virtual void OnAttach(AppContext& ctx) { (void)ctx; }   // once per run, after ALL services are constructed, before the first BindScene
        virtual void OnDetach() {}                              // once, before the module unloads / Play stops
        virtual void OnUpdate(float ts) { (void)ts; }           // every variable tick, BEFORE UiSystem::Update and the flow
        virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }   // every fixed step, BEFORE ScriptHost::FixedTick
        virtual void OnSignal(const std::string& signal, Entity source) { (void)signal; (void)source; }   // every scene-bus signal
        virtual void OnSceneChanged(Scene* oldScene, Scene* newScene) { (void)oldScene; (void)newScene; }
        virtual void OnEvent(Event& e) { (void)e; }             // input/app events while not paused, before scripts

        AppContext&    Context() const;   // valid from OnAttach to OnDetach
        DataBus&       Bus()     const { return Context().Bus; }
        PanelRegistry& Panels()  const { return Context().Panels; }
    private:
        friend class ServiceHost;
        AppContext* m_Ctx = nullptr;
    };

    // Register a hosted-panel drawer from inside OnAttach; records the call site for "Open source".
    // Expands inside a member function of an AppService subclass (Panels() is a protected member).
    #define CS_PANEL(name, fn) Panels().Register((name), (fn), __FILE__, __LINE__)

    struct ServiceDescriptor
    {
        std::string                    Name;
        std::function<AppService*()>   Factory;
        std::string                    Module;   // owner module ("" = in-exe)
        std::string                    File;     // registration call site (D-LINKS)
        int                            Line = 0;
        int                            Order = 0;
    };

    template<typename T> class ServiceBuilder { public: ServiceBuilder& Order(int n); /* CS_END closes the block */ };

    class COSMIC_API ServiceHost
    {
    public:
        ServiceHost() = default;
        ~ServiceHost();
        ServiceHost(const ServiceHost&) = delete;
        ServiceHost& operator=(const ServiceHost&) = delete;

        // Construct every service registered by `module` (registration order, stable-sorted by Order),
        // inject the context, call OnAttach in that order. Idempotent re-entry: calls Destroy() first.
        void Instantiate(const std::string& module, const AppContext& ctx);
        // Re-point services at a new scene: unsubscribes the old bus, subscribes ConnectAny on the new one
        // (fanning to OnSignal), updates ctx.ActiveScene, calls OnSceneChanged(old, new) on every service.
        void BindScene(Scene* scene);
        void Tick(float ts);              // Bus.SetProducer(name) ... OnUpdate ... SetProducer("") per service
        void FixedTick(float fixedDt);    // same bracket around OnFixedUpdate
        void DispatchEvent(Event& e);
        void DispatchSignal(const std::string& signal, Entity source);   // public for tests
        void Destroy();                   // OnDetach in REVERSE order, delete, unsubscribe, Panels.Clear()

        bool   IsInstantiated() const;
        size_t Count() const;
        std::vector<std::string> Names() const;   // instantiation order
        const ServiceDescriptor* DescriptorOf(const std::string& name) const;
    };
}
```

`ModuleRegistry` gains `template<typename T> ServiceBuilder<T> AddService(const std::string& name,
const char* file, int line)`, `const ServiceDescriptor* FindService(const std::string&) const`,
`std::vector<std::string> ServiceNames() const` / `ServiceNames(const std::string& module) const`, and
`UnregisterModule` erases that module's services exactly like its scripts (KI-29 discipline: before
`FreeLibrary`). `ModuleMacros.h` gains

```cpp
#define CS_SERVICE(T)  { using CS_ReflectedType = T; ::Cosmic::ModuleRegistry::Get().AddService<T>(#T, __FILE__, __LINE__)
// usage:   CS_SERVICE(PendulumService).Order(0) CS_END;     (no CS_FIELD support in v1)
```

**Host frame order (both hosts, fixed):**

| Phase | PlayerLayer | StarforgeApp (Play) |
| --- | --- | --- |
| attach / PlayScene | read manifest → `m_Bus` fresh → `m_Services.Instantiate(project, ctx)` → flow `Start`/scene load → `RebindScripts` (which also calls `m_Services.BindScene(scene)`) | build the runtime scene → `m_PlayBus.Clear()` unless resuming after a reload → `m_Services.Instantiate(projectName, ctx{InEditor=true})` → `m_Scripts.SetDataBus(&m_PlayBus)` → `m_Scripts.Instantiate` → `m_Services.BindScene` |
| variable tick | `m_Bus.Advance(unscaledDt)` → `m_Scenes.OnUpdate` → `RebindScripts` (+ `BindScene` on swap) → `m_Services.Tick(ts)` (skipped while paused) → `UpdateUI(&m_Bus)` → key bridge + `m_Flow.OnUpdate` → `m_Scripts.Tick` → render | same order inside `TickPlay`; `RenderViewport` passes `&m_PlayBus` while playing, the edit-mode preview bus + `preview=true` otherwise |
| fixed tick | `m_Services.FixedTick` → `m_Scripts.FixedTick` → physics → contact dispatch | same |
| ImGui | hosted panels (`UiSystem::CollectHostedPanels` → `m_Panels.Draw` inside a NoDecoration window at the viewport-offset rect) → pause menu | hosted panels over the viewport image (letterbox-aware) → the rest of the editor |
| detach / StopScene | `m_Flow.Stop()` → `m_Scripts.Destroy()` → `m_Services.Destroy()` → physics/renderer shutdown → `UnregisterModule` | `m_Scripts.Destroy()` → `m_Services.Destroy()` → restore the edit scene |
| ReloadModule (editor) | — | if playing: `StopScene` (destroys services, bus untouched) → snapshot / unload / load as today → AP-03 resumes |

Scripts get a `Data()` proxy (`ScriptableEntity` and `SystemScript`), injected by
`ScriptHost::SetDataBus(DataBus*)` before `Instantiate` (same seam shape as `SetTelemetrySink`):
`GetNumber/Set/GetBool/SetBool/GetString/SetString/Has/Age`; every call is a no-op / default with no bus.

## §3 Bound widgets — `scene/ui/UiComponents.h`, `UiSystem.cpp`, `reflect/TypeRegistry.cpp` (AP-02)

Reflected names are in quotes; category `"UI"`; every struct gets `CS_REGISTER_COMPONENT`. Runtime-only
members are not reflected. `PreviewValue`/`PreviewOn` are used only in preview mode (edit mode), or when
the bus lacks the channel while in preview mode.

| Component (reflected name) | Fields (type, default) | Draws / does |
| --- | --- | --- |
| `UiValueTextComponent` ("UiValueText") | `std::string Channel`; `std::string Format = "%.2f"` (printf, exactly one numeric conversion; anything else → literal); `std::string Prefix`, `Suffix`; `std::string Placeholder = "--"`; `float StaleAfter = 0` (s, 0 = never); `glm::vec4 StaleColor{0.6,0.6,0.6,1}`; `float PreviewValue = 0` | **Requires a sibling `UiTextComponent`** (same entity) that supplies font/size/colour/alignment; at draw time the resolved string replaces `UiText.Text` (the component's own `Text` is untouched). Missing channel → `Placeholder`; `Age > StaleAfter` → drawn with `StaleColor`. String channels print `AsString()`; bool → `true`/`false`. |
| `UiGaugeComponent` ("UiGauge") | `Channel`; `float Min = 0, Max = 100`; `UiGaugeStyle Style = Bar` (`Bar=0`, `Arc=1`); `UiGaugeDirection Direction = LeftToRight` (`LeftToRight=0`, `BottomToTop=1`; ignored by Arc); `glm::vec4 FillColor{0.2,0.8,0.3,1}`, `TrackColor{0.15,0.15,0.18,1}`; `float Thickness = 0.25` (Arc ring thickness as a fraction of the radius); `float PreviewValue = 50` | Fills the element rect: Bar = track quad + fill quad clipped to `fill = clamp((v-Min)/(Max-Min),0,1)`; Arc = 270° ring (gap at the bottom) via `Renderer2D::DrawCircle` thickness/fade for the track and a polyline of quads for the fill. A sibling `UiImage` draws behind. |
| `UiIndicatorComponent` ("UiIndicator") | `Channel`; `std::string Op = "=="` (`== != < > <= >=`); `float Threshold = 1`; `glm::vec4 OnTint{0.2,1,0.3,1}`, `OffTint{0.3,0.3,0.3,1}`; `std::string OnTexture`, `OffTexture` (`AsAssetPath("texture")`, empty = solid); `bool PreviewOn = false` | Draws a quad with the On/Off texture + tint; bool channels compare as 0/1; missing/non-finite → Off. |
| `UiPlotComponent` ("UiPlot") | `std::string Channel, Channel2, Channel3, Channel4`; `float WindowSeconds = 10`; `bool AutoScaleY = true`; `float YMin = -1, YMax = 1`; `glm::vec4 LineColor{0.3,0.8,1,1}, LineColor2{1,0.6,0.2,1}, LineColor3{0.6,1,0.4,1}, LineColor4{1,0.4,0.8,1}`; `glm::vec4 GridColor{1,1,1,0.12}`, `BackgroundColor{0,0,0,0.35}`; `int32_t GridDivisions = 4`; `float LineWidth = 2` (px, canvas-scaled); `bool ShowLabels = true`; `float PreviewAmplitude = 1` | Background quad, grid lines, one polyline per bound channel from `bus->History(ch, out, WindowSeconds)` (x = time within the window, y = value), Y range from the visible samples when `AutoScaleY` (padded 5 %), labels (min/max Y, window seconds) via `DrawString`. Skips non-finite samples. Preview: a sine of `PreviewAmplitude` over the window. Stays within `Renderer2D::MaxLines` per plot (decimate to ≤ 512 segments per channel). |
| `UiSliderComponent` ("UiSlider") | `Channel` (written); `float Min = 0, Max = 1, Step = 0`; `std::string Signal` (emitted on release when the value changed; empty = none); `UiSliderOrientation Orientation = Horizontal` (`Horizontal=0`, `Vertical=1`); `glm::vec4 TrackColor{0.15,0.15,0.18,1}`, `FillColor{0.3,0.6,1,1}`, `KnobColor{0.95,0.95,1,1}`; `float KnobSize = 18` (px); `bool Interactable = true`; `float PreviewValue = 0.5`; runtime: `bool Dragging = false` | Track + fill + knob. Interaction in `UiSystem::Update`: press inside arms + sets, drag updates, release emits `Signal` with the entity as source. Value = `Min + t*(Max-Min)` snapped to `Step` when `Step > 0`; written with `bus->Set(Channel, v)` (so the app's own writes to the channel are reflected when not dragging). |
| `UiToggleComponent` ("UiToggle") | `Channel` (bool written); `std::string Signal`; `glm::vec4 OnTint{0.2,1,0.3,1}`, `OffTint{0.5,0.5,0.5,1}`; `std::string OnTexture`, `OffTexture`; `bool Interactable = true`; `bool PreviewOn = false`; runtime: `bool Armed = false` | **An image + this** (the tint multiplies into a sibling `UiImage`, like `UiButton`). Release-inside flips `bus->GetBool(Channel)` and emits `Signal`. |
| `UiHostedPanelComponent` ("UiHostedPanel") | `std::string PanelName`; `bool ShowFrame = true`; `glm::vec4 FrameColor{1,1,1,0.25}`; `std::string PlaceholderText` (empty = `PanelName`); runtime: `bool DrawnThisFrame = false` | `Render` draws only the frame (and, in preview mode or when the host reports the name unregistered, the placeholder label). The contents are drawn by the host through `CollectHostedPanels` + `PanelRegistry::Draw` (§4). |

`UiSystem` additions (signatures land in AP-01 with no-op bodies; AP-02 implements):

```cpp
struct UiHostedPanelDraw { uint32_t Handle; std::string Name; UiRect Rect; float Scale; };

static bool Update(Scene&, const UiRect& viewport, const UiPointer&, const glm::mat4* cameraViewProj = nullptr,
                   DataBus* bus = nullptr);                                   // sliders/toggles write the bus
static void Render(Scene&, const UiRect& viewport, const glm::mat4* cameraViewProj = nullptr,
                   const DataBus* bus = nullptr, bool preview = false);
static void Render(Scene&, const UiRect& canvasRect, uint32_t targetW, uint32_t targetH,
                   const glm::mat4* cameraViewProj = nullptr, const DataBus* bus = nullptr, bool preview = false);
static void CollectHostedPanels(Scene&, const UiRect& viewport, std::vector<UiHostedPanelDraw>& out,
                                const glm::mat4* cameraViewProj = nullptr);  // back-to-front like elements
// pure, unit-tested helpers (AP-02):
static std::string FormatValue(const UiValueTextComponent&, const DataValue* value, bool stale);
static float       GaugeFill(float min, float max, double value);           // 0..1, non-finite -> 0
static double      SliderValueAt(const UiRect& rect, UiSliderOrientation, const glm::vec2& p, float min, float max, float step);
```

Rules: `bus == nullptr` ⇒ preview mode regardless of `preview`. Interactive precedence: the topmost
element with an interactable `UiButton`, `UiSlider` or `UiToggle` under the pointer wins; `Update`
returns true when the pointer is over any of them. Existing call sites keep compiling (defaults).
Serialization needs no per-type code (reflection-driven); AP-02 proves that with a round-trip test.

## §4 Hosted panels — host side (AP-01 plumbing, AP-02 collection, AP-03 editor draw)

Per frame, after the scene render and before any other ImGui of the host:

```
panels = UiSystem::CollectHostedPanels(scene, viewportRect, camVP)
for each p in panels (back to front):
    screenRect = viewportScreenTopLeft + p.Rect            // editor: map through the letterbox band (m_GameBandUv)
    ImGui::SetNextWindowPos(screenRect.Min); SetNextWindowSize(screenRect.Size()); SetNextWindowViewport(main viewport id)
    ImGui::Begin("##hosted_" + p.Name + "_" + handle, nullptr,
        NoDecoration | NoMove | NoResize | NoSavedSettings | NoDocking | NoFocusOnAppearing | NoNav | NoBackground)
    drawn = m_Panels.Draw(p.Name, p.Rect)                   // false => leave the canvas placeholder visible
    ImGui::End()
```
Unknown names are not an error (the canvas shows "`<name>` (unregistered)"). The ImGui stack must be
balanced around the block (the WO-07 oracle checks it in V05). Edit mode never calls `Draw`.

## §5 Screens, screen scripts, flow additions (AP-01 engine, AP-03 editor, AP-04 templates)

**Screen convention.** A screen is `scenes/<Name>.cscene` containing at least: an entity `"Canvas"` with
`CanvasComponent` (+ optional full-bleed `UiImage`), an entity `"Camera"` with an orthographic
`CameraComponent` (`Primary = true`), and — when the screen has logic — a `NativeScriptComponent{ClassName
= "<Name>Screen"}` on the canvas entity. The flow state has the same `Name` and `"scene":
"project://scenes/<Name>.cscene"`.

**Screen script stub** (`Projects/Starforge/assets/editor/stubs/ScreenScript.h.in`, AP-03; tokens
`@SCREEN@`, `@PROJECT_NAME@`):

```cpp
#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

// @SCREEN@ screen — per-display logic for scenes/@SCREEN@.cscene (created by Starforge ▸ Screens ▸ New Screen).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
class @SCREEN@Screen : public Cosmic::ScriptableEntity
{
public:
    float ExampleField = 1.0f;
protected:
    void OnStart() override {}
    void OnUpdate(float ts) override { (void)ts; }
    void OnSignal(const std::string& signal, Cosmic::Entity source) override { (void)signal; (void)source; }
};
```

**`Module.cpp` markers** (all templates carry them; the scaffold inserts between them):

```cpp
    // CS_SCREENS_BEGIN — managed by Starforge ▸ Screens (one CS_SCRIPT per screen script)
    // CS_SCREENS_END
```
The scaffold also inserts `#include "screens/<Name>Screen.h"` after the last `#include` line that
precedes `CS_MODULE_BEGIN`. Missing markers ⇒ the editor reports "Module.cpp has no CS_SCREENS markers"
and does not edit the file.

**Flow additions** (`scene/FlowMachine.{h,cpp}`):

```cpp
struct FlowGuard { std::string Channel; /* NEW, highest precedence */ std::string Var; std::string Entity, Component, Field; std::string Op = "=="; FlowValue Value; };
// JSON: "if": { "channel": "pendulum.energy", "op": "<", "value": 0.01 }      -> compares bus->GetNumber / GetBool / GetString
// "on": "when"   -> condition-only transition: evaluated once per OnUpdate AFTER the signal drain and BEFORE timers;
//                   at most one `when` transition fires per update (first in declaration order whose guard passes);
//                   Validate() reports a `when` transition without an `if`.
class FlowMachine {
    void SetDataBus(const DataBus* bus);                              // null => channel guards are false (warned once per guard)
    void StartAt(const FlowAsset& asset, const std::string& stateName); // Start(asset) == StartAt(asset, asset.Start); unknown name => Start
    static std::vector<std::string> KeySignals(const FlowAsset& asset); // distinct "key:<Name>" strings across all transitions
};
```
Save writes `"channel"` only when non-empty and `"on": "when"` verbatim; v1 files without these stay
byte-stable. `EvaluateFlowGuard` gains a `lookupChannel` callback parameter (default empty ⇒ false).

**Key bridge** (`scene/FlowKeyBridge.{h,cpp}`, engine; replaces the hand-rolled Escape edge in both hosts):

```cpp
class COSMIC_API FlowKeyBridge {
public:
    using Probe = std::function<bool(int keyCode)>;              // default: Input::IsKeyPressed
    void Bind(const FlowAsset& asset, Probe probe = {});          // resolves every KeySignals() name -> key code; unknown names warn once
    void Poll(FlowMachine& machine);                              // rising edge per bound key -> machine.FeedSignal("key:<Name>")
    static int KeyCodeFor(const std::string& name);               // Escape, Space, Enter, Tab, Backspace, Up, Down, Left, Right, F1..F12, A..Z, 0..9; -1 unknown
};
```

**Manifest key** `kind = "app" | "game" | "blank"` in `project.cproj` (default `game` when absent);
`ProjectManifest` (AP-03) reads and writes it; templates (AP-04) set it. The editor uses it for defaults
(auto-build ON for `app`).

## §6 Live logic loop (D-LIVE; AP-01 plumbing, AP-03 UX)

Sequence when a source file under `<project>/src/` changes while the editor is open:

1. `FileWatcher` event → debounce 500 ms, coalesce → if `AutoBuild` (default ON for `kind = "app"`,
   OFF otherwise; toolbar chip as today) and not already building → `BuildScripts()`.
2. If Play is active: remember `{ flowState = m_PlayFlow.CurrentState(), wasPlaying, wasPaused }`,
   `StopScene()` (scripts destroyed, **services destroyed, `m_PlayBus` untouched**), then build.
3. Build success → `ReloadModule` (snapshot while the old module is mapped, drop scene, unregister,
   `FreeLibrary`, load, `CosmicModule_Register`, rebuild the edit scene) → if `wasPlaying` and the editor
   pref `AutoResumePlay` (default true): `PlayScene()` with `m_PlayFlow.StartAt(asset, flowState)`; the bus
   keeps its values and history, so readouts and plots continue; services re-run `OnAttach` and may re-read
   the bus to recover state.
4. Build failure → stay stopped; the Console shows the compiler output; the status bar chip reads
   "Build failed" in red; the next successful build resumes as in step 3 if `wasPlaying` was set.
5. Status-bar chip states: `Live` (auto-build on, idle) · `Building…` · `Reloading` · `Build failed`.

What survives a swap: the bus (values, history, producers), the edit scene, the flow state name; the
selection and the undo stack are cleared (existing reload contract). What does not: service member
state, script member state, the physics world, ImGui panel state. Documented in
`docs/guide/app-authoring.md` (AP-D2) as "keep state you care about on the bus or in components".

## §7 Source links (D-LINKS; AP-01 records, AP-03 resolves + UI)

`Projects/Starforge/src/SourceLocator.{h,cpp}`:

```cpp
struct SourceHit { std::string Path; int Line = 0; std::string Reason; };   // Path empty => unresolved, Reason says why
class SourceLocator {
public:
    explicit SourceLocator(std::string projectRoot);
    SourceHit ForScriptClass(const std::string& className) const;   // scan <root>/src/**/*.h,*.cpp for "class <Name>" (first match)
    SourceHit ForService(const std::string& serviceName) const;     // ModuleRegistry::FindService()->File/Line, else scan like a class
    SourceHit ForPanel(const std::string& panelName, const PanelRegistry&) const;   // PanelRegistry::SourceOf
    SourceHit ForChannel(const std::string& channel, const DataBus&) const;         // Producer(channel) -> ForService
    std::vector<SourceHit> ForSignal(const std::string& signal) const;              // every src/** file containing the quoted signal string
    SourceHit ForScreen(const std::string& screenName) const;       // src/screens/<Name>Screen.h if it exists, else ForScriptClass
    static bool Open(const SourceHit& hit);      // ShellExecuteW "open" on the file (the OS default editor); false when unresolved
    static bool Reveal(const SourceHit& hit);    // explorer.exe /select,"<path>"
};
```
Placement (all AP-03): Inspector — `NativeScript` row gets **Open source** / **Reveal**; `UiHostedPanel`
row gets them via the play-time registry (edit mode: via the last registry seen, else "run Play once to
resolve"); bound-widget rows (`Channel` field) get **Open producer** when the play bus has a producer;
`UiButton.Signal` gets **Find handlers** (a popup listing hits, click opens). Screens panel: per screen
**Open script** / **Reveal** / **Create script**. DataBus panel: per channel the producer + **Open**.
Viewport: right-click on a selected element → context menu **Open logic source** (resolves in the
order: hosted panel → value/gauge/plot/indicator channel producer → button signal → the entity's
script). Unresolved items are disabled with the `Reason` as tooltip. Precedents:
`panels/ContentBrowserPanel.cpp:174` (`ShellExecuteW … explorer.exe`), `panels/HierarchyPanel.cpp:333`
(`BeginPopupContextItem`).

## §8 Template layout (AP-01 moves, AP-04 fills, AP-03 picks)

```
Projects/Starforge/assets/templates/          (synced POST_BUILD to assets/projects/Starforge/templates/)
├── game/        today's template, moved verbatim by AP-01; AP-04 makes scenes/Main.cscene a 2D scene
│                (sprite + ortho camera), keeps the 2D sample scripts, registers or deletes StoryUiBinding
├── app/         project.cproj (kind = "app", startup_flow = "flows/Main.cflow", fixed_dt_hz = 60)
│                flows/Main.cflow  (Home -> Dashboard -> Settings; key:Escape back; @quit from Home)
│                scenes/{Home,Dashboard,Settings}.cscene  (canvas + ortho camera + widgets, §3 names)
│                src/Module.cpp  (CS_SERVICE(AppService) + CS_SCREENS markers + the three screen scripts)
│                src/services/AppService.{h,cpp}  (publishes "app.uptime", "app.sine", "app.counter";
│                  handles "counter.increment"/"counter.reset"; CS_PANEL("Diagnostics") ImGui text)
│                src/screens/{Home,Dashboard,Settings}Screen.h  (from the §5 stub, lightly filled)
│                assets/ui/{panel.png,logo.png}  (small placeholder PNGs)   CMakeLists.txt (== game's)
├── blank/       project.cproj (kind = "blank"), scenes/Main.cscene (canvas + camera only),
│                src/Module.cpp (markers only), CMakeLists.txt
└── samples/
    ├── FlowDemo/   the tree BuildFlowDemo used to generate (kind = "game")
    └── ForgePong/  the tree BuildForgePong used to generate (kind = "game")
```
`StarforgeApp::ScaffoldProjectTo(name, destRoot, kind)` copies `templates/<kind>/` (or
`templates/samples/<Name>/`) replacing `@PROJECT_NAME@` in every file (binary files are copied byte-for-
byte — AP-04 keeps PNGs free of the token). Every template's `Module.cpp` carries the §5 markers. The
root scanner's target-name == directory-name rule (`CMakeLists.txt:194-202`) applies to in-tree copies.

## §9 3D purge scope (AP-05)

**Part A — delete** (all preserved on `engine-3d` / the tag):

| Group | Paths |
| --- | --- |
| Engine trees | `Cosmic/src/{terrain,voxel,water,nav,particles}/` |
| Engine files | `renderer/{Renderer3D,EnvironmentMap,ShadowMap,CoverageCapture,InstanceSet}.*`, `graphics/{Model,Skeleton,AnimationClip,CgltfImpl}.*`, `camera/NavigationCube.*`, `scene/{Scene3D.cpp,Components3D.h,SceneNav.*,ScenePicker.*,WorldSystemRecipes.*}`, `reflect/TypeRegistry3D.*`, `assets/MeshImport.*` (exactly the `list(FILTER)` block, `Cosmic/CMakeLists.txt:182-214`) |
| Vendored deps | `Cosmic/dependencies/recastnavigation/`, `Cosmic/dependencies/assimp/`, `Cosmic/dependencies/cgltf/` (verify no 2D consumer with grep before each) |
| Shaders/assets | engine shaders/assets referenced only by deleted code (grep `engine://` paths from the deleted TUs) |
| Tests | the 3D-only TU block `tests/CMakeLists.txt:140-170`, `tests/render/render_3d.cpp`, goldens referenced only by deleted TUs (`tests/render/goldens/`: verify by grep — expected `mesh_pbr`, `sky_ibl`, `terrain`, `water`, `particles`, the 3D `instancing.png`; **keep** `instancing2d.png` and every 2D golden) |
| Editor | `Projects/Starforge/src/panels/{WorldSystemsPanel,VoxelPanel}.*`, `editors/AnimationEditor.*` (the TUs already excluded at `Projects/Starforge/CMakeLists.txt:56-58`) |
| Template scripts | `Projects/Starforge/assets/templates/src/scripts/{VoxelDigger,NavCritter}.h` + their fenced blocks in `Module.cpp` + the `NavCritter` include/test in `tests/test_template_scripts.cpp` |
| Build/scripts | `build_3d.bat`, the "default" (3D) preset in `CMakePresets.json` (make `2d` the default), the `if(NOT COSMIC_2D_ONLY)` blocks in `Cosmic/CMakeLists.txt` (recast at `:112-113`, assimp at `:126`, links at `:264`, `:278-279`), the filter block itself |
| Docs manifest | every `docs/reference/README.md` row whose header was deleted (the checker's "stale row" mode enforces this) |
| Checker | `tests/check_docs_coverage.ps1`: remove the CMake-filter parse (`:89-112`, which exits 1 when no filter rules exist), the fence-aware `Cosmic.h` parse and the `³ᴰ`/`³ᴰ⁺` marker modes; keep everything else |

**Part B — unfence.** A Python script (`evidence/AP-05/unfence.py`, committed) rewrites every file
containing `COSMIC_2D_ONLY`: `#ifndef COSMIC_2D_ONLY … #endif` → dropped; `#ifndef COSMIC_2D_ONLY … #else …
#endif` → the else branch; `#ifdef COSMIC_2D_ONLY … #endif` → the body; `#if defined(COSMIC_2D_ONLY)` /
`#if !defined(COSMIC_2D_ONLY)` likewise. Nesting-aware (`#if*` depth stack); any `#elif` on a marked block
or a `COSMIC_2D_ONLY` in a compound condition is **left untouched and listed** for manual edit. CMake:
`if(COSMIC_2D_ONLY)`/`if(NOT …)` blocks are edited by hand (few). Keep `option(COSMIC_2D_ONLY … ON)` +
the reject-OFF gate (root `CMakeLists.txt:81-88`) + the PUBLIC define (`Cosmic/CMakeLists.txt:238`) as a
**compatibility no-op**, comment them as such, and keep `-DCOSMIC_2D_ONLY=ON` accepted everywhere.

**Oracle B06:** `grep -rn "COSMIC_2D_ONLY" Cosmic/src Projects tests` → zero `#if`/`#ifdef`/`#ifndef`/
`defined(` uses; none of the Part-A paths exist; `grep -rniE "Renderer3D|Terrain|Voxel|NavMesh|assimp|Recast|
EnvironmentMap|ShadowMap" Cosmic/src` → zero identifiers (comments that explain history may mention 3D
only under a "History:" note); both configs 0-warn; `CosmicTests` case count = pre-purge count minus the
deleted TUs' cases (record both numbers); every retained 2D golden byte-identical; both audits exit 0.

## §10 Parallel lanes (D-LANES) — file ownership matrix

"Owns" is exclusive. "May touch" is shared with a rebase expectation (keep both sides). Anything else is
reported, not edited. Concurrent lanes (same wave) never share an "Owns" path.

| WO | Owns | May touch |
| --- | --- | --- |
| AP-05 | everything in §9 Part A/B; `Cosmic/CMakeLists.txt`; root `CMakeLists.txt`; `tests/CMakeLists.txt`; `tests/render/CMakeLists.txt`; `Projects/Starforge/CMakeLists.txt`; `CMakePresets.json`; `build_*.bat`; `tests/check_docs_coverage.ps1`; `docs/reference/README.md` (row deletions); `Projects/Starforge/src/**` fence sites; `Cosmic/templates/ExampleProject/**` fence sites | — (runs alone on `main`; `.github/workflows/**` is AP-P1's) |
| AP-01 | `Cosmic/src/data/**`; `Cosmic/src/scripting/{AppService.h,ServiceHost.h,ServiceHost.cpp,ModuleRegistry.h,ModuleRegistry.cpp,ModuleMacros.h,ScriptableEntity.h,ScriptHost.h,ScriptHost.cpp}`; `Cosmic/src/scene/{FlowMachine.h,FlowMachine.cpp,FlowKeyBridge.h,FlowKeyBridge.cpp}`; `Cosmic/src/scene/ui/UiSystem.h` (signatures) + the matching no-op bodies in `UiSystem.cpp`; `Cosmic/src/layers/PlayerLayer.{h,cpp}`; `Cosmic/src/Cosmic.h`; `Projects/Starforge/src/StarforgeApp.{h,cpp}` (play/stop/tick/render/reload hooks and the `ScaffoldProjectTo` path only); `Projects/Starforge/assets/templates/**` (the move to `game/`); `Projects/Starforge/CMakeLists.txt` (sync path); `tests/test_databus.cpp`, `tests/test_servicehost.cpp`, `tests/test_flow_channels.cpp`; `tests/acceptance/manifests/ap01-*.json` | `tests/CMakeLists.txt` (add TUs); `docs/reference/README.md` (add rows for `data/DataBus.h`, `scripting/AppService.h`, `scripting/ServiceHost.h`, `scene/FlowKeyBridge.h`) |
| AP-P1 | `Runtime/CMakeLists.txt`; root `CMakeLists.txt` install rules only; `package.bat`, `package_installer.bat`; `installer/**`; `.github/workflows/**`; `Projects/Starforge/src/Packager.{h,cpp}`; `Projects/SF_Telem/CMakeLists.txt`; `Projects/SF_Telem/src/SF_Telem.cpp` (the writable-path lines only); `tests/acceptance/**` except other WOs' manifests/wrappers; `docs/plans/app-platform-2026-09-18/evidence/AP-P1/**`; this file §12 (user-data policy) | — |
| AP-02 | `Cosmic/src/scene/ui/{UiComponents.h,UiSystem.cpp}` (+ `UiSystem.h` below the AP-01 signatures); `Cosmic/src/reflect/TypeRegistry.cpp`; `tests/test_ui_widgets.cpp`; `tests/render/render_ap02_widgets.cpp`; `tests/render/goldens/ap02_*.png`; `tests/acceptance/manifests/ap02-*.json`; `tests/acceptance/fixtures/Run-AP02Render.ps1` | `tests/CMakeLists.txt`, `tests/render/CMakeLists.txt` (add TUs) |
| AP-04 | `Projects/Starforge/assets/templates/**` (except `_stubs`); `Projects/PendulumLab/**`; root `CMakeLists.txt` (skip list only); `tests/test_template_scripts.cpp`; `tests/test_pendulumlab.cpp`; `tests/fixtures/ap04/**`; `tests/acceptance/fixtures/Run-AP04Sample.ps1`; `tests/acceptance/manifests/ap04-*.json` | `tests/CMakeLists.txt` (add TUs, fixture define) |
| AP-D1 | `docs/**` except `docs/plans/app-platform-2026-09-18/{01-Design-Contracts.md,evidence/**}`; `README.md`; `tests/check_docs_links.ps1` (new); `tests/check_docs_coverage.ps1` (parked-dir allowance only) | `.github/workflows/ci.yml` (add one step); `docs/reference/README.md` (rewrite links of moved chapters) |
| AP-03 | `Projects/Starforge/src/**` except `Packager.*`, the existing `*SelfTest.cpp` and AP-01's hook lines; new TUs `UiRectGizmo.{h,cpp}`, `panels/ScreensPanel.{h,cpp}`, `panels/DataBusPanel.{h,cpp}`, `ScreenScaffold.{h,cpp}`, `SourceLocator.{h,cpp}`, `AP03AuthoringSelfTest.cpp`; `Projects/Starforge/assets/editor/stubs/ScreenScript.h.in`; `tests/acceptance/fixtures/Run-AP03Authoring.ps1`; `tests/acceptance/manifests/ap03-*.json` | `Projects/Starforge/CMakeLists.txt` (TU list) |
| AP-D2 | `docs/**`; `README.md` | `docs/reference/README.md` (rows) |
| AP-Q1 | `docs/plans/app-platform-2026-09-18/evidence/AP-Q1/**`; `docs/showcase/**`; `README.md` top strip; this file (§13 and recorded deviations); code only for registered KI fixes, each in its own commit | anything a KI fix needs, with the file named in the report |

Worktree protocol and land protocol: `work-orders/README.md` L1–L5.

## §11 Docs policy (AP-D1, AP-D2)

- **Archive tiers.** Completed/superseded plans → `docs/plans/archive/` (existing; keep its README's
  table, adding a **Commit** column: `| Doc | What it was | Outcome | Carried forward / replacement | Commit |`).
  Superseded design docs → `docs/archive/design/` with a README in the `docs/archive/README.md` shape
  (`| Doc | What it is | Superseded by |`). Archived docs are **warn-only** for the link checker.
- **Archived banner** (line 3 of every archived doc):
  `> **ARCHIVED 2026-09-18** — completed/superseded; kept as the record of what was built and why. Do not execute. Origin: <phase/date>. Landed by: <commit or "see git log">. Replacement: <link or "none">.`
- **Parked 3D.** `docs/parked-3d/{guide,reference,systems}/` + `docs/parked-3d/README.md` (index) +
  `docs/parked-3d/README-part2-3d-systems.md` (the root README Part II sections that describe 3D
  systems, moved verbatim). Every parked file gets, at line 3:
  `> **PARKED 3D — not on the trunk.** This chapter documents code that lives only on the \`engine-3d\` branch (\`0e8894b\`, tag \`cosmic-pre-2d-2026-09-16\`). The 2D trunk (\`main\`) no longer builds or ships it (D-PURGE, 2026-09-18). Kept for when 3D resumes.`
  Manifest rows that pointed at a moved chapter are rewritten to `../parked-3d/…` (rows may point
  outside `docs/reference/`, and headers deleted by AP-05 no longer have rows at all). A chapter is
  parked only when every header it documents was deleted; mixed chapters (`rendering-pipeline`,
  `cameras-navigation`) stay live and hand their 3D sections to a parked twin. A moved or trimmed
  chapter keeps its `STATUS: SKELETON` banner unchanged (strict mode otherwise engages). Live docs may
  link into `parked-3d/` only with the visible label "(parked 3D)". Supersession banners on retired
  prompts sit at line 3, like archived and parked banners (the three WO-11/12/13 banners follow this).
- **Link checker** `tests/check_docs_links.ps1` (AP-D1): scans `README.md`, `docs/**/*.md`,
  `Projects/*/README.md`, `Projects/*/docs/*.md`, `tests/**/*.md`; resolves relative file links and
  `#anchors` (GitHub slug rules: lowercase, spaces→`-`, strip punctuation except `-`/`_`); skips
  `http(s)://`, `mailto:`; **strict** (exit 1) for live tiers, **warn-only** under `docs/archive/**`,
  `docs/plans/archive/**`, `docs/parked-3d/**`, `docs/plans/2d-stability-2026-09-16/evidence/**`;
  PowerShell 5.1-safe, ASCII-only source; CI step after the two audits.
- **Roadmap v5** (`docs/plans/00-MASTER-ROADMAP.md`): shipped foundation (Phases 1–29, archived),
  the 2D stability campaign (done; WO-11/12/13 absorbed), the App Platform campaign (AP table with
  status), the deferred list from `00-Start-Here.md`, the v3 rule retained. v4 archived as
  `docs/plans/archive/00-MASTER-ROADMAP-v4.md`.
- **Stale-project sweep**: any live-tier mention of Frontier / Engine3DDemo / ForgeIsle / ViperSim /
  ForgePlayground / ForgeBlocks / `engine-2d` as a separate branch is rewritten to the trunk policy or
  moved to a dated "History" note. `docs/guide/README.md:50` (the exemplar list) becomes
  "the template projects, PendulumLab, AnalysisSample, SF_Telem".

## §12 Writable user data (AP-P1; extends the stability packet's contracts.md)

An installed app must never write under its install directory. `project://logs` and relative
`recordings/` in `Projects/SF_Telem/src/SF_Telem.cpp` (see `:62`) resolve to `user://logs` and
`user://recordings/SF_Telem` respectively; the packaged identity comes from `boot.cfg`
(`Runtime/Main.cpp:68-83`, `:100-101`), so two shipped apps never share a user root. The single package
layout is the editor Packager's (`<App>.exe` = renamed `CosmicApp.exe`, `Cosmic.dll`, `<App>.dll`,
`assets/`, `assets/projects/<App>/`, `boot.cfg`, `user/` placeholder); `package.bat` and `release.yml`
produce the same tree (AP-P1 proves byte-equivalent file lists). AP-P1 records the final policy text here.

## §13 New-surface register (AP sessions append; AP-Q1 finalizes)

| Surface | Contract | Class | Proven by | Status |
| --- | --- | --- | --- | --- |
| DataBus | §1 | new | V01 | planned |
| AppService / ServiceHost / CS_SERVICE / CS_PANEL | §2 | new | V02, V05 | planned |
| Data() script proxy | §2 | new | V06 | planned |
| Bound widgets (7 components) | §3 | new | V03, V04, E05 | planned |
| Hosted panels (host draw) | §4 | new | V05 | planned |
| Flow channel guards / when / StartAt / key bridge | §5 | new | V06, F02 | planned |
| Screens panel + screen scripts + scaffold | §5 | new | E02, F01 | planned |
| Live loop (auto-build → reload → resume) | §6 | new | E07 | planned |
| Source links | §7 | new | E08 | planned |
| Template kinds + samples on disk | §8 | new/changed | E01, E06 | planned |
| PendulumLab | §8 | new | F02, Y01–Y03 | planned |
| 3D purge | §9 | removal | B06 | planned |
| Packaging identity + writable user data | §12 | changed | K01–K04 | planned |
| Acceptance in CI | — | new | H05 | planned |
| Docs archive / parked-3d / link checker | §11 | changed | DOC01, DOC03–DOC05 | planned |
| DataBus (`data/DataBus.{h,cpp}`; `DataValue::AsBool` added) | §1 | new | V01 (13 cases, both configs) | landed (AP-01) |
| AppService / PanelRegistry / ServiceHost / CS_SERVICE / CS_PANEL; `ModuleRegistry` AddService/FindService/ServiceNames + UnregisterModule stripping (`ServiceDescriptor` lives in `ModuleRegistry.h`) | §2 | new | V02 U (9 cases) + V02 W (20 GameModule reloads in-exe, 20 PlayerLayer reloads through the Application) | landed (AP-01); V05 pending AP-02/AP-03 |
| Data() script proxy (`ScriptHost::SetDataBus`, `DataProxy` on ScriptableEntity + SystemScript) | §2 | new | V06 (Data proxy case) | landed (AP-01) |
| Flow channel guards / `when` / `StartAt` / `KeySignals` / `SetDataBus` / `EvaluateFlowGuard(lookupChannel)`; `scene/FlowKeyBridge.{h,cpp}` | §5 | new | V06 (9 flow cases; v1/v2 save bytes pinned against the AP-05B binary) | landed (AP-01); F02 pending AP-04 |
| UiSystem signatures: `DataBus*` on Update, `bus`/`preview` on both Render overloads, `UiHostedPanelDraw`, `CollectHostedPanels` (no-op body) | §3 | new | existing UI suites unchanged; bodies proven by V03–V05 | landed (AP-01, signatures only) |
| Host wiring: PlayerLayer (`m_Bus`/`m_Panels`/`m_Services`/`m_KeyBridge`, §2 frame order, §4 hosted-panel block, key bridge replaces the Escape edge) and StarforgeApp Play (`m_PlayBus`/`m_PlayPanels`/`m_PlayServices`/`m_PlayKeyBridge`, `StarforgeAppServices.cpp`) | §2, §4 | new | V02 W (PlayerLayer path); wo09-editor C05 (editor Play/Stop unchanged); editor hosted-panel draw pending AP-03 (V05) | landed (AP-01) |
| Template layout: `assets/templates/game/` (moved verbatim), `ScaffoldProjectTo(name, dest, kind = "game")`, `ProjectManifest::Kind` (read only) | §8, §5 | changed | wo09-editor C05 (scaffold + Play through the moved template) | landed (AP-01); app/blank/samples pending AP-04, `kind` write + picker pending AP-03 |

## §14 Anchors (revalidated 2026-09-18 at `8da533c`; re-check before editing)

- `Cosmic/src/scene/ui/UiComponents.h:68-209` (components), `UiSystem.h:100-130` (API)
- `Cosmic/src/scene/FlowMachine.h:84-92` (guard), `:109-117` (transition), `:169-236` (machine);
  `FlowMachine.cpp:90` (parser), `:217-224` (version write), `:312` (path resolve), `:393` (CompareValue), `:704` (timer)
- `Cosmic/src/scene/Scene.h:388-396` (`Events()`, `SetActiveFlow/ActiveFlow`), `:443`
- `Cosmic/src/scene/EventBus.h:34-72`
- `Cosmic/src/scripting/ModuleRegistry.h:41-60`, `:79-111`, `:137`; `ModuleMacros.h:56-106`;
  `ScriptableEntity.h:67-74` (sink), `:101-115` (telemetry proxy), `:203-247` (signal/flow proxies), `:415-436` (SystemScript);
  `ScriptHost.h:48-127`
- `Cosmic/src/layers/PlayerLayer.h:46-89`; `PlayerLayer.cpp:54` OnAttach, `:120-137` flow start, `:164` OnDetach,
  `:191` RebindScripts, `:218` OnUpdate (`:239-262` UI-then-flow, `:246-249` Escape edge), `:280` UpdateUI,
  `:346` RenderScene (`:377-380` overlay → `UiSystem::Render`), `:406` OnImGuiRender
- `Projects/Starforge/src/StarforgeApp.cpp:313` ScaffoldProjectTo, `:341` ScaffoldProject, `:348` NewProjectAt,
  `:472` BuildScripts, `:497` ReloadModule, `:616` PlayScene, `:732` StopScene, `:774` TickPlay,
  `:1144-1150` auto-build poll, `:1193` RenderViewport, `:2107-2108` AutoBuild chip, `:2886` BuildFlowDemo,
  `:3061` BuildForgePong, `:3777` DrawHomescreen, `:3884` per-frame LoadProjects, `:3927-3978` New Project modal
  (`:3944-3969` the decorative template combo), `:3751` explorer precedent; `StarforgeApp.h:132-142` PlayMode,
  `:295-299` play-flow members, `:302-308` module/build members
- `Projects/Starforge/src/EditorPrefs.h:65`, `:121-158`; `ProjectManifest.h:18-71`; `GameModule.h:23-53`
- `Projects/Starforge/src/panels/ContentBrowserPanel.cpp:174`; `panels/HierarchyPanel.cpp:333`;
  `editors/FlowEditor.cpp:143-168` (signal scan), `:562-583` (scene picker), `:625` (action combo), `:730-741` (On), `:752-776` (To)
- `Cosmic/src/reflect/TypeRegistry.cpp:200-234` (UI registrations)
- `Cosmic/src/math/Integrators.h:35` `IntegrateRK4`, `:68` `FixedSubstepper`
- `Cosmic/CMakeLists.txt:20`, `:112-113`, `:126`, `:165`, `:182-214`, `:238`, `:264`, `:278-279`;
  root `CMakeLists.txt:67`, `:81-88`, `:142-152`, `:163-173`, `:176-208`
- `tests/CMakeLists.txt:9-135` (shared TUs), `:126-134` (SF_Telem TUs), `:140-170` (3D-only TUs), `:172`;
  `tests/render/CMakeLists.txt:18`; `Projects/Starforge/CMakeLists.txt:16-18` (/bigobj), `:56-58`, `:62-79`, `:148-155`
- `tests/check_docs_coverage.ps1:38-53`, `:67-68`, `:89-112`; `tests/check_gl_conformance.ps1:29`, `:34`
- `Runtime/CMakeLists.txt:8`, `:50-67`, `:79`; `Runtime/Main.cpp:39-46`, `:68-83`, `:100-101`;
  `Projects/Starforge/src/Packager.cpp:43-120`; `package.bat:50`, `:57-62`, `:100-113`;
  `.github/workflows/ci.yml:28-39`, `:65`, `:94-108`, `:113-125`; `release.yml:28`, `:41-65`
- `docs/reference/README.md:129-275` (manifest, 148 rows after AP-00), `:101-105`; `docs/plans/archive/README.md:8-11`;
  `docs/archive/README.md:8`; `docs/guide/README.md:26-44`, `:50`; root `README.md:9-20`, `:311-343`
- KI register template: `docs/plans/2d-stability-2026-09-16/contracts/known-issues.md:11-20`
