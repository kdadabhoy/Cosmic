#pragma once

// SelectableComponent.h
// Last Modified: 5/29/2026

/**
 * @brief Empty tag component that marks an entity as clickable by EntityPicker.
 *
 * Add this component to any entity that should respond to CPU bounding-box
 * picking via EntityPicker::Pick. Carries no data — its presence in the
 * registry is the signal.
 *
 * Registration:
 *   CS_REGISTER_COMPONENT(Cosmic::SelectableComponent) is called in this
 *   header so all TUs that include it share the same stable EnTT type hash.
 *   This is required for components that cross the DLL boundary.
 */

#include "scene/ComponentRegistry.h"

namespace Cosmic
{
    struct SelectableComponent {};

} // namespace Cosmic

// Stable cross-DLL type ID — must appear at global scope after the struct definition.
CS_REGISTER_COMPONENT(Cosmic::SelectableComponent)
