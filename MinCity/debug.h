#pragma once
#include "betterenums.h"

// define all debug variables here ..,,,
BETTER_ENUM(DebugLabel, uint32_t const,
	CAMERA_FRACTIONAL_OFFSET,
	PUSH_CONSTANT_VECTOR,
	RAMP_CONTROL_BYTE,
	TOGGLE_1_BOOL,
	TOGGLE_2_BOOL,
	TOGGLE_3_BOOL,
	PERFORMANCE_VOXEL_SUBMISSION,
	HOVERVOXEL_US,
	QUERY_VOXELINDEX_PIXMAP_US,
	WORLD_ORIGIN,
	WORLD_ORIGIN_LOCKED
);

#if !defined(NDEBUG)
#ifndef DEBUG_OPTIONS_USED
#define DEBUG_OPTIONS_USED
#endif
namespace Debug
{
	// put your global access functions for debugging here //
	extern size_t const getFrameCount();

} // end ns

#endif

#if (!defined(NDEBUG) || defined(ALLOW_DEBUG_VARIABLES_ANY_BUILD))
#define DEBUG_VARIABLES_ENABLED

#if !defined(NDEBUG) // assert_print only available in debug builds only
#include <Utility/assert_print.h>
#endif

// ************* DEBUG VARIABLES **************************************************************************************************/
// does not matter if a different local static variable will be referenced afterwards, can be done
// can also be initialized to some global static
// Debug variables cannot be local variables. There must be memory backing the variable, and the lifetime of
// that variable must be infinite. If referencing and object variable and that object is deleted, the reference will be broken
// for the debug variable, and any further usage of the debug variable is undefined and could result in a runtime error.
// *********************************************************************************************************************************/
extern void const* _debugMap[DebugLabel::_size()];	// not actually constant uses some hacky trickery

	template<typename type>
	static __inline type const _getDebugVariable_Debug(DebugLabel const label) {
#if !defined(NDEBUG) // assert_print only available in debug builds only
		assert_print(nullptr != _debugMap[label], fmt::format("Debug Variable : {:s} : Not Initialized, setDebugVariable() must be called before getDebugVariable()", label._to_string()));
#endif
		return(*reinterpret_cast<type const* const>(_debugMap[label]));
	}
#define getDebugVariable(type, label) _getDebugVariable_Debug<type>(label)

	template<typename type>
	static __inline type& _getDebugVariableReference_Debug(DebugLabel const label) {
#if !defined(NDEBUG) // assert_print only available in debug builds only
		assert_print(nullptr != _debugMap[label], fmt::format("Debug Variable : {:s} : Not Initialized, setDebugVariable() must be called before getDebugVariable()", label._to_string()));
#endif
		return(const_cast<type&>(*reinterpret_cast<type const* const>(_debugMap[label])));
	}
#define getDebugVariableReference(type, label) _getDebugVariableReference_Debug<type>(label)

	// ******VALUE MUST BE A STATIC VARIABLE********, and THIS FUNCTION MUST BE CALLED B4 GETDEBUGVARIABLE
	template<typename type>
	static __inline type const _setDebugVariable_Debug(DebugLabel const label, type const& value) {		

		_debugMap[label] = reinterpret_cast<void const* const>(&value);

		return(*reinterpret_cast<type const* const>(_debugMap[label]));
	}
#define setDebugVariable(type, label, value) _setDebugVariable_Debug<type>(label, value)

	extern void InitializeDebugVariables();	// implemented by includer

#else

	template<typename type>
	static __inline type const _getDebugVariable_ReleaseHack(DebugLabel const label) {
		[[maybe_unused]] type zero = {};
		return(zero);
	}
#define getDebugVariable(type, ignored) (void)_getDebugVariable_ReleaseHack<type>(ignored)

	template<typename type>
	static __inline type& _getDebugVariableReference_ReleaseHack(DebugLabel const label) {
		[[maybe_unused]] type zero = {};
		return(zero);
	}
#define getDebugVariableReference(type, ignored) (void)_getDebugVariableReference_ReleaseHack<type>(ignored)

	// VALUE MUST BE A STATIC VARIABLE, and THIS FUNCTION MUST BE CALLED B4 GETDEBUGVARIABLE
	template<typename type>
	static __inline type const _setDebugVariable_ReleaseHack(DebugLabel const label, type const& value) {
		[[maybe_unused]] type zero = {};
		return(zero);
	}
#define setDebugVariable(type, ignored_label, ignored) (void)_setDebugVariable_ReleaseHack<type>(ignored_label, ignored)


#endif





