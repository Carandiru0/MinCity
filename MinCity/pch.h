#ifndef PCH_H
#define PCH_H

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
#endif

#define NOMINMAX
#define NOGDI
#define NOGDICAPMASKS //- CC_*, LC_*, PC_*, CP_*, TC_*, RC_
#define NOMENUS //- MF_ *
#define NOSYSCOMMANDS //- SC_ *
#define NORASTEROPS //- Binary and Tertiary raster ops
#define OEMRESOURCE //- OEM Resource values
#define NOATOM //- Atom Manager routines
#define NOCLIPBOARD //- Clipboard routines
#define NOCOLOR //- Screen colors
#define NODRAWTEXT //- DrawText() and DT_ *
#define NOKERNEL //- All KERNEL defines and routines
#define NOMB -// MB_ * andMessageBox()
#define NOMEMMGR //- GMEM_*, LMEM_*, GHND, LHND, associated routines
#define NOMETAFILE //- typedef METAFILEPICT
#define NOOPENFILE //- OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#define NOSCROLL //- SB_ * andscrolling routines
#define NOSERVICE //- All Service Controller routines, SERVICE_ equates, etc.
#define NOSOUND //- Sound driver routines
#define NOTEXTMETRIC //- typedef TEXTMETRICand associated routines
#define NOWH //- SetWindowsHook and WH_ *
#define NOWINOFFSETS //- GWL_*, GCL_*, associated routines
#define NOCOMM //- COMM driver routines
#define NOKANJI //- Kanji support stuff.
#define NOHELP //- Help engine interface.
#define NOPROFILER //- Profiler interface.
#define NODEFERWINDOWPOS //- DeferWindowPos routines
#define NOMCX //- Modem Configuration Extensions

#define _SECURE_SCL 0 
#define _ITERATOR_DEBUG_LEVEL 0
#define _SILENCE_CXX17_UNCAUGHT_EXCEPTION_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "targetver.h"

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
#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

// (6)
#include <string_view>
#include <fmt/fmt.h>

#ifndef NDEBUG
#include <Utility/assert_print.h>
#endif

#endif //PCH_H


 