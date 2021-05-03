#include "pch.h"
#include "debug.h"
#include "MinCity.h"

#ifdef DEBUG_VARIABLES_ENABLED

void const* _debugMap[DebugLabel::_size()] = {};

#endif

#ifndef NDEBUG

namespace Debug
{
	// put your global access functions for debugging here //

	size_t const getFrameCount() {
		return(MinCity::getFrameCount());
	}

} // end ns


#endif

