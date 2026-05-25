#pragma once

// Last Modified 5/24/2026

/**
*  Needs to be Updated
* 
* 
* 
 * General Description:
 * Core.h serves as the fundamental definition layer for the Cosmic Engine. It is
 * designed to be the first inclusion in nearly every engine file, providing
 * universal macros, platform detection, and memory management abstractions.
 *
 * This file establishes the "language" of the engine, defining how we handle
 * debugging (Asserts), event registration (Binding), and bitwise flags for
 * systems like the Event Nervous System.
 *
 * Memory Management (Scope & Ref):
 * To maintain clean and readable code, Cosmic wraps standard smart pointers:
 * - Scope<T>: An alias for std::unique_ptr. Used for strict, single ownership
 *             (e.g., the Window, Application, or VertexArray).
 * - Ref<T>:   An alias for std::shared_ptr. Used for shared resources that multiple
 *             objects may point to simultaneously (e.g., Shaders and Textures).
 *
 * Platform Detection:
 * Currently specialized for Windows (x64), this section ensures the engine is
 * running on a supported environment, which is critical for RendererAPI
 * implementations and platform-specific hardware calls.
 */

#include <memory>


namespace Cosmic
{




	////////////////////////////////
	// Platform Detection
	///////////////////////////////
#ifdef _WIN32
	#ifdef _WIN64
		#define COSMIC_PLATFORM_WINDOWS
	#else
		#error "x86 Builds are not supported! Please switch to x64."
	#endif
#endif


////////////////////////////////
// DLL Export/Import Macros
///////////////////////////////
#ifdef COSMIC_PLATFORM_WINDOWS
    #ifdef COSMIC_BUILD_DLL
        // Compiling Cosmic.dll: Export symbols to create Cosmic.lib
        #define COSMIC_API __declspec(dllexport)
    #else
        // Compiling DinoProject.dll / Runtime App: Hook into Cosmic.dll
        #define COSMIC_API __declspec(dllimport)
    #endif
#else
    #define COSMIC_API
#endif






////////////////////////////////
	// Debugging & Asserts
	///////////////////////////////
#if defined(GLCORE_DEBUG) || defined(CS_DEBUG)
#define CS_ENABLE_ASSERTS
#endif

	/**
	 * CS_ASSERT / GLCORE_ASSERT
	 * Verifies conditions during evaluation. If 'x' returns false, the engine outputs
	 * a localized core error via the logger stream subsystem before forcing a debug halt.
	 */
#ifdef CS_ENABLE_ASSERTS
	// We use an expansion technique so that any file including Core.h knows the macro signature,
	// but the actual call to Log occurs downstream without requiring a tight #include "Log.h" dependency here.
	#define CS_ASSERT(x, ...)     { if(!(x)) { CS_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
	#define GLCORE_ASSERT(x, ...) { if(!(x)) { CS_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
	#define CS_ASSERT(x, ...)
	#define GLCORE_ASSERT(x, ...)
#endif





	 ////////////////////////////////
	 // Universal Macros
	 ///////////////////////////////
	 /**
	  * BIT(x)
	  * Converts an integer into a bitwise flag (1 shifted left by x).
	  * Primarily used in Event.h to define overlapping Event Categories.
	  */
#define BIT(x) (1 << x)




	  /**
	   * CS_BIND_EVENT_FN(fn)
	   * A helper macro to simplify binding class member functions to the Event system.
	   * Prefer modern C++20 lambda syntax in client-facing code over this macro:
	   * dispatcher.Dispatch<Event>([this](auto& e) { return OnEvent(e); });
	   */
#define CS_BIND_EVENT_FN(fn)     std::bind(&fn, this, std::placeholders::_1)
#define GLCORE_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)








	   ////////////////////////////////
	   // Smart Pointer Aliases
	   ///////////////////////////////

	   /**
		* Scope<T>
		* Alias for unique ownership. When a Scope goes out of context,
		* the memory is automatically freed.
		*/
	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}



		/**
		 * Ref<T>
		 * Alias for shared ownership. The resource is kept alive as long as
		* at least one Ref points to it.
		*/
	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}