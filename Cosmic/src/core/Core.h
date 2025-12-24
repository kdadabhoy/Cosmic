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





/*	Documentation:

	The purpose of Core.h is to act as a fundamental definition layer.
	- It should be the first thing included by almost every other file in the engine
	- It provides universal macros and other helpful stuff (platform detection)


	Memory Management (Re-naming unique and shared ptrs)
	*** Literally just use these in place of unique_ptr and shared_ptr..
		Like genuinely it is just to keep the code cleaner

	- Scope is used for things that should only have one owner
		- Ex: Window and Application and VertexArray and BufferLayout
	- Ref is for resources that multiple objects might need at the same time.
		- Ex: Shaders and Textures


	The Platform detection basically checks if the engine is loaded on windows...
	- This is needed for RedererAPI stuff


	The BIT Macro
	- Provides bitwise flags
	- Used in Event.h to help define categories
	

	Asserts are also defined in this
	- COSMIC_CORE_ASSERT should be defined... it is a good debug tool








*/