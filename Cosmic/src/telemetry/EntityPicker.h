#pragma once

// EntityPicker.h
// Last Modified: 5/29/2026

/**
 * @brief Header-only CPU bounding-box picking utility.
 *
 * EntityPicker::Pick iterates every entity that has BOTH TransformComponent
 * and SelectableComponent and tests the world-space point against the entity's
 * 2D AABB (Position ± Scale/2). Returns the first hit Entity, or an invalid
 * Entity{} if nothing was hit.
 *
 * EntityPicker::ScreenToWorld converts a screen-space pixel coordinate to a
 * world-space 2D point using the camera's inverse view-projection matrix.
 * Screen Y is flipped to match OpenGL NDC conventions (origin bottom-left).
 *
 * Both functions are static and this class is never instantiated. COSMIC_API
 * is intentionally omitted because the class is header-only.
 */

#include "core/Core.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SelectableComponent.h"
#include "camera/OrthographicCamera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <cmath>

namespace Cosmic
{
    class EntityPicker
    {
    public:
        // -------------------------------------------------------------------------
        // Screen → World unprojection
        // -------------------------------------------------------------------------

        /**
         * @brief Convert a screen-space pixel position to a 2D world-space point.
         *
         * Uses the inverse of the camera's view-projection matrix. Screen Y is
         * flipped so that pixel (0,0) at the top-left maps to NDC Y = +1 (top
         * of the OpenGL clip volume).
         *
         * @param camera        The active orthographic camera.
         * @param screenPos     Pixel coordinates — (0,0) = top-left of the viewport.
         * @param viewportSize  Viewport dimensions in pixels (width, height).
         * @return              World-space XY position on the near plane (Z = 0).
         */
        static glm::vec2 ScreenToWorld(
            const OrthographicCamera& camera,
            glm::vec2                 screenPos,
            glm::vec2                 viewportSize)
        {
            // Map pixel → NDC in [-1, 1]. Y is flipped: screen top = NDC +1.
            float ndcX =  (screenPos.x / viewportSize.x) * 2.0f - 1.0f;
            float ndcY =  1.0f - (screenPos.y / viewportSize.y) * 2.0f;

            glm::vec4 ndc   { ndcX, ndcY, 0.0f, 1.0f };
            glm::mat4 invVP = glm::inverse(camera.GetViewProjectionMatrix());
            glm::vec4 world = invVP * ndc;

            // Perspective divide (always 1 for ortho, but kept for correctness).
            if (world.w != 0.0f)
                world /= world.w;

            return { world.x, world.y };
        }

        // -------------------------------------------------------------------------
        // AABB hit test
        // -------------------------------------------------------------------------

        /**
         * @brief Return the first entity under worldPos, or an invalid Entity{}.
         *
         * Iterates all entities with BOTH TransformComponent and SelectableComponent.
         * The hit box is the entity's Scale-sized rectangle, rotated by Rotation.z:
         * the query point is transformed into the entity's local frame before the
         * axis-aligned test, so rotated entities pick exactly on their visual bounds.
         * Z position is ignored — this is strictly 2D picking.
         *
         * @param scene     The scene to search.
         * @param worldPos  2D world-space query point (from ScreenToWorld).
         * @param filter    Optional extra predicate — return false to reject an
         *                  otherwise-hit entity (e.g. "only Agents", "skip locked").
         *                  Null means accept all entities with SelectableComponent.
         * @return          First entity whose (rotated) box contains worldPos and
         *                  passes the filter, or Entity{}.
         */
        static Entity Pick(
            const Ref<Scene>& scene,
            glm::vec2         worldPos,
            const std::function<bool(Entity)>& filter = nullptr)
        {
            auto view = scene->View<TransformComponent, SelectableComponent>();

            for (auto rawEntity : view)
            {
                const auto& transform = view.get<TransformComponent>(rawEntity);

                float halfW = transform.Scale.x * 0.5f;
                float halfH = transform.Scale.y * 0.5f;

                // Rotate the query point into the entity's local frame by the
                // inverse of its 2D rotation. Rotation is stored in DEGREES
                // (GetTransform() converts with glm::radians — mirror that here).
                const float ang = glm::radians(-transform.Rotation.z);
                const float c   = std::cos(ang);
                const float s   = std::sin(ang);
                const float dx  = worldPos.x - transform.Position.x;
                const float dy  = worldPos.y - transform.Position.y;
                const float localX = c * dx - s * dy;
                const float localY = s * dx + c * dy;

                bool hitX = glm::abs(localX) <= halfW;
                bool hitY = glm::abs(localY) <= halfH;

                if (hitX && hitY)
                {
                    Entity candidate{ rawEntity, scene.get() };
                    if (!filter || filter(candidate))
                        return candidate;
                }
            }

            return Entity{};
        }

    private:
        EntityPicker() = delete;
    };

} // namespace Cosmic
