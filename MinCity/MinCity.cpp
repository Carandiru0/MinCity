/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#include "pch.h"
#include <combaseapi.h>
#include <timeapi.h>
#include <oleacc.h>
#include <ShlObj.h>
#include <initguid.h>
#include <filesystem>

#include "globals.h"

#define RANDOM_IMPLEMENTATION
#define NOISE_IMPLEMENTATION
#include <Random/superrandom.hpp>
#include <Noise/supernoise.hpp>
#include <Math/superfastmath.h>

#include "cVulkan.h"
#include "cTextureBoy.h"
#include "cPostProcess.h"
#include "cNuklear.h"
#include "cVoxelWorld.h"
#include "cProcedural.h"
#include "cUserInterface.h"
#include "cAudio.h"
#include "cCity.h"

#define ASYNC_LONG_TASK_IMPLEMENTATION
#include <Utility/async_long_task.h>

// ^^^^ SINGLETON INCLUDES ^^^^ // b4 MinCity.h include

#define MINCITY_IMPLEMENTATION
#include "MinCity.h"

#include "RedirectIO.h"

// private global variables //
static inline tbb::task_scheduler_init* TASK_INIT{ nullptr };
static inline GLFWwindow* g_glfwwindow(nullptr);

#ifdef DEBUG_VARIABLES_ENABLED

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#include "performance.h"
#endif

// Initialization of any debug variables here
// does not matter if a different local static variable will be referenced afterwards, can be done
// can also be initialized to some global static
// Debug variables cannot be local variables. There must be memory backing the variable, and the lifetime of
// that variable must be infinite. If referencing and object variable and that object is deleted, the reference will be broken
// for the debug variable, and any further usage of the debug variable is undefined and could result in a runtime error.

static void InitializeDebugVariables()
{
	static XMVECTOR xmZero = XMVectorZero();
	static uint8_t uZero = 0;
	static bool bZero = false;

		
	setDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET, xmZero);
	setDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR, xmZero);
	setDebugVariable(uint8_t, DebugLabel::RAMP_CONTROL_BYTE, uZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_1_BOOL, bZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_2_BOOL, bZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_3_BOOL, bZero);

#ifdef DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP
	static microseconds tZero = {};
	setDebugVariable(microseconds, DebugLabel::HOVERVOXEL_US, tZero);
	setDebugVariable(microseconds, DebugLabel::QUERY_VOXELINDEX_PIXMAP_US, tZero);
#endif

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
	static PerformanceResult PerformanceCounters;
	setDebugVariable(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION, PerformanceCounters);
#endif
}

#endif

void cMinCity::LoadINI()
{
	wchar_t const* const szINIFile(L"./MinCity.ini");

	int32_t const iResolutionWidth = GetPrivateProfileInt(L"RENDER_SETTINGS", L"RESOLUTION_WIDTH", Globals::DEFAULT_SCREEN_WIDTH, szINIFile);
	int32_t const iResolutionHeight = GetPrivateProfileInt(L"RENDER_SETTINGS", L"RESOLUTION_HEIGHT", Globals::DEFAULT_SCREEN_HEIGHT, szINIFile);

	if (iResolutionWidth > 0 && iResolutionHeight > 0 && iResolutionWidth <= 9999 && iResolutionHeight <= 9999) {
		Nuklear.setFrameBufferSize(iResolutionWidth, iResolutionHeight);
	}
	else {
		Nuklear.setFrameBufferSize(Globals::DEFAULT_SCREEN_WIDTH, Globals::DEFAULT_SCREEN_HEIGHT);
	}

	bool const bFullscreenExclusive = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"FULLSCREEN_EXCLUSIVE", TRUE, szINIFile);
	Vulkan.setFullScreenExclusiveEnabled(bFullscreenExclusive);

	bool const bVsyncEnabled = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"VSYNC", TRUE, szINIFile);
	Vulkan.setVsyncDisabled(!bVsyncEnabled);

	bool const bDPIAware = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"DPI_AWARE", TRUE, szINIFile);
	Nuklear.setFrameBufferDPIAware(bDPIAware);
}

static void window_iconify_callback(GLFWwindow* const window, int const iconified)
{
	// process input first
	cNuklear::nk_iconify_callback(window, iconified);

	if (iconified)
	{
		// The window was iconified
		MinCity::Vulkan.OnLost(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusLost(); // other handling and update of focus member boolean
	}
	else
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); // done a second time when window is focused

		// The window was restored
		MinCity::Vulkan.OnRestored(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusRestored(); // other handling and update of focus member boolean
	}
}
static void window_focus_callback(GLFWwindow* const window, int const focused)
{
	// process input first
	cNuklear::nk_focus_callback(window, focused);

	if (focused)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); // done a second time when window is focused
		
		// The window gained input focus
		MinCity::Vulkan.OnRestored(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusRestored(); // other handling and update of focus member boolean
	}
	else
	{
		// The window lost input focus
		MinCity::Vulkan.OnLost(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusLost(); // other handling and update of focus member boolean
	}
}

