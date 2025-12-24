#pragma once

#include <memory>

namespace Cosmic
{
	// --- Platform Detection ---
#ifdef _WIN32
#ifdef _WIN64
#define COSMIC_PLATFORM_WINDOWS
#else
#error "x86 Builds are not supported!"
#endif
#endif

// --- Debugging and Asserts ---
#ifdef GLCORE_DEBUG
#define GLCORE_ENABLE_ASSERTS
#endif

#ifdef GLCORE_ENABLE_ASSERTS
#define GLCORE_ASSERT(x, ...) { if(!(x)) { /* Add your logger error call here */; __debugbreak(); } }
#else
#define GLCORE_ASSERT(x, ...)
#endif

// --- Bit Manipulation ---
#define BIT(x) (1 << x)

// --- Event Binding Helper ---
#define GLCORE_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

// --- Smart Pointer Aliases ---
// Use Scope for unique ownership (e.g., the Window or a Buffer Layout)
	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	// Use Ref for shared ownership (e.g., Shaders, Textures, Vertex Buffers)
	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}