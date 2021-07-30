#ifndef PCH_H
#define PCH_H

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
#endif

#include "targetver.h"

#define NOMINMAX
#define _ITERATOR_DEBUG_LEVEL 0
#define _SILENCE_CXX17_UNCAUGHT_EXCEPTION_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

// not using, want random seed at startup
//#define PROGRAM_KEY_SEED (0xC70CC609)
//#define DETERMINISTIC_KEY_SEED PROGRAM_KEY_SEED 

// (0)
#include <windows.h>
#include <minwindef.h>
#include <tchar.h>

// **** header include dependendies on order here are important:

// (1)
#include <tbb/tbb.h>
#include "types.h"

// (2)
#include "Globals.h"

// (3)

// (4)
#include <Math/superfastmath.h>
#include <Math/DirectXCollision.aligned.h>
// (5)
#include "optimized.h"

// (6)
#include <string_view>
#include <fmt/fmt.h>

#ifndef NDEBUG
#include <Utility/assert_print.h>
#endif

#endif //PCH_H


 