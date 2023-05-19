#pragma once
#include <stdint.h>

#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

#include "betterenums.h"

#ifndef GREEN_ANSI
#define GREEN_ANSI "\x1b[32m"
#endif
#ifndef BLUE_ANSI 
#define BLUE_ANSI "\x1b[34m"
#endif
#ifndef WHITE_ANSI
#define WHITE_ANSI "\x1b[37m"
#endif
#ifndef ANSI_OFF
#define ANSI_OFF "\x1b[0m"
#endif
#ifndef INVERSE_ANSI 
#define INVERSE_ANSI "\x1b[7m"
#endif
#ifndef INVERSE_ANSI_OFF 
#define INVERSE_ANSI_OFF "\x1b[27m"
#endif

#ifndef NDEBUG			// Debug TESTS //

//#define VKU_VMA_DEBUG_ENABLED
//#define LIVESHADER_MODE
//#define DEBUG_VOLUMETRIC
//#define DEBUG_ALIGNMENT_TERRAIN
#define DEBUG_SHOW_GUI_WINDOW_BORDERS
#define DEBUG_DISABLE_MUSIC
//#define DEBUG_TRAFFIC
//#define DEBUG_OUTPUT_STREAMING_STATS
#define DEBUG_VOXEL_BANDWIDTH
//#define DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#define DEBUG_VOXEL_RENDER_COUNTS
//#define DEBUG_EXPORT_TERRAIN_KTX
//#define DEBUG_EXPORT_BLUENOISE_KTX
//#define DEBUG_EXPORT_BLUENOISE_DUAL_CHANNEL_KTX
//#define DEBUG_EXPORT_BLACKBODY_KTX
//#define DEBUG_EXPORT_NOISEMIX_KTX
//#define DEBUG_EXPORT_SAVE_IMAGE_THUMBNAILS
// 
//#define DEBUG_HIGHLIGHT_BOUNDING_RECTS
//#define DEBUG_NO_RENDER_STATIC_MODELS
//#define DEBUG_EXPLOSION_COUNT
//#define DEBUG_TORNADO_COUNT
//#define DEBUG_SHOCKWAVE_COUNT
//#define DEBUG_FLAT_GROUND
//#define DEBUG_ASSERT_JFA_SEED_INDICES_OK // good validation, state is setup at runtime so this is a good dynamic test.
//#define DEBUG_DEPTH_CUBE
//#define DEBUG_MOUSE_HOVER_VOXEL
#define DEBUG_FPS_WINDOW
//#define DEBUG_LUT_WINDOW
#define VOX_DEBUG_ENABLED
//#define DEBUG_EXPLOSION_WINDOW
//#define DEBUG_DISALLOW_RENDER_DISABLING	// note that this causes any swapchain recreation to fail, this is not normally on in release
#define FULLSCREEN_EXCLUSIVE

#if defined(DEBUG_VOLUMETRIC)
#define DEBUG_STORAGE_BUFFER
#endif

#define DEBUG_DISALLOW_PAUSE_FOCUS_LOST

#ifdef LIVESHADER_MODE
//#define DEBUG_DISALLOW_RENDER_DISABLING	// note that this causes any swapchain recreation to fail, this is not normally on in release
#endif

extern void debug_out_nuklear(std::string const message); 
extern void debug_out_nuklear_off();
#define FMT_LOG_DEBUG(message, ...) {  (fmt::print(fg(fmt::color::red), "[DEBUG] " message "\n", __VA_ARGS__)); }
#define FMT_NUKLEAR_DEBUG(bLog, message, ...) { if ( bLog ) FMT_LOG_DEBUG( message, __VA_ARGS__ ); debug_out_nuklear( fmt::format(message, __VA_ARGS__) ); }
#define FMT_NUKLEAR_DEBUG_OFF() { debug_out_nuklear_off(); }

