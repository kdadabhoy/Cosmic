#pragma once

// Core.h
// Last Modified 5/14/2026

/**
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
	#ifdef COSMIC_DYNAMIC_LINK
		#ifdef COSMIC_BUILD_DLL
			#define COSMIC_API __declspec(dllexport)
		#else
			#define COSMIC_API __declspec(dllimport)
		#endif
	#else
		// Since we are compiling an EXE but allowing companion DLLs to link into it,
		// we force the executable to export its engine functions, generating Cosmic.lib!
		#define COSMIC_API __declspec(dllexport)
	#endif
#else
	#define COSMIC_API
#endif





	////////////////////////////////
	// Debugging & Asserts
	///////////////////////////////

#ifdef GLCORE_DEBUG
#define GLCORE_ENABLE_ASSERTS
#endif




	/**
	 * GLCORE_ASSERT
	 * A development tool used to verify assumptions in the code. If the condition 'x'
	 * fails, the engine will trigger a debug break, allowing the developer to
	 * inspect the call stack.
	 */
#ifdef GLCORE_ENABLE_ASSERTS
#define GLCORE_ASSERT(x, ...) { if(!(x)) { /* TODO: Add Logger Error Call */; __debugbreak(); } }
#else
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
	   * GLCORE_BIND_EVENT_FN(fn)
	   * A helper macro to simplify binding class member functions to the Event system.
	   * It handles the 'std::bind' syntax and 'this' pointer placement automatically.
	   */
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