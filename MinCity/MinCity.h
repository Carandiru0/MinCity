#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#include <Utility/class_helper.h>
#include "tTime.h"
#include <Math/point2D_t.h>

// event types //
BETTER_ENUM(eEvent, uint32_t const,

	NOT_USED = 0,
	PAUSE,
	PAUSE_PROGRESS,
	REVERT_EXCLUSIVITY,
	REFRESH_LOADLIST,
	SAVE,
	AUTO_SAVE,
	LOAD,
	NEW,

	QUIT			   = (UINT32_MAX - 1),
	EXPEDITED_SHUTDOWN = (QUIT + 1)

);

BETTER_ENUM(eExclusivity, uint32_t const,

	MAIN = 0,
	SAVING = (1 << 0),
	LOADING = (1 << 1)

);

// forward declarations //
namespace world {
	class cVoxelWorld;
	class cProcedural;
} // end ns world

namespace std {
	namespace filesystem {
		class path;
	}
} // end ns

// special global functions outside of any class (declarations) //
namespace Volumetric {
	extern bool const isGraduallyStartingUp();
} // end ns
// ---------------------------------------------------------- //

class no_vtable cMinCity : no_copy
{
	static constexpr size_t const // for optimizing physical memory usage of this app
		PROCESS_MIN_WORKING_SET = (1ULL << 30ULL),  // 1GB
		PROCESS_MAX_WORKING_SET = (1ULL << 33ULL);  // 8GB
public:
	// Common Accessors // 
	static size_t const					getFrameCount() { return(m_frameCount); }

	static __forceinline tTime const&					start() { return(m_tStart); }
	static __forceinline tTime const&					now() { return(m_tNow); }
	static __forceinline tTime const&					critical_now() { return(m_tCriticalNow); }	// time that continues even while paused

	static point2D_t const __vectorcall	getFramebufferSize();
	static float const					getFramebufferAspect();

	static __inline bool const			isRunning() { return(m_bRunning); }
	static __inline bool const			isPaused() { return(m_bPaused); }
	static __inline bool const			isFocused() { return(m_bFocused); }
	static __inline bool const			isGraduallyStartingUp() { return(m_bGradualStartingUp); }

	static __inline uint32_t const		getExclusivity() { return(m_eExclusivity); }

	NO_INLINE static std::filesystem::path const getUserFolder();

	static __inline std::string_view const getCityName() { return(m_szCityName); }
	static __inline void setCityName(std::string_view const& szCityName) { m_szCityName = szCityName; }

	// Main Init //
	__declspec(noinline) static void CriticalInit();
	__declspec(noinline) static void CriticalCleanup();

	static bool const Initialize(struct GLFWwindow*& glfwwindow);

	// Main Update & Render //
	static void UpdateWorld();
	static void StageResources(uint32_t const resource_index);

	static void Render();
	static bool const DispatchEvent(uint32_t const eventType, void* const data = nullptr); // returns true when event added, false when event was discarded
	static void ClearEvents(bool const bDisableNewEventsAfter = false);

	// Update/Render Specific //
	static void Pause(bool const bStateIsPaused);
	static void Load();
	static void Save(bool const bShutdownAfter = false);
	static int32_t const Quit(bool const bQueryStateOnly = false); // prompts user to quit, returns user selection (eWindowQuit)
	
	// Callbacks / Events //
	static void OnFocusLost();
	static void OnFocusRestored();

	// Tidy memory //
	__declspec(noinline) static void Cleanup(struct GLFWwindow* const glfwwindow);
private:
	// Private Init //
	__declspec(noinline) static HANDLE const SetupEnvironment(); // for main thread only

	static void LoadINI();
	static bool const Shutdown(int32_t const action, bool const expedite = false); // returns true if MinCity will be shutdown
	static void ProcessEvents();
	static void OnNew();
	static void OnLoad();
	static void OnSave(bool const bShutdownAfter);
/* #####################################_S I N G L E T O N S_######################################### */
public:
	static class cVulkan					Vulkan;
	static class cTextureBoy				TextureBoy;
	static class cPostProcess				PostProcess;
	static class cNuklear					Nuklear;
	static class world::cVoxelWorld			VoxelWorld;
	static class world::cProcedural			Procedural;
	static class cUserInterface				UserInterface;
	static class cAudio						Audio;
	static class cCity*						City;	// any access of City outside of this class is promised this pointer
													// is never null during runtime (after initialization)
/* ################################################################################################### */

private:
	static uint32_t						m_eExclusivity; // *only safe to set from main thread*
	static size_t						m_frameCount;
	static tTime const					m_tStart;
	static tTime						m_tNow, m_tCriticalNow, m_tLastPause;
	static std::atomic_bool				m_bNewEventsAllowed;
	static bool							m_bRunning,
										m_bPaused,
										m_bFocused,
										m_bGradualStartingUp;

	static std::string					m_szCityName;
	// Event container:
	static tbb::concurrent_queue<std::pair<uint32_t, void*>> m_events;
};
#define MinCity cMinCity

// global helper functions for time, used for includee's of globals.h they are defined, hrmmmm
__forceinline tTime const& start() { return(cMinCity::start()); }
__forceinline tTime const& now() { return(cMinCity::now()); }
__forceinline tTime const& critical_now() { return(cMinCity::critical_now()); }	// time that continues even while paused
__forceinline size_t const frame() { return(cMinCity::getFrameCount()); }

#ifdef MINCITY_IMPLEMENTATION

inline cVulkan				cMinCity::Vulkan;
inline cTextureBoy			cMinCity::TextureBoy;
inline cPostProcess			cMinCity::PostProcess;
inline cNuklear				cMinCity::Nuklear;
inline world::cVoxelWorld	cMinCity::VoxelWorld;
inline world::cProcedural	cMinCity::Procedural;
inline cUserInterface		cMinCity::UserInterface;
inline cAudio				cMinCity::Audio;

inline uint32_t					cMinCity::m_eExclusivity{}; // *only safe to set from main thread*
inline size_t					cMinCity::m_frameCount = 0;
inline tTime const				cMinCity::m_tStart{ high_resolution_clock::now() }; // always valid time
inline tTime					cMinCity::m_tNow{ cMinCity::m_tStart }; // always valid time
inline tTime					cMinCity::m_tCriticalNow{ cMinCity::m_tStart }; // always valid time that continues even while paused
inline tTime					cMinCity::m_tLastPause{ zero_time_point };
inline std::atomic_bool			cMinCity::m_bNewEventsAllowed = false;
inline bool						cMinCity::m_bRunning = false;		// state is set in cMinCity::Initialize on Success
inline bool						cMinCity::m_bPaused = false;		// state refers to a "live pause" of the rendering and updates affected by the pause
inline bool						cMinCity::m_bFocused = false;		// start time of app is safetly swapped, so at no given point of time is it errornous
inline bool						cMinCity::m_bGradualStartingUp = true;	// start time of app will reset during very first call of Update()
inline std::string				cMinCity::m_szCityName = "";
inline cCity*					cMinCity::City(nullptr);
inline tbb::concurrent_queue<std::pair<uint32_t, void*>> cMinCity::m_events;

// Common Accessors // 
point2D_t const __vectorcall	cMinCity::getFramebufferSize() { return(Nuklear.getFramebufferSize()); }
float const						cMinCity::getFramebufferAspect() { return(Nuklear.getFramebufferAspect()); }


// special global functions outside of any namespace or class (definitions) //
namespace Volumetric {
	extern bool const isGraduallyStartingUp() { return(MinCity::isGraduallyStartingUp()); }
} // end ns
// ---------------------------------------------------------- //
#endif