#else // **** must add to global detection macro below aswell **** //
//#define SECURE_DYNAMIC_CODE_NOT_ALLOWED
#define FULLSCREEN_EXCLUSIVE
#define DEBUG_DISABLE_MUSIC
#define DEBUG_CONSOLE
#define VOX_DEBUG_ENABLED
//#define DEBUG_EXPLOSION_WINDOW
//#define DEBUG_DEPTH_CUBE
//#define DEBUG_PERFORMANCE_VOXEL_SUBMISSION		// all debug performance defines are mutually exclusive, ie.) only one of them should be enabled at any given time/build
//#define DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP
//#define DEBUG_OUTPUT_STREAMING_STATS
#define DEBUG_VOXEL_BANDWIDTH

#define FMT_LOG_DEBUG(message, ...) //{ (void)message; (void)__VA_ARGS__; }
#define FMT_NUKLEAR_DEBUG(bLog, message, ...) //{ (void)bLog; (void)message; (void)__VA_ARGS__; }
#define FMT_NUKLEAR_DEBUG_OFF()

#endif // NDEBUG

// **** global macro **** warning if debug options are enabled during release mode
#if defined(DEBUG_DISABLE_MUSIC) \
	|| defined(DEBUG_DEPTH_CUBE)					\
	|| defined(DEBUG_CONSOLE)						\
    || defined(VOX_DEBUG_ENABLED)					\
	|| defined(DEBUG_PERFORMANCE_VOXEL_SUBMISSION)	\
	|| defined(DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP) \
    || defined(DEBUG_OUTPUT_STREAMING_STATS) \
    || defined(DEBUG_VOXEL_BANDWIDTH) \
    || defined(TRACY_ENABLE) \

#ifndef DEBUG_OPTIONS_USED
#define DEBUG_OPTIONS_USED
#define DEBUG_FPS_WINDOW		// this window is always on if defined for <debug> *or* <release with debug options used>

#endif

#endif // **** global macro **** warning

#if defined(DEBUG_PERFORMANCE_VOXEL_SUBMISSION) \
	|| defined(DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP)

#define ALLOW_DEBUG_VARIABLES_ANY_BUILD

#endif
#include "debug.h" // debug variables, NDEBUG HANDLING inside

#define FMT_LOG(category, message, ...) {  (fmt::print(fg(fmt::color::white),  ("[{:s}] " ANSI_OFF message "\n"), category, __VA_ARGS__)); }
#define FMT_LOG_OK(category, message, ...) {  (fmt::print(fg(fmt::color::white),  ("[{:s}] " ANSI_OFF message GREEN_ANSI " ok.\n"), category, __VA_ARGS__)); }
#define FMT_LOG_WARN(category, message, ...) {  (fmt::print(fg(fmt::color::orange),  ("[{:s}] " message "\n"), category, __VA_ARGS__)); }
#define FMT_LOG_FAIL(category, message, ...) {  (fmt::print(fg(fmt::color::red),  (INVERSE_ANSI"[FAIL] " INVERSE_ANSI_OFF" [{:s}] " message "\n"), category, __VA_ARGS__)); }
#define FMT_BOOL_SZ(boolean) ((boolean) ? "true" : "false")
// log catergories //
#define NULL_LOG ""
#define INFO_LOG "INFO"
#define GAME_LOG "GAME"
#define GPU_LOG "GPU"
#define VOX_LOG "VOX"
#define TEX_LOG "TEX"
#define AUDIO_LOG "AUDIO"

// helper types
#define read_only constexpr extern const __declspec(selectany)
#define read_only_no_constexpr extern const __declspec(selectany)

// relative paths //
namespace Globals
{
#define _S * const 
	static constexpr char const 
		_S TITLE = "minCity";

// folders in mincity main application folder //
#define DATA_DIR L"data/"
#define BIN_DIR L"\\data\\bin\\" // bugfix, needs to be \\ for this one (secure dll loading)
#define FONT_DIR DATA_DIR L"fonts/"
#define SHADER_BINARY_DIR DATA_DIR L"shaders/binary/"
#define TEXTURE_DIR DATA_DIR L"textures/"
#define GIF_DIR TEXTURE_DIR L"gifs/"
#define GUI_DIR TEXTURE_DIR L"gui/"
#define VOX_DIR DATA_DIR L"vox/"
#define VOX_CACHE_DIR VOX_DIR L"cached/"
#define AUDIO_DIR DATA_DIR L"audio/"

// folders in .mincity user folder //
#define USER_DIR L".mincity/"
#define VIRTUAL_DIR USER_DIR L"virtual/"
#define SAVE_DIR USER_DIR L"saves/"


	
#ifndef NDEBUG
#define DEBUG_DIR DATA_DIR L"debug/"
#endif
// ####################################################################################################################################################//
	static constexpr uint32_t const DEFAULT_STACK_SIZE = (1 << 19);	// in bytes, 512KB for any tbb thread. main thread uses program defined stack size (default 1MB))   *bugfix - stack overflow encountered @ 256KB, increased to 512KB

	static constexpr uint32_t const DEFAULT_SCREEN_WIDTH = 1920,	// 16:9 default, should not be used directly
									DEFAULT_SCREEN_HEIGHT = 1080;  
	static constexpr float const DEFAULT_ANISOTROPIC_LEVEL = 8.0f;

	static constexpr float const INTERPOLATION_TIME_SCALAR = 1.0f; // for slow-motion, or a view of high precision motion, use a really low value

	static constexpr float const DEFAULT_ZOOM_SCALAR = 2.5f * SFM::GOLDEN_RATIO,				// controls "zoom" higher values are farther away
								 MAX_ZOOM_FACTOR = 0.5f,
#ifndef NDEBUG
								 MIN_ZOOM_FACTOR = DEFAULT_ZOOM_SCALAR * 4.0f, // allow zoom out in debug builds
#else
								 MIN_ZOOM_FACTOR = DEFAULT_ZOOM_SCALAR * 4.0f, // fits the bounded volume in release builds
#endif
								 ZOOM_SPEED = 0.44f; // see Iso::CAMERA_SCROLL_DISTANCE_MULTIPLIER for edge scrolling speed
	
							// Parallel Projections have a magnitude greater range in precision. Orthographic projection has nearly infinite accuracy compared to perspective projection. *Do not optimize these values further* The high depth buffer precision is supported, when coupled with a 32bit depth buffer.
	static constexpr double const MINZ_DEPTH = (0.111111 * SFM::GOLDEN_RATIO);			// Tweaked Z Range, don't change, type purposely double
	static constexpr double const MAXZ_DEPTH = 9.0 * (111.111111 * SFM::GOLDEN_RATIO);	// remember orthographic projection makes the distribution of z values linear - best precision possible
											/* DO NOT CHANGE, PERFECT RAYMARCH PRECISION */	// **** this affects clipping of the raymarch "unit cube", do not change values

	static constexpr uint32_t const INTERVAL_GUI_UPDATE = 16;	 // 16ms = 60fps maximum gui update interval when no input is flagging the gui to be updated (set for minimum latency)
	
	static constexpr 
		uint32_t const MIN_BLACK_LVL_EMISSIVE = 8;

	static constexpr uint32_t const NK_MAX_VERTEX_BUFFER_SZ = 256 * 1024,
									NK_MAX_INDEX_BUFFER_SZ = 64 * 1024;

	static constexpr int32_t const
		UNLOADED = 0,
		PENDING = -1,
		LOADED = 1;

} // end ns Globals

#define IsUnloaded(state) (0 == state)
#define IsPending(state) (state < 0)
#define IsLoaded(state) (state > 0)

#define SAFE_DELETE(p)				{ if(p) { delete (p);     (p)=nullptr; } }
#define SAFE_DELETE_ARRAY(p)		{ if(p) { delete[] (p);   (p)=nullptr; } }
#define SAFE_RELEASE_DELETE(p)      { if(p) { (p)->release(); delete (p); (p)=nullptr; } }

#define QI(i)  (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :
#define QI2(i) (riid == IID_##i) ? GetInterface((i*)this, ppv) :

template <typename T>
inline void SafeRelease(T& p)
{
	if (nullptr != p)
	{
		p.Release();
		p = nullptr;
	}
}

#define HR(x) if(FAILED(x)) { return(x); }

#include "tTime.h"

// global accessors for time, implemented in mincity.h
extern __forceinline tTime const& start();
extern __forceinline tTime const& now();
extern __forceinline nanoseconds const& delta();

extern __forceinline tTime const& critical_now();
extern __forceinline constexpr nanoseconds const& critical_delta();
extern __forceinline size_t const frame();