bool const cMinCity::Initialize(GLFWwindow*& glfwwindow)
{
#ifdef DEBUG_VARIABLES_ENABLED
	InitializeDebugVariables();
#endif

#ifndef NDEBUG
	fmt::print(fg(fmt::color::red), "MinCity DEBUG Build\t");
#else
	fmt::print(fg(fmt::color::royal_blue), "MinCity Release Build\t");
#ifdef DEBUG_OPTIONS_USED
	fmt::print(fg(fmt::color::orange_red), " **** Warning, debugging options enabled. \t");
#endif
#ifdef DEBUG_PERFORMANCE
	fmt::print(fg(fmt::color::orange), " *PROFILING ENABLED*");
#endif
#endif
	InitializeRandomNumberGenerators();
	fmt::print(fg(fmt::color::yellow), "\nMinCity Log h: {:#x} p: {:#x}\n", Hash((int64_t)oRandom.hashSeed), PsuedoRandomNumber());

	if (TASK_INIT) {
		if (TASK_INIT->is_active()) {
			FMT_LOG_OK(INFO_LOG, "Intel Threading Building Blocks initialized ");
			// not showin status of speculation -- to many cpu's do not support TSX, all AMD and a lot of Intel CPU's that have the feature disabled. shame. FMT_LOG_OK(NULL_LOG, "speculation : [{:s}]", TASK_INIT->has_speculation() ? "supported" : "unsupported");
		}
		else {
			FMT_LOG_FAIL(INFO_LOG, "Intel Threading Building Blocks was unable to initialize! 1x\n");
			return(false);
		}
	}
	else {
		FMT_LOG_FAIL(INFO_LOG, "Intel Threading Building Blocks was unable to initialize! 0x\n");
		return(false);
	}

	// Start background thread for long running tasks
	if (!async_long_task::initialize()) {
		FMT_LOG_FAIL(INFO_LOG, "Background Thread for long tasks could not be initialized! \n");
		return(false);
	}

	// Load Settings
	LoadINI(); // sequence first

	if (!Vulkan.LoadVulkanFramework()) {// sequence second
		fmt::print(fg(fmt::color::red), "[Vulkan] failed to load framework, unsupported required extension or feature.\n");
		return(false);
	}
	
	// Initialise the GLFW framework.
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);		// visibility of window deferred
	glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);		// startup window focus is not set yet, deferred
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);  // Show window always sets input focus
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);	// ensure this is off
	
	/// cool option 
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

	point2D_t resolution(getFramebufferSize());	// set by ini settings b4
	point2D_t max_resolution(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	if (Vulkan.isFullScreenExclusiveExtensionSupported()) {
		// maximum monitor resolution, users can decide to render to resolutions up to 2x the actual monitor resolution using "super resolution" in the amd cntrl panel, or similar for nvidia
		// only works properly if dedicated fullscreen exclusive mode is enabled and supported
		// other wise maximum resolution defaults to the actual screen resolution of the monitor
		max_resolution = p2D_shiftl(max_resolution, 1);
	}
  
	resolution.v = SFM::min(resolution.v, max_resolution.v); // clamp ini set resoluion to maximum monitor resolution

	// DPI Scaling - Scales Resolution by the factor set in display setings for Windows, eg.) 125% - Only affects borderless windowed mode
	//																								 Native resolution is used for fullscreen exclusive
	if (Nuklear.isFramebufferDPIAware()) { // option in MinCity.ini to disable DPI Aware Scaling of resoluion
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
		fmt::print(fg(fmt::color::hot_pink), "dpi awareness enabled\n");
	}

	// Make a window
	GLFWmonitor* monitor(nullptr); // default to "windowed" mode. Can be smaller than desktop resolution or be a "borderless full screen window"
								   // otherwise:
	if (Vulkan.isFullScreenExclusiveExtensionSupported()) { // if the ini setting is enabled & the extension for fullscreen exclusive is available
		monitor = glfwGetPrimaryMonitor();	// this enables native full-screen, allowing GLFW to change desktop resolution
	}										// the actual exclusive mode is handled in the window part of vku
	
	glfwwindow = glfwCreateWindow(resolution.x, resolution.y, Globals::TITLE, monitor, nullptr);

	point2D_t framebuffer_size;
	glfwGetFramebufferSize(glfwwindow, &framebuffer_size.x, &framebuffer_size.y);

	// bugfix, if window created is larger than maximum monitor resolution, disable hint GLFW_SCALE_TO_MONITOR and recreate window!
	if (framebuffer_size.x > max_resolution.x || framebuffer_size.y > max_resolution.y) {
		glfwDestroyWindow(glfwwindow); glfwwindow = nullptr;
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
		fmt::print(fg(fmt::color::orange), "ini resolution > than maximum monitor resolution, dpi awareness disabled\n");

		glfwwindow = glfwCreateWindow(resolution.x, resolution.y, Globals::TITLE, monitor, nullptr);

		// refresh window size
		glfwGetFramebufferSize(glfwwindow, &framebuffer_size.x, &framebuffer_size.y);
	}

	if (monitor) {
		glfwSetWindowMonitor(glfwwindow, monitor, 0, 0, resolution.x, resolution.y, GLFW_DONT_CARE);
	}
	else { // // only center window if not a fullscreen window
		point2D_t middle_point;

#ifndef NDEBUG // debug only top right corner
		middle_point.x = (max_resolution.x - framebuffer_size.x);
		middle_point.y = 0;

#else // normal // centered window on screen
		middle_point = p2D_sub(p2D_shiftr(max_resolution, 1), p2D_shiftr(framebuffer_size, 1));
#endif

		glfwSetWindowPos(glfwwindow, middle_point.x, middle_point.y);
	}

	// Adjust / Update application stored value tracking for framebuffer / window size
	glfwGetFramebufferSize(glfwwindow, &framebuffer_size.x, &framebuffer_size.y); // just in case after window reposition...
	Nuklear.setFrameBufferSize(framebuffer_size); // always update the internally used framebuffer size

	{ // output resolution info
		point2D_t const used_resolution(Nuklear.getFramebufferSize());
		fmt::print(fg(fmt::color::white), "resolution(.ini):  "); fmt::print(fg(fmt::color::hot_pink), "{:d}x{:d}\n", resolution.x, resolution.y);
		fmt::print(fg(fmt::color::white), "resolution(using):  "); fmt::print(fg(fmt::color::hot_pink), "{:d}x{:d}\n", used_resolution.x, used_resolution.y);
		fmt::print(fg(fmt::color::white), "dpi scaling: "); fmt::print(fg(fmt::color::green_yellow), "{:.0f}%\n", (float(used_resolution.x) / float(resolution.x)) * 100.0f);
	}

	supernoise::InitializeDefaultNoiseGeneration();

	if (!Vulkan.LoadVulkanWindow(glfwwindow)) { // sequence third
		fmt::print(fg(fmt::color::red), "[Vulkan] failed to create window surface, swap chain or other critical resources.\n");
		return(false);
	}
	
	VoxelWorld.LoadTextures();
	Nuklear.Initialize(glfwwindow);

	Vulkan.CreateResources();

	VoxelWorld.Initialize();

	Vulkan.UpdateDescriptorSetsAndStaticCommandBuffer();

	// deferred window show / focus set
	{
		glfwSetWindowIconifyCallback(glfwwindow, window_iconify_callback);
		glfwSetWindowFocusCallback(glfwwindow, window_focus_callback);

		glfwShowWindow(glfwwindow);

		glfwPollEvents(); // required once here, which will enable rendering at the right time
		window_iconify_callback(glfwwindow, 0);  // required to triggger first time, ensures rendering is started
	}
	// #### City pointer must be valid starting *here*
	City = new cCity(m_szCityName);

	VoxelWorld.Update(m_tNow, zero_time_duration, true, true);
	
	UserInterface.Initialize();

	if (!Audio.Initialize()) {
		FMT_LOG_FAIL(AUDIO_LOG, "FMOD was unable to initialize!\n");
	}

	m_bNewEventsAllowed = true; // defaults to false on startup........
	
	// ****start up events must begin here**** //
	DispatchEvent(eEvent::REFRESH_LOADLIST);


	m_bRunning = true;	// defaults to false on startup, succeeds only here***

	return(true);
}

void cMinCity::Pause(bool const bStateIsPaused)
{
	if (!bStateIsPaused && (eWindowType::DISABLED != Nuklear.getWindowEnabled())) // disallow changing pause state while quit, save, load, new windows are showing 
		return;

	if (bStateIsPaused) {
		if (!m_bPaused) { // block if already paused dont want to change last timestamp
			m_tLastPause = high_resolution_clock::now();
			m_bPaused = true;
			FMT_LOG(GAME_LOG, "Paused");
		}
		Vulkan.enableOffscreenRendering(true);  // enable rendering of offscreen rt for gui effect usage
	}
	else {
		if (m_bPaused) { // block if not paused, don't want to change start or last timestamps
			m_bPaused = false;
			FMT_LOG(GAME_LOG, "Un-Paused");
		}
		Vulkan.enableOffscreenRendering(false);  // disable / reset rendering of offscreen rt
	}
}

// local definition of FOLDERID_RoamingAppData declared in KnownFolders.h required
// {3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}
DEFINE_GUID(FOLDERID_RoamingAppData, 0x3EB685DB, 0x65F9, 0x4CF6, 0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D);

NO_INLINE std::filesystem::path const cMinCity::getUserFolder()
{
	static constexpr wchar_t const* const USER_DIR(L"\\.mincity");
	static std::filesystem::path path;

	if (path.empty()) { // only query once, use cached path therafter

		PWSTR path_tmp(nullptr);

		/* Attempt to get user's AppData folder
		 *
		 * Microsoft Docs:
		 * https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
		 * https://docs.microsoft.com/en-us/windows/win32/shell/knownfolderid
		 */
		auto const get_folder_path_ret = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp);

		// fallback
		if (S_OK != get_folder_path_ret) { // on failure
			if (path_tmp) {
				CoTaskMemFree(path_tmp); path_tmp = nullptr;
			}

			if (0 == _wdupenv_s(&path_tmp, nullptr, L"APPDATA")) {  // on success
				path = path_tmp;
			}

			if (path_tmp) {
				free(path_tmp); path_tmp = nullptr;
			}
		}
		else { // on success

			path = path_tmp;

			if (path_tmp) {
				CoTaskMemFree(path_tmp); path_tmp = nullptr;
			}
		}

		if (!path.empty()) {
			path += USER_DIR;

			// make sure directory exists //
			if (!std::filesystem::exists(path)) {
				std::filesystem::create_directory(path);
			}

			path += L"\\"; // required!
		}
		else {

			FMT_LOG_FAIL(GAME_LOG, "Could not find users appdata folder!\n");

		}
	}

	return(path);
}

void cMinCity::OnNew()
{
	SAFE_DELETE(City);
	City = new cCity(m_szCityName);

	FMT_LOG(GAME_LOG, "New");
}
void cMinCity::OnLoad()
{
	static int64_t _task_id_load(0);

	DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

	async_long_task::wait<background>(_task_id_load, "load"); // wait 1st on any loading task to complete before creating a new loading task

	m_eExclusivity = eExclusivity::LOADING;
	_task_id_load = async_long_task::enqueue<background>([&] {

		DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(1));
		VoxelWorld.LoadWorld();

		// make sure user can see feedback of loading, regardless of how fast the saving execution took
		FMT_LOG(GAME_LOG, "Loaded");

		DispatchEvent(eEvent::PAUSE, new bool(false));
		DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

		DispatchEvent(eEvent::REVERT_EXCLUSIVITY);
	});
}
void cMinCity::OnSave(bool const bShutdownAfter)
{
	static int64_t _task_id_save(0);

	DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

	// must be done in main thread:
	MinCity::Vulkan.enableOffscreenCopy();

	async_long_task::wait<background>(_task_id_save, "save"); // wait 1st on any saving task to complete before creating a new saving task

	m_eExclusivity = eExclusivity::SAVING;
	_task_id_save = async_long_task::enqueue<background>([&, bShutdownAfter] {

		DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(1));
		VoxelWorld.SaveWorld();

		// make sure user can see feedback of saving, regardless of how fast the saving execution took
		FMT_LOG(GAME_LOG, "Saved");

		if (bShutdownAfter) {
			DispatchEvent(eEvent::EXPEDITED_SHUTDOWN);
		}
		else {
			DispatchEvent(eEvent::PAUSE, new bool(false));
			DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!
			DispatchEvent(eEvent::REFRESH_LOADLIST);

			DispatchEvent(eEvent::REVERT_EXCLUSIVITY);
		}
	});
}

void cMinCity::Load()
{
	static bool bDelay(true);

	int32_t const load_state(Nuklear.getLastSelectionForWindow<eWindowType::LOAD>());

	if (eWindowLoad::IDLE == load_state) {

		if (!Nuklear.enableWindow<eWindowType::LOAD>(true)) { // load window not currently shown, and now were opening it

			Pause(true); // matching unpause is in shutdown()
		}
	}
	else {
		OnLoad();
	}
}
void cMinCity::Save(bool const bShutdownAfter) // no user prompt, immediate clean shutdown
{
	static bool bDelay(true);

	int32_t const save_state(Nuklear.getLastSelectionForWindow<eWindowType::SAVE>());

	if (eWindowSave::IDLE == save_state) {

		if (!Nuklear.enableWindow<eWindowType::SAVE>(true)) { // save window not currently shown, and now were opening it

			Pause(true); // matching unpause is in shutdown()
		}
	}
	else {
		OnSave(bShutdownAfter);
	}
}
int32_t const cMinCity::Quit(bool const bQueryStateOnly) // prompts user to quit, only returns result of prompt does not actually quit application, shutdown must be called in that case
{
	int32_t const quit_state(Nuklear.getLastSelectionForWindow<eWindowType::QUIT>());
	
	if (!bQueryStateOnly && eWindowQuit::IDLE == quit_state) {

		if (!Nuklear.enableWindow<eWindowType::QUIT>(true)) { // quit window not currently shown

			Pause(true); // matching unpause is in shutdown()
		}
	}

	return(quit_state);
}

bool const cMinCity::Shutdown(int32_t const action, bool const expedite) // no user prompt, returns true if shutting down
{
	bool bShuttingDown(false);

	switch (action) {

	case eWindowQuit::SAVE_AND_QUIT:
	case eWindowQuit::JUST_QUIT:
		bShuttingDown = true;
		break;
	default:
		bShuttingDown = expedite; // expedite == true = exception case - user hit alt+f4 *or* external system event
		// otherwise normally do nothing in idle or pending states
		break;
	}

	if (bShuttingDown) {
		m_bRunning = false;  // only place this flag changes
		ClearEvents(true);
	}
	return(bShuttingDown);
}

NO_INLINE static bool const GradualStartUp(size_t const frameCount, bool const& IsRunning)	// prevents huge spike in power usuage at startup, preventing crashes
{
	static constexpr uint32_t const
		StageOneStartUpFrameCount = 16U,							 // 60 fps, first 60 frames (~first second)
		StageTwoStartUpFrameCount = StageOneStartUpFrameCount << 1U;  // 60 fps, first (~3 seconds)
	static constexpr uint32_t const
		StageOneSleepTime = 33U,
		StageTwoSleepTime = 16U;

	bool GradualStartUpOngoing(false); // default to completed gradual startup state

	uint32_t uiSleepTime(0);
	if (frameCount < StageOneStartUpFrameCount) {
		uiSleepTime = StageOneSleepTime;
		GradualStartUpOngoing = true;
	}
	else if (frameCount < (StageTwoStartUpFrameCount)) {
		uiSleepTime = StageTwoSleepTime;
		GradualStartUpOngoing = true;
	}

	if (GradualStartUpOngoing && IsRunning) { // extra protect against using sleep when were not really wanting too
		_mm_pause();
		Sleep(uiSleepTime - 1);						// prevent sleep if not running flag is current state
	}

	return(GradualStartUpOngoing); // signal as gradual startup complete
}

void cMinCity::UpdateWorld() 
{
	static bool bWasPaused(false);
	static tTime 
		tLast{ zero_time_point },
		tLastGUI{ zero_time_point };

	duration tDeltaFixedStep(fixed_delta_duration);
	tTime tNow{ high_resolution_clock::now() };
	tTime const 
		tCriticalNow(tNow),
		tCriticalLast(tLast);
				
	{
		[[unlikely]] if (m_bGradualStartingUp) {
			m_bGradualStartingUp = GradualStartUp(m_frameCount, m_bRunning);
		}
	}

	bool const bPaused(MinCity::isPaused());
	if (bPaused) {

		tNow = tLast = m_tLastPause;
		tDeltaFixedStep = zero_time_duration;  // paused timestep
		bWasPaused = true;
	}
	else if (bWasPaused) {
		tLast = tNow; // resume as if no time elapsed
		bWasPaused = false;
	}

	static duration tAccumulate(nanoseconds(0));
	{   // variable time step intended to not be used outside of this scope
		// Accunmulate actual time per frame
		// clamp at the 2x step size, don't care or want spurious spikes of time

		tAccumulate += std::min(duration(tCriticalNow - tCriticalLast), fixed_delta_x2_duration);
	}

	// add to fixed timestamp n fixed steps, while also removing the fixed step from the accumulator
	bool bDispatchUpdate(false);
	while (tAccumulate >= fixed_delta_duration) {

		m_tNow += tDeltaFixedStep;  // pause-able time step
		m_tCriticalNow += fixed_delta_duration;

		tAccumulate -= fixed_delta_duration;
		bDispatchUpdate = true;
	}
	
	Audio.Update(); // done 1st as this is asynchronous, other tasks can occur simultaneously

	// always update *input* everyframe, UpdateInput returns true to flag a gui update is neccesary
	bool const bInputDelta = Nuklear.UpdateInput() | ((tCriticalNow - tLastGUI) >= nanoseconds(milliseconds(Globals::INTERVAL_GUI_UPDATE)));
	
	if (bInputDelta) {  
		Nuklear.UpdateGUI(); // gui requires critical timing (always on, never pause gui)
		tLastGUI = tCriticalNow;
	}

	if (bDispatchUpdate) {
		fp_seconds const tDelta(tDeltaFixedStep);
		VoxelWorld.Update(m_tNow, tDelta, bPaused); // world/game uses regular timing, with a fixed timestep (best practice)
	}
	
	// fractional amount for render path
	VoxelWorld.UpdateUniformState(fp_seconds(fp_seconds(tAccumulate) / fp_seconds(fixed_delta_duration)).count()); // always update everyframe
	//---------------------------------------------------------------------------------------------------------------------------------------//
	tLast = tCriticalNow;
	
	ProcessEvents();
}

void cMinCity::StageResources(uint32_t const resource_index)
{
	[[unlikely]] if (eExclusivity::MAIN != m_eExclusivity)
		return;

	// Updating the voxel lattice by "rendering" it to the staging buffers, light probe image, etc
	VoxelWorld.Render(resource_index);
	VoxelWorld.UpdateUniformStateLatest();
	Vulkan.checkStaticCommandsDirty();
}

void cMinCity::Render()
{
	Vulkan.Render();

	++m_frameCount;
}

bool const cMinCity::DispatchEvent(uint32_t const eventType, void* const data)
{
	if (m_bNewEventsAllowed || eEvent::EXPEDITED_SHUTDOWN == eventType) {
		m_events.emplace(std::forward<std::pair<uint32_t, void*>&&>(std::make_pair(eventType, data)));
		return(true);
	}
	return(false);
}
void cMinCity::ProcessEvents()
{
	if (glfwWindowShouldClose(Nuklear.getGLFWWindow())) {
		DispatchEvent(eEvent::EXPEDITED_SHUTDOWN);
	}

	std::pair<uint32_t, void*> new_event;

	while (m_events.try_pop(new_event))
	{
		switch (new_event.first) {
		case eEvent::PAUSE:
			if (new_event.second) {
				Pause(*((bool const* const)new_event.second));
			}
			else {
				Pause(true);
			}
			break;
		case eEvent::PAUSE_PROGRESS:
			if (new_event.second) {
				Nuklear.setPauseProgress(*((uint32_t const* const)new_event.second));
			}
			else {
				Nuklear.setPauseProgress(0);
			}
			break;
		case eEvent::REVERT_EXCLUSIVITY:
			m_eExclusivity = eExclusivity::MAIN;
			break;
		case eEvent::REFRESH_LOADLIST:
			VoxelWorld.RefreshLoadList();
			break;
		case eEvent::SAVE:
			if (new_event.second) {
				Save(*((bool const* const)new_event.second));
			}
			else {
				Save();
			}
			break;
		case eEvent::LOAD:
			Load();
			break;
		case eEvent::NEW:
			break;

		//quit & expedited shutdown
		case eEvent::QUIT:
		{
			if (eWindowQuit::PENDING & Nuklear.getLastSelectionForWindow<eWindowType::QUIT>()) {
				Shutdown(Quit(true));
			}
		}
			break;
		//last
		case eEvent::EXPEDITED_SHUTDOWN:
			Shutdown(0, true);
			break;
		default:
			break;
		}

		SAFE_DELETE(new_event.second);
	}
}
void cMinCity::ClearEvents(bool const bDisableNewEventsAfter)
{
	m_bNewEventsAllowed = false; // disable adding events while clearing

	std::pair<uint32_t, void*> new_event;

	while (!m_events.empty()) {
		while (m_events.try_pop(new_event))
		{
			SAFE_DELETE(new_event.second);
		}
		_mm_pause();
	}

	// only restore events if not disabling
	if (!bDisableNewEventsAfter) {
		m_bNewEventsAllowed = true;
	}
}
void cMinCity::OnFocusLost()
{
	m_bFocused = false;
}
void cMinCity::OnFocusRestored()
{
	m_bFocused = true;
	SetForegroundWindow(glfwGetWin32Window(g_glfwwindow));  // enable foreground window for the now foreground process
}

__declspec(noinline) void cMinCity::Cleanup(GLFWwindow* const glfwwindow)
{
	ClipCursor(nullptr);

	Vulkan.WaitDeviceIdle(); // huge memory leak bugfix
	
	SAFE_DELETE(City);

	Audio.CleanUp();
	Nuklear.CleanUp();
	PostProcess.CleanUp();
	VoxelWorld.CleanUp();
	TextureBoy.CleanUp();

	Vulkan.Cleanup(glfwwindow);  /// should be LAST //
}

__declspec(noinline) static bool SetProcessPrivilege(
	HANDLE const hToken,          // access token handle
	LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
	bool const bEnablePrivilege   // to enable or disable privilege
)
{
	TOKEN_PRIVILEGES tp;
	LUID luid;

	if (!LookupPrivilegeValue(
		NULL,            // lookup privilege on local system
		lpszPrivilege,   // privilege to lookup 
		&luid))        // receives LUID of privilege
	{
		return(false);
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	if (bEnablePrivilege)
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	else
		tp.Privileges[0].Attributes = 0;

	// Enable the privilege or disable all privileges.

	if (!AdjustTokenPrivileges(
		hToken,
		FALSE,
		&tp,
		sizeof(TOKEN_PRIVILEGES),
		(PTOKEN_PRIVILEGES)NULL,
		(PDWORD)NULL))
	{
		return(false);
	}

	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
	{
		return(false);
	}

	return(true);
}

__declspec(noinline) HANDLE const cMinCity::SetupEnvironment() // main thread and hyperthreading cache optimizations
{
	HANDLE const hProcess(GetCurrentProcess());
	HANDLE const hThread(GetCurrentThread());

	// 1st step: Process CPU Priority
	// *** setting cpu process prority at any given time also resets all threads created by that process. So It should only be used
	// at startup init, before any possible thread changes could take place.

	// since this is a realtime application / user focused / computer game, AboveNormal Priority is Normal. Do not set to HIGH or REALTIME in
	// any case, as those levels are restricted for OS usage for critical operations (drivers, sound, ctrl+alt+delete)
	SetPriorityClass(hProcess, ABOVE_NORMAL_PRIORITY_CLASS);

	// ensure boosting is enabled for this process
	SetProcessPriorityBoost(hProcess, FALSE);

	// cpu and core optimization
	{
		// Windows 10 - new optimal CPUSet API for managing threads
		unsigned long retsize = 0;
		(void)GetSystemCpuSetInformation(nullptr, 0, &retsize,
			hProcess, 0);

		if (retsize) {
			std::unique_ptr<uint8_t[]> data(new uint8_t[retsize]);
			PSYSTEM_CPU_SET_INFORMATION const& cpu_set(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(data.get()));
			if (GetSystemCpuSetInformation(
				cpu_set,
				retsize, &retsize, hProcess, 0))
			{

				std::set<DWORD> hw_cores;
				std::vector<DWORD> logical_processors;
				uint8_t const* ptr = data.get();
				for (DWORD size = 0; size < retsize; ) {
					auto const info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION const* const>(ptr);
					if (CpuSetInformation == info->Type) {
						logical_processors.push_back(info->CpuSet.Id);
						hw_cores.insert(info->CpuSet.CoreIndex);
					}
					ptr += info->Size;
					size += info->Size;
				}

				bool const hyperthreaded = logical_processors.size() != hw_cores.size();

				unsigned long logical_processor_count_used(0);

				if (hyperthreaded) {

					// reserve both logical processors on first hardware core for main thread
					{
						unsigned long const cores[] = { logical_processors[0], logical_processors[1] };
						logical_processor_count_used = 2;
						SetThreadSelectedCpuSets(hThread, cores, logical_processor_count_used);
					}
				}
				else {
					// reserve logical processor that is the first hardware core for main thread
					{
						unsigned long const cores[] = { logical_processors[0] };
						logical_processor_count_used = 1;
						SetThreadSelectedCpuSets(hThread, cores, logical_processor_count_used);
					}
				}

				// any new threads are limited to hw cores not the same as the main thread
				SetProcessDefaultCpuSets(hProcess, logical_processors.data() + logical_processor_count_used,
					(unsigned long)logical_processors.size() - logical_processor_count_used);
			}
		}
	}

	// turn off windows managwd power throttliny of this process
	{
		PROCESS_POWER_THROTTLING_STATE PowerThrottling{};
		PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

		PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
		PowerThrottling.StateMask = 0;

		SetProcessInformation(hProcess,
			ProcessPowerThrottling,
			&PowerThrottling,
			sizeof(PowerThrottling));
	}

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	{
		HANDLE hToken;
		if (OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		{
			SetProcessPrivilege(hToken, L"SeIncreaseWorkingSetPrivilege", true);
			SetProcessPrivilege(hToken, L"SeLockMemoryPrivilege", true);
		}
	}

	// optimize usage of virtual memory of process
	{
		size_t szMin(0), szMax(0);
		DWORD flags(0);

		if (GetProcessWorkingSetSizeEx(hProcess, &szMin, &szMax, &flags)) {

			flags |= (QUOTA_LIMITS_HARDWS_MIN_ENABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE);
			// defined private in cMinCity.h
			szMin += PROCESS_MIN_WORKING_SET / sysInfo.dwPageSize;
			szMax += PROCESS_MAX_WORKING_SET / sysInfo.dwPageSize;
			SetProcessWorkingSetSizeEx(hProcess, szMin, szMax, flags);

		}
	}

	// enable low-fragmentation heap for entire process
	{
		ULONG HeapEnableLFH(2); // 2 == flag for LFH
		{
			HANDLE const hProcessHeap = GetProcessHeap();

			HeapSetInformation(hProcessHeap,
				HeapCompatibilityInformation,
				&HeapEnableLFH,
				sizeof(HeapEnableLFH));
		}

		{
			intptr_t const hUCRTHeap = _get_heap_handle();

			HeapSetInformation((PVOID)hUCRTHeap,
				HeapCompatibilityInformation,
				&HeapEnableLFH,
				sizeof(HeapEnableLFH));
		}
	}

	// give priority to memory pages for main thread
	{
		MEMORY_PRIORITY_INFORMATION MemPrio{};

		MemPrio.MemoryPriority = MEMORY_PRIORITY_NORMAL;	// 5 (NORMAL) Is the highest level. This can also be seen in process explorer. 1 Is VERY LOW, not high like it would seem one would indicate.

		SetThreadInformation(hThread,
			ThreadMemoryPriority,
			&MemPrio,
			sizeof(MemPrio));
	}

	return(hThread);
}

extern __declspec(noinline) void global_init_tbb_floating_point_env(tbb::task_scheduler_init*& TASK_INIT);  // external forward decl
__declspec(noinline) void cMinCity::CriticalInit()
{
	// setup secure loading of dlls, should be done before loading any dlls, or creation of any threads under this process (including dlls creating threads)
	{
		SetDllDirectory(L""); // disallow *live loading* dll's in process current directory (LoadLibrary())
		SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE);

		DWORD const dwLength = GetCurrentDirectory(0, NULL);

		if (0 != dwLength) {
			LPTSTR szBuffer = new TCHAR[dwLength + 1];
			if (0 != GetCurrentDirectory(dwLength, szBuffer)) {
				std::wstring const
					szCurrentDirectory(szBuffer),
					szBinDirectory(BIN_DIR);

				// location of live loaded dlls required by MinCity - off of the current directory and now a fully qualified name 
				// see:
				// https://support.microsoft.com/en-us/help/2389418/secure-loading-of-libraries-to-prevent-dll-preloading-attacks
				SetDllDirectory((szCurrentDirectory + szBinDirectory).c_str());
			}
			SAFE_DELETE_ARRAY(szBuffer);
		}

		SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);
	}

	// changes timing resolution, affects threading, scheduling, sleep, waits etc globally (important)
	// https://docs.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
	timeEndPeriod(1); // ensure that the regular default timer resolution is active

	SetupEnvironment();
	
	global_init_tbb_floating_point_env(TASK_INIT);
}
__declspec(noinline) void cMinCity::CriticalCleanup()
{
	ClipCursor(nullptr);

	SAFE_DELETE(TASK_INIT);
}

int __stdcall _tWinMain(_In_ HINSTANCE hInstance,
					    _In_opt_ HINSTANCE hPrevInstance,
					    _In_ LPTSTR    lpCmdLine,
					    _In_ int       nCmdShow)
 {  
	UNREFERENCED_PARAMETER(hPrevInstance);	
	
	cMinCity::CriticalInit();
	
	RedirectIOToConsole();

	cMinCity::Initialize(g_glfwwindow);  // no need to check the state here, unless handling errors
									   // Running status is updated in this function if succesful

	// Loop waiting for the window to close, exit of program, etc
	while (cMinCity::isRunning()) {

		cMinCity::UpdateWorld();
		cMinCity::Render();
	}

	cMinCity::Cleanup(g_glfwwindow);
	cMinCity::CriticalCleanup();

	WaitConsoleClose();

	return(0);
}
