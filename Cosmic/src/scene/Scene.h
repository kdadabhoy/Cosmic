#pragma once
// Scene.h

#include "core/Core.h"
#include "core/UUID.h"
#include "scene/System.h"
#include "scene/EventBus.h"          // U2 — per-scene signal channel (by value)
#include "jobs/ParallelSystem.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Cosmic
{
	class Entity;          // Forward declaration
	class Camera;             // Forward declaration (OnRender3D takes any camera)
	class OrthographicCamera; // Forward declaration
	class Material;        // Forward declaration (used only as a bucket key pointer)
	struct SceneRenderDesc;      // renderer/SceneRenderer.h (H2 — BuildRenderDesc fills it)
	class  SceneDrawContext;     // renderer/SceneRenderer.h (H2 — routed opaque submit)
	struct EnvironmentComponent; // scene/Components.h (E4 — FindEnvironment returns it)
	struct AnimatorComponent;    // scene/Components.h (A2 — FindAnimatorFor returns it)
	class  PhysicsWorld;         // physics/PhysicsWorld.h (J4 — a play-session service)
	class  ScenePhysics;         // physics/ScenePhysics.h (J4 — runtime body binding)
	class  SceneNavRuntime;      // scene/SceneNav.h        (N4 — play-session agent/crowd binding)
	class  ScriptHost;           // scripting/ScriptHost.h  (J5 — collision-event dispatch)
	class  FlowMachine;          // scene/FlowMachine.h     (Q2 — Flow() variable proxy link)

	class COSMIC_API Scene
	{
	public:
		Scene();
		~Scene();   // out-of-line: m_Physics is a unique_ptr to a forward-declared type

		/** @brief Static factory helper to match unified engine smart pointer instantiation rules. */
		template<typename... Args>
		static Ref<Scene> Create(Args&&... args)
		{
			return std::make_shared<Scene>(std::forward<Args>(args)...);
		}

		/** @brief Instantiates a blank Entity handle bound to this scene instance.
		 *  Emplaces a Transform, a Tag, and an IDComponent with a fresh UUID (E2). */
		Entity CreateEntity(const std::string& name = "GenericEntity");

		/** @brief As CreateEntity, but with a caller-supplied UUID — used by the
		 *  SceneSerializer so loaded entities keep their stable identity. If the
		 *  UUID is 0/invalid a fresh one is generated. */
		Entity CreateEntityWithUUID(UUID id, const std::string& name = "GenericEntity");

		/** @brief Resolve a UUID to its Entity handle, or an invalid handle if the
		 *  scene holds no entity with that id (E2). O(1) via the id index. */
		Entity FindByUUID(UUID id);

		/** @brief Destroys an entity (E3). By default its whole subtree is
		 *  destroyed too; pass destroyChildren=false to orphan the children
		 *  (their Parent link is cleared) instead. Always detaches the entity
		 *  from its own parent's Children list. Flat entities (no
		 *  RelationshipComponent) behave exactly as before. */
		void DestroyEntity(Entity entity, bool destroyChildren = true);

		// --- Hierarchy (E3) ---------------------------------------------------

		/** @brief The SUPPORTED hierarchy depth (WO-09 / KI-42, ratified in the 2D
		 *  stability numeric-bar policy): every walker — the parent chain
		 *  (GetWorldTransform, IsAncestor, IsActiveInHierarchy) and the children
		 *  walks (DestroyEntity, UiSystem::CollectElements, SceneSerializer::
		 *  SavePrefab) — visits at most this many nodes along one path (self +
		 *  4,095 ancestors / descendants). Deeper data is not an error: a walk stops
		 *  at the ceiling (and warns once) instead of overflowing the stack, and a
		 *  malformed cycle (RelationshipComponent authored by hand) is entered once
		 *  and left. Data the editor and the serializer produce (SetParent refuses
		 *  cycles) is always well inside it. */
		static constexpr int kMaxHierarchyDepth = 4096;

		/** @brief Re-parent `child` under `parent` (pass an invalid parent to
		 *  detach to root). When keepWorldPose is true the child's local
		 *  transform is rewritten so its WORLD pose does not change. Refuses (and
		 *  warns, returns false) if it would create a cycle. Children order is
		 *  the append order of SetParent calls. */
		bool SetParent(Entity child, Entity parent, bool keepWorldPose = true);

		/** @brief World-space transform = parent-world x local, walking the
		 *  parent chain. For an entity without a RelationshipComponent this is
		 *  just its local TransformComponent — so flat scenes are unchanged. */
		glm::mat4 GetWorldTransform(Entity entity);

		/** @brief True if `ancestor` appears anywhere on `node`'s parent chain. */
		bool IsAncestor(Entity ancestor, Entity node);

		/** @brief Effective-active (T13): the entity's own TagComponent::Active AND
		 *  every ancestor's. An inactive entity (or one under an inactive ancestor)
		 *  is not rendered, ticked, or baked. Walks the parent chain like WorldOf;
		 *  entities without a TagComponent are treated as active. */
		bool IsActiveInHierarchy(entt::entity handle);
		bool IsActiveInHierarchy(Entity entity);

		/** @brief Runs ongoing frame logic updates across all registered systems. */
		void OnUpdate(float deltaTime);

		/** @brief Runs fixed time-step physics/simulation routines across systems. */
		void OnFixedUpdate(float fixedDeltaTime);

		// --- Physics session (Phase 15 / J4) ---------------------------------
		// Bodies exist only while a simulation session runs; edit mode holds none.
		// The PhysicsWorld is owned by the session (Starforge play mode / PlayerLayer)
		// and passed in. Fixed-step contract (per fixed step, in order):
		//   scripts OnFixedUpdate -> OnPhysicsStep -> DispatchPhysicsEvents.

		/** @brief Build bodies + character controllers from the scene's components
		 *  (uses world transforms). Call once when a play session starts. */
		void OnPhysicsStart(PhysicsWorld& world);

		/** @brief Advance physics one fixed step: push kinematic targets, step the
		 *  world, write dynamic transforms back, update characters. No-op if no
		 *  session is active. */
		void OnPhysicsStep(float fixedDeltaTime);

		/** @brief Drain queued contact events and fire the OnCollision / OnTrigger
		 *  script callbacks (J5). Call after OnPhysicsStep each fixed step. */
		void DispatchPhysicsEvents(ScriptHost& scripts);

		/** @brief Destroy every body/character and end the session. */
		void OnPhysicsStop(PhysicsWorld& world);

		/** @brief The active physics runtime binding, or nullptr in edit mode. Used
		 *  by ScriptableEntity::Physics()/Character() to reach a body/controller. */
		ScenePhysics* GetPhysics() { return m_Physics.get(); }

		/**
		 * @brief Calls BeginScene with the provided camera, dispatches all sprite-bearing
		 * entities to Renderer2D (grouped by material bucket to minimise draw call overhead),
		 * then calls EndScene.
		 *
		 * Callers must NOT wrap this in their own BeginScene/EndScene — the scene owns the
		 * full render pass so that the camera and viewport state are always correct.
		 *
		 * Entities within each material bucket are sorted ascending by Position.z before
		 * drawing to guarantee correct depth order. This sort is O(n log n) per bucket per
		 * frame, where n is the number of entities sharing one material. Cost is negligible
		 * for typical bucket sizes (< ~1000). If a single material bucket becomes very large,
		 * consider a dirty-flag skip (only sort when z-values changed) or a pre-sorted
		 * container updated incrementally rather than re-sorted every frame.
		 *
		 * @param camera  The orthographic camera whose View-Projection matrix will be used
		 *                for this render pass.
		 */
		void OnRender(const OrthographicCamera& camera);

		/**
		 * @brief Advance flipbook sprite animations (U4). For every entity with a
		 * SpriteAnimationComponent + SpriteRendererComponent, accumulates time and
		 * writes the current frame's UV into the sprite's SourceRect (resolving the
		 * sheet through AssetLibrary for its pixel size). Framerate-independent.
		 * Main-thread (texture resolve). No-op for scenes without sprite animations
		 * (the compat gate). Call once per variable tick before the 2D render.
		 */
		void UpdateSpriteAnimations(float deltaTime);

		/**
		 * @brief One entry of the 2D painter list built by BuildSpriteDrawList.
		 * `E` is the entity, `Z` its ZOrder, `Key` the within-layer sort key
		 * (sprites: YSort ? -Position.y : Position.z; tilemaps: Position.z) and
		 * `Map` distinguishes a tilemap from a sprite.
		 */
		struct SpriteDrawItem { entt::entity E; int32_t Z; float Key; bool Map; };

		/**
		 * @brief The painter-ordered 2D draw list: every enabled, effectively-active
		 * sprite and tilemap, sorted ascending by (ZOrder, per-item key, entity id).
		 * Sprites sort by -Position.y when YSort is set (lower on screen draws in
		 * front), otherwise by Position.z (the legacy 2D-path tie-break); tilemaps
		 * interleave with sprites through the same keys. Pure CPU — no GL, no
		 * asset resolution — so it is headless-testable; OnRenderSprites calls it
		 * to decide draw order and this is the only place that order is decided.
		 */
		std::vector<SpriteDrawItem> BuildSpriteDrawList();

		/**
		 * @brief Generic world-space 2D pass (U3/U4): draws every entity with a
		 * TransformComponent + SpriteRendererComponent, and every tilemap
		 * (TransformComponent + TilemapComponent — a camera-rect-culled cell
		 * walk, one cell = one world unit), through Renderer2D under the given
		 * view-projection, painter-ordered ascending by (ZOrder, then
		 * per-sprite key = YSort ? -Position.y : Position.z, then entity id).
		 * Runs INSIDE the main scene pass — depth test stays ON so 3D geometry
		 * occludes sprites in 2.5D scenes, depth writes go OFF, straight alpha —
		 * the same contract as the transparent queue (no second compositor).
		 * Call from a SceneRenderDesc::DrawTransparent hook (HDR still bound) or
		 * after OnRender3D. Textures resolve lazily from TexturePath; a sprite
		 * with neither texture nor material draws its flat Color. Sprites use the
		 * RAW TransformComponent (position/Rotation.z/scale — no hierarchy
		 * compounding), matching the legacy 2D path. COMPAT GATE: a scene with no
		 * sprites returns before any GL call, so existing 3D apps are untouched.
		 * Main-thread / GL.
		 */
		void OnRenderSprites(const glm::mat4& viewProjection,
		                     uint32_t viewportWidth, uint32_t viewportHeight);

		/**
		 * @brief 2D lighting composite (X5 / gap §12.1): accumulate every active
		 * Light2DComponent into a half-res HDR buffer (cleared to the environment's
		 * Ambient2D, default white) and MULTIPLY it over the bound target, darkening
		 * the 2D scene between lights. Call right AFTER OnRenderSprites, same target.
		 * COMPAT GATE: no lights + white ambient returns before any GL call, so 2D
		 * scenes without lights are byte-identical. Uses the RAW TransformComponent
		 * XY (the legacy 2D-path convention). Main-thread / GL.
		 */
		void OnRender2DLights(const glm::mat4& viewProjection,
		                      uint32_t viewportWidth, uint32_t viewportHeight);

		/**
		 * @brief Fill a SceneRenderDesc from this scene (H2) so SceneRenderer — the
		 * engine's HDR/post orchestrator — is THE editor + player render path:
		 * advances the world clock and fills the camera + time fields the passes
		 * read. The CALLER then sets the clear color and, when FindEnvironment() is
		 * non-null, applies it via SceneRenderer::ApplyEnvironment before Render().
		 * Generic — no editor concepts leak in. Main-thread.
		 *
		 * History: the 3D engine defined this twice (W6) — the gather in
		 * scene/Scene3D.cpp also collected lights, terrain, water, particle emitters
		 * and the routed DrawOpaque submit; AP-05 purged it and this 2D twin is the
		 * only definition left.
		 */
		void BuildRenderDesc(const Camera& camera, float deltaTime, SceneRenderDesc& out);

		/**
		 * @brief The scene's authored EnvironmentComponent (E4), or nullptr — the
		 * single "Environment" entity's component. The editor/player feed it to
		 * SceneRenderer::ApplyEnvironment; a scene without one renders on the engine
		 * defaults (flat clear, no sky/IBL) exactly like before H2.
		 */
		EnvironmentComponent* FindEnvironment();

		/** @brief Allocates and attaches an execution system to the scene lifecycle. */
		template<typename T, typename... Args>
		T& AddSystem(Args&&... args)
		{
			auto system = CreateScope<T>(std::forward<Args>(args)...);
			T& ref = *system;
			if (auto* ps = dynamic_cast<ParallelSystem*>(system.get()))
				m_ParallelSystems.push_back(ps);
			m_Systems.push_back(std::move(system));
			return ref;
		}

		/** @brief Clears out all systems bound to this scene instance. */
		void RemoveAllSystems()
		{
			// Destroy owned systems first (runs their destructors), then clear the
			// non-owning parallel pointer list. This order ensures that if a future
			// ParallelSystem destructor ever inspects m_ParallelSystems (e.g. to
			// unregister itself), the list is still intact when the destructor runs.
			m_Systems.clear();
			m_ParallelSystems.clear();
		}

		/** @brief Safe multi-component layout querying mechanism for external or client layers. */
		template<typename... Components>
		auto View()
		{
			return m_Registry.view<Components...>();
		}

		inline entt::registry& GetRegistry() { return m_Registry; }
		inline const entt::registry& GetRegistry() const { return m_Registry; }

		/** @brief The scene's signal channel (U2). Buttons emit onto it, the
		 *  FlowMachine + scripts subscribe. Empty by default (no-op for shipped
		 *  apps that never touch UI/flow). */
		inline EventBus& Events() { return m_Events; }
		inline const EventBus& Events() const { return m_Events; }

		/** @brief The FlowMachine currently driving this scene, or null (Q2). A
		 *  running FlowMachine points its top scene here so scripts can reach the
		 *  flow blackboard via Flow().GetVar/SetVar; cleared when the flow moves on
		 *  or stops. Not owned. */
		inline void         SetActiveFlow(FlowMachine* flow) { m_ActiveFlow = flow; }
		inline FlowMachine* ActiveFlow() const { return m_ActiveFlow; }


	public:
		/**
		 * @brief Linear search for a registered system of type T.
		 *
		 * WARNING — O(n) per call: iterates the full system list and performs a
		 * dynamic_cast on each entry. Must NOT be called per-frame. Cache the
		 * returned pointer in your layer's OnAttach and reuse it every tick.
		 *
		 * @return Pointer to the first system of type T, or nullptr if none is found.
		 */
		template<typename T>
		T* GetSystem()
		{
			for (auto& system : m_Systems)
			{
				// Safely attempt to downcast the base System pointer to your requested subclass
				T* target = dynamic_cast<T*>(system.get());
				if (target)
					return target;
			}
			return nullptr;
		}

	private:
		/** @brief Recursive world-transform walk keyed by entt handle (E3). */
		glm::mat4 WorldOf(entt::entity handle);

		float m_WorldTime = 0.0f;   // accumulated seconds for water/particle animation (E18)

		EventBus m_Events;          // U2 — per-scene signal channel
		FlowMachine* m_ActiveFlow = nullptr;   // Q2 — the flow driving this scene (not owned)

		std::unique_ptr<ScenePhysics>    m_Physics;      // J4 — non-null only during a play session

		entt::registry m_Registry;
		std::vector<Scope<System>>   m_Systems;
		std::vector<ParallelSystem*> m_ParallelSystems; // non-owning; owned by m_Systems

		// UUID -> handle index, kept in sync by CreateEntity*/DestroyEntity so
		// FindByUUID (parent/EntityRef/prefab resolution) is O(1). Entities added
		// straight to the registry (not via CreateEntity*) aren't indexed.
		std::unordered_map<UUID, entt::entity> m_UUIDMap;

		// Persistent scratch buffers for OnRender's material-bucket sort. Reused every
		// frame (cleared, not reallocated) to avoid per-frame heap churn. Inner vectors
		// retain their capacity across frames; empty buckets are skipped during dispatch.
		std::unordered_map<Material*, std::vector<entt::entity>> m_RenderMaterialBuckets;
		std::vector<entt::entity>                                m_RenderFlatColorBucket;

		friend class Entity; // Gives access to the registry mapping internals securely
	};
}