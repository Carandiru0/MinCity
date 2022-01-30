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
#include "resource.h"

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
namespace // private to this file (anonymous)
{
	constinit static inline tbb::task_scheduler_init* TASK_INIT{ nullptr };
	constinit static inline GLFWwindow* g_glfwwindow(nullptr);
	constinit static inline HINSTANCE g_hInstance(nullptr);
}

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
		Nuklear->setFrameBufferSize(iResolutionWidth, iResolutionHeight);
	}
	else {
		// default to maximum desktop resolution
		Nuklear->setFrameBufferSize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	}

	bool const bFullscreenExclusive = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"FULLSCREEN_EXCLUSIVE", TRUE, szINIFile);
	Vulkan->setFullScreenExclusiveEnabled(bFullscreenExclusive);

	uint32_t const uiHDRNits = (uint32_t)GetPrivateProfileInt(L"RENDER_SETTINGS", L"HDR_NITS", TRUE, szINIFile);
	Vulkan->setHDREnabled(0 != uiHDRNits, uiHDRNits);

	bool const bVsyncEnabled = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"VSYNC", TRUE, szINIFile);
	Vulkan->setVsyncDisabled(!bVsyncEnabled);

	bool const bDPIAware = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"DPI_AWARE", TRUE, szINIFile);
	Nuklear->setFrameBufferDPIAware(bDPIAware);
}

static void window_iconify_callback(GLFWwindow* const window, int const iconified)
{
	// process input first
	cNuklear::nk_iconify_callback(window, iconified);

	if (iconified)
	{
		// The window was iconified
		MinCity::Vulkan->OnLost(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusLost(); // other handling and update of focus member boolean
	}
	else
	{
		// The window was restored
		MinCity::Vulkan->OnRestored(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusRestored(); // other handling and update of focus member boolean
	}
}
static void window_focus_callback(GLFWwindow* const window, int const focused)
{
	// process input first
	cNuklear::nk_focus_callback(window, focused);

	if (focused)
	{
		// The window gained input focus
		MinCity::Vulkan->OnRestored(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusRestored(); // other handling and update of focus member boolean
	}
	else
	{
		// The window lost input focus
		MinCity::Vulkan->OnLost(window); // critical first rendering enable/disable and pause / unpause handling
		MinCity::OnFocusLost(); // other handling and update of focus member boolean
	}
}

NO_INLINE bool const cMinCity::Initialize(GLFWwindow*& glfwwindow)
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

			FMT_LOG_OK(INFO_LOG, "[Threading] initialized ");
			// not showin status of speculation -- to many cpu's do not support TSX, all AMD and a lot of Intel CPU's that have the feature disabled. shame. FMT_LOG_OK(NULL_LOG, "speculation : [{:s}]", TASK_INIT->has_speculation() ? "supported" : "unsupported");
		}
		else {
			FMT_LOG_FAIL(INFO_LOG, "[Threading] was unable to initialize! 1x\n");
			return(false);
		}
	}
	else {
		FMT_LOG_FAIL(INFO_LOG, "[Threading] was unable to initialize! 0x\n");
		return(false);
	}

	// Load Settings
	LoadINI(); // sequence first

	if (!Vulkan->LoadVulkanFramework()) {// sequence second
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
	
	glfwWindowHint(GLFW_RED_BITS, 10);
	glfwWindowHint(GLFW_GREEN_BITS, 10);
	glfwWindowHint(GLFW_BLUE_BITS, 10);
	glfwWindowHint(GLFW_REFRESH_RATE, 75);

	/// cool option 
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

	point2D_t const desired_resolution(getFramebufferSize());	// set by ini settings b4
	point2D_t const max_resolution(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	point2D_t resolution( SFM::min(desired_resolution.v, max_resolution.v) ); // clamp ini set resoluion to maximum monitor resolution

	GLFWmonitor* const primary_monitor(glfwGetPrimaryMonitor());

	if (!Vulkan->isFullScreenExclusiveExtensionSupported()) { // if not exclusive fullscreen

		if (resolution < max_resolution) { // and resolution is less than the monitor solution (not borderless fullscreen)

			// adjust the window to be friendly with the windows taskbar
			rect2D_t rectUsableArea;
			glfwGetMonitorWorkarea(primary_monitor, &rectUsableArea.left, &rectUsableArea.top, &rectUsableArea.right, &rectUsableArea.bottom);
			// glfwGetMonitorWorkarea returns the position, then the dimensions (width/height)
			// formatting rect to be correct from window position (top-left):
			rectUsableArea.right += rectUsableArea.left;
			rectUsableArea.bottom += rectUsableArea.top;

			// clamp resolution to bounds of work area rect
			resolution.v = SFM::clamp(resolution.v, rectUsableArea.left_top().v, rectUsableArea.right_bottom().v);
		}
	}

	// DPI Scaling - Scales Resolution by the factor set in display setings for Windows, eg.) 125% - Only affects borderless windowed mode
	//																								 Native resolution is used for fullscreen exclusive
	if (Nuklear->isFramebufferDPIAware()) { // option in MinCity.ini to disable DPI Aware Scaling of resoluion
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
		fmt::print(fg(fmt::color::hot_pink), "dpi awareness enabled\n");
	}

	// Make a window
	GLFWmonitor* monitor(nullptr); // default to "windowed" mode. Can be smaller than desktop resolution or be a "borderless full screen window"
								   // otherwise:
	if (Vulkan->isFullScreenExclusiveExtensionSupported()) { // if the ini setting is enabled & the extension for fullscreen exclusive is available
		monitor = primary_monitor;	// this enables native full-screen, allowing GLFW to change desktop resolution
	}										// the actual exclusive mode is handled in the window part of vku
	
	glfwwindow = glfwCreateWindow(resolution.x, resolution.y, Globals::TITLE, monitor, nullptr);

	point2D_t framebuffer_size;
	glfwGetFramebufferSize(glfwwindow, &framebuffer_size.x, &framebuffer_size.y);

	// bugfix, if window created is larger than maximum monitor resolution, disable hint GLFW_SCALE_TO_MONITOR and recreate window!
	if (framebuffer_size.x > max_resolution.x || framebuffer_size.y > max_resolution.y) {
		glfwDestroyWindow(glfwwindow); glfwwindow = nullptr;
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
		fmt::print(fg(fmt::color::orange), "dpi awareness disabled\n");

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
		middle_point.y = 0; // *bugfix: align to top of screen to av				oid issues with taskbar covering bottom area of window.
#endif

		glfwSetWindowPos(glfwwindow, middle_point.x, middle_point.y);
	}

	// Adjust / Update application stored value tracking for framebuffer / window size
	glfwGetFramebufferSize(glfwwindow, &framebuffer_size.x, &framebuffer_size.y); // just in case after window reposition...
	Nuklear->setFrameBufferSize(framebuffer_size); // always update the internally used framebuffer size

	{ // output resolution info
		point2D_t const used_resolution(Nuklear->getFramebufferSize());
		fmt::print(fg(fmt::color::white), "resolution(desired):  "); fmt::print(fg(fmt::color::hot_pink), "{:d}x{:d}\n", desired_resolution.x, desired_resolution.y);
		fmt::print(fg(fmt::color::white), "resolution(using):  "); fmt::print(fg(fmt::color::hot_pink), "{:d}x{:d}\n", used_resolution.x, used_resolution.y);
		fmt::print(fg(fmt::color::white), "dpi scaling: "); fmt::print(fg(fmt::color::green_yellow), "{:.0f}%\n", (float(used_resolution.x) / float(desired_resolution.x)) * 100.0f);
	}

	supernoise::InitializeDefaultNoiseGeneration();

	if (!Vulkan->LoadVulkanWindow(glfwwindow)) { // sequence third
		fmt::print(fg(fmt::color::red), "[Vulkan] failed to create window surface, swap chain or other critical resources.\n");
		return(false);
	}
	
	VoxelWorld->LoadTextures();
	Nuklear->Initialize(glfwwindow);

	Vulkan->CreateResources();

	VoxelWorld->Initialize();

	Vulkan->UpdateDescriptorSetsAndStaticCommandBuffer();

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

	VoxelWorld->Update(m_tNow, zero_time_duration, true, true, true);
	
	UserInterface->Initialize();

	if (!Audio->Initialize()) {
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
	if (!bStateIsPaused && (eWindowType::DISABLED != Nuklear->getWindowEnabled())) // disallow changing pause state while quit, save, load, new windows are showing 
		return;

	if (bStateIsPaused) {
		if (!m_bPaused) { // block if already paused dont want to change last timestamp
			m_tLastPause = high_resolution_clock::now();
			m_bPaused = true;
			FMT_LOG(GAME_LOG, "Paused");
		}

	}
	else {
		if (m_bPaused) { // block if not paused, don't want to change start or last timestamps
			m_bPaused = false;
			FMT_LOG(GAME_LOG, "Un-Paused");
		}
		Vulkan->enableOffscreenRendering(false);  // disable / reset rendering of offscreen rt - always shutoff
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
	async_long_task::wait_for_all(milliseconds(async_long_task::beats::frame));
	m_eExclusivity = eExclusivity::NEW;

	SAFE_DELETE(City);
	City = new cCity(m_szCityName);

	FMT_LOG(GAME_LOG, "New");

	m_eExclusivity = eExclusivity::DEFAULT;
}
void cMinCity::OnLoad()
{
	constinit static int64_t _task_id_load(0);

	async_long_task::wait_for_all(milliseconds(async_long_task::beats::frame));
	m_eExclusivity = eExclusivity::LOADING;

	DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

	async_long_task::wait<background>(_task_id_load, "load"); // wait 1st on any loading task to complete before creating a new loading task

	_task_id_load = async_long_task::enqueue<background>([&] {

		DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(1));
		VoxelWorld->LoadWorld();

		// make sure user can see feedback of loading, regardless of how fast the saving execution took
		FMT_LOG(GAME_LOG, "Loaded");

		DispatchEvent(eEvent::PAUSE, new bool(false));
		DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

		DispatchEvent(eEvent::REVERT_EXCLUSIVITY);
	});
}
void cMinCity::OnSave(bool const bShutdownAfter)
{
	constinit static int64_t _task_id_save(0);

	async_long_task::wait_for_all(milliseconds(async_long_task::beats::frame));
	m_eExclusivity = eExclusivity::SAVING;

	DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

	// must be done in main thread:
	MinCity::Vulkan->enableOffscreenCopy();

	async_long_task::wait<background>(_task_id_save, "save"); // wait 1st on any saving task to complete before creating a new saving task

	_task_id_save = async_long_task::enqueue<background>([&, bShutdownAfter] {

		DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(1));
		VoxelWorld->SaveWorld();

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
	constinit static bool bDelay(true);

	int32_t const load_state(Nuklear->getLastSelectionForWindow<eWindowType::LOAD>());

	if (eWindowLoad::IDLE == load_state) {

		if (!Nuklear->enableWindow<eWindowType::LOAD>(true)) { // load window not currently shown, and now were opening it

			Pause(true); // matching unpause is in shutdown()
		}
	}
	else {
		OnLoad();
	}
}
void cMinCity::Save(bool const bShutdownAfter) // no user prompt, immediate clean shutdown
{
	constinit static bool bDelay(true);

	int32_t const save_state(Nuklear->getLastSelectionForWindow<eWindowType::SAVE>());

	if (eWindowSave::IDLE == save_state) {

		if (!Nuklear->enableWindow<eWindowType::SAVE>(true)) { // save window not currently shown, and now were opening it

			Pause(true); // matching unpause is in shutdown()
		}
	}
	else {
		OnSave(bShutdownAfter);
	}
}
int32_t const cMinCity::Quit(bool const bQueryStateOnly) // prompts user to quit, only returns result of prompt does not actually quit application, shutdown must be called in that case
{
	int32_t const quit_state(Nuklear->getLastSelectionForWindow<eWindowType::QUIT>());
	
	if (!bQueryStateOnly && eWindowQuit::IDLE == quit_state) {

		if (!Nuklear->enableWindow<eWindowType::QUIT>(true)) { // quit window not currently shown

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
	constinit static bool bWasPaused(false);
	constinit static tTime 
		tLast{ zero_time_point },
		tLastGUI{ zero_time_point };

	duration tDeltaFixedStep(delta());
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

	// *first*
	VoxelWorld->clearMiniVoxels(); // clearing voxel queue required b4 any voxels are added by addVoxel() method (which is only allowed in paint metods of UserInterface)

	// *second*
	bool const bInputDelta = Nuklear->UpdateInput() | ((tCriticalNow - tLastGUI) >= nanoseconds(milliseconds(Globals::INTERVAL_GUI_UPDATE))); // always update *input* everyframe, UpdateInput returns true to flag a gui update is neccesary

	// *third*
	VoxelWorld->PreUpdate(bPaused); // called every frame regardless of timing

	// *fourth*
	constinit static duration tAccumulate(nanoseconds(0));
	{   // variable time step intended to not be used outside of this scope
		// Accunmulate actual time per frame
		// clamp at the 2x step size, don't care or want spurious spikes of time

		tAccumulate += std::min(duration(tCriticalNow - tCriticalLast), fixed_delta_x2_duration);
	}

	// add to fixed timestamp n fixed steps, while also removing the fixed step from the accumulator
	bool bFirstUpdate(true);

	while (tAccumulate >= delta()) {

		m_tNow += tDeltaFixedStep;  // pause-able time step
		m_tCriticalNow += delta();

		tAccumulate -= delta();

		VoxelWorld->Update(m_tNow, tDeltaFixedStep, bPaused, bFirstUpdate); // world/game uses regular timing, with a fixed timestep (best practice)
		bFirstUpdate = false;
	}
	// fractional amount for render path (uniform shader variables)
	VoxelWorld->UpdateUniformState(time_to_float(fp_seconds(tAccumulate) / fp_seconds(delta()))); // always update everyframe - this is exploited between successive renders with no update() in between (when tAccumulate < delta() or bFirstUpdate is true)

	// *fifth*
	Audio->Update(); // done 1st as this is asynchronous, other tasks can occur simultaneously
	
	// *sixth*
	if (bInputDelta) {  
		Nuklear->UpdateGUI(); // gui requires critical timing (always on, never pause gui)
		tLastGUI = tCriticalNow;
	}
	
	// *seventh*
	UserInterface->Paint(); // must be done after all updates to VoxelWorld
	
	// *last*
	ProcessEvents();

	//---------------------------------------------------------------------------------------------------------------------------------------//
	tLast = tCriticalNow;
}

STATIC_INLINE_PURE bool const durations_to_seeds(int64_t& __restrict s0, int64_t& __restrict s1, fp_seconds const t0, fp_seconds const t1)
{
	static constexpr double const MAX_EXACT_INT = 9.007199254740992e15; // (2^53 to signed 64bit integer) maximum value that can be exactly converted from double to int64_t

	if (t0 != t1) {
		int64_t const i0(SFM::floor_to_i64(t0.count()));
		int64_t const i1(SFM::floor_to_i64(t1.count()));

		if (i0 != i1) { // comparing the non fractional part (integer) of the double precision time durations. 

			s0 = i0; // use change as seed
			s1 = i1; // use change as seed

			return(true);
		}
		/*else {*/ // comparing the fractional part (fraction) of the double precision time durations

			double d0(SFM::floor(t0.count()));
			double d1(SFM::floor(t1.count()));

			d0 = t0.count() - d0; // fract
			d1 = t1.count() - d1; // fract

			// range [0.0 ... 1.0] to [0.0 ... MAX_EXACT_INT]
			d0 = d0 * MAX_EXACT_INT;
			d1 = d1 * MAX_EXACT_INT;

			// now comparing the smallest possible change
			// (comparison hidden] already completed in 1st comparison between double precision time durations (above)
			// guarantees d0 != d1

			s0 = SFM::floor_to_i64(d0); // use scaled change as seed
			s1 = SFM::floor_to_i64(d1); // use scaled change as seed

			return(true);
		//}
	}

	return(false); // no change // seeds remain the same, no output from function.
}

void cMinCity::StageResources(uint32_t const resource_index)
{
	// resources uploaded to the gpu only change if exclusiity is not taken away from default
	// prevents threading errors while doing simultaneous tasks accessing the same data in a write(staging) and read/write(saving & loading) condition
	// instead the currently used state of resources on the gpu is re-used; does not change
	[[unlikely]] if (eExclusivity::DEFAULT != m_eExclusivity) {
		_mm_pause();
		Sleep(async_long_task::beats::minimum);

		// bring down cpu & power usage when standing by //
		[[unlikely]] if (eExclusivity::STANDBY == m_eExclusivity) {
			_mm_pause();
			Sleep(((DWORD const)duration_cast<milliseconds>(delta()).count()));
		}
		return;
	}

	// Important & Powerful construct
	// - Allows for repeatable determinism between frames
	// - Can skip or produce the eaxct same state everyframe for any algorithm used during that frame that leverages random numbers to define its state (ie.) Rain)
	// - properly pauses and repeats last seed, so things like rain that don't depend on time progression, but rather frame progression are actually paused.
	// - **** allows for a new unique sequence of random numbers to be used every frame, so no two frames are ever dependent on the progression of a single random number sequence and it's usage in previous frames ****
	// *do not remove*, critical code with far reaching effects on entire program life*
	// - note: dependent on the update() rate, so the normal delta or time step between frames should be set to 60 FPS or better. Refering to the fixed time-step used for updating the world ( tTime.h )
	//         not dependent on actual frame rate (decoupled)
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	{ // 
		constinit static fp_seconds tAppLast{}; // double precision required
		constinit static int64_t last_seed{1};  // a seed should never ever be zero
		constinit static int64_t no_delta_count(0);

		fp_seconds const tApp(now() - start()); // app run time - not using critical_now() specifically to capture when the world has actually updated.
		int64_t now_seed( last_seed );

		[[likely]] if (durations_to_seeds(last_seed, now_seed, tAppLast, tApp)) {
			SetSeed(now_seed); // reset to new seed
			last_seed = now_seed; // save as last seed to be used
			tAppLast = tApp; // update the last app time this whole comparison is about.
			no_delta_count = 0; // reset
		}
		else {
			SetSeed(last_seed); // reset to last seed used last frame

			if (no_delta_count) { // successive entry with no change //
				return; // resources uploaded to the gpu only change if there has been an update - can skip staging and re-use - no clears are done either!
			}

			++no_delta_count; // record lack of change
		}
	}
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	

	// Updating the voxel lattice by "rendering" it to the staging buffers, light probe image, etc
	VoxelWorld->Render(resource_index);
	Vulkan->checkStaticCommandsDirty(resource_index);
}

void cMinCity::Render()
{
	static constexpr size_t const MAGIC_NUM = 67108860;

	Vulkan->Render();

	if (++m_frameCount >= MAGIC_NUM) { // WRAP_AROUND SAFE (at 60 frames per second, the wrap around occurs every 12 days, 22 hours, 41 minutes, 21 seconds)
		m_frameCount = 0; // NUMBER CALCULATED >  is the highest number a 32bit float can goto while still having a resolution of +-1.0f  (frame increments by 1, and if this uint64 is converted to float in a shader for example, it needs the precision of 1 frame)
	}
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
	if (glfwWindowShouldClose(Nuklear->getGLFWWindow())) {
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
				Nuklear->setPauseProgress(*((uint32_t const* const)new_event.second));
			}
			else {
				Nuklear->setPauseProgress(0);
			}
			break;
		case eEvent::REVERT_EXCLUSIVITY:
			m_eExclusivity = eExclusivity::DEFAULT;
			break;
		case eEvent::REFRESH_LOADLIST:
			VoxelWorld->RefreshLoadList();
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
			if (eWindowQuit::PENDING & Nuklear->getLastSelectionForWindow<eWindowType::QUIT>()) {
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

	if (eExclusivity::DEFAULT == m_eExclusivity) {
		m_eExclusivity = eExclusivity::STANDBY;
	}
	SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}
void cMinCity::OnFocusRestored()
{
	m_bFocused = true;

	if (eExclusivity::STANDBY == m_eExclusivity) {
		m_eExclusivity = eExclusivity::DEFAULT;
	}

	SetForegroundWindow(glfwGetWin32Window(g_glfwwindow));  // enable foreground window for the now foreground process
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
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

__declspec(noinline) int32_t const cMinCity::SetupEnvironment() // main thread and hyperthreading cache optimizations
{
	static constexpr uint32_t const ASYNC_THREAD_STACK_SIZE = Globals::DEFAULT_STACK_SIZE >> 2;

	bool async_threads_started(false);

	int32_t num_hw_threads(-1); // -1 will have tbb automatically fallback, in cases where it can't be optimized here.00000000000000002111111111111111/
	HANDLE const hProcess(GetCurrentProcess());
	HANDLE const hThread(GetCurrentThread());

	{
		HANDLE hToken;
		if (OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		{
			SetProcessPrivilege(hToken, SE_INC_BASE_PRIORITY_NAME, true);
			SetProcessPrivilege(hToken, SE_INC_WORKING_SET_NAME, true);
			SetProcessPrivilege(hToken, SE_LOCK_MEMORY_NAME, true);
		}
	}

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

				logical_processors.reserve(32);

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

				num_hw_threads = (int32_t)hw_cores.size();

				bool const hyperthreaded = logical_processors.size() != hw_cores.size();

				unsigned long logical_processor_count_used(0);

				if (hyperthreaded) {

					// reserve both logical processors on second hardware core for main thread
					{
						unsigned long const cores[] = { logical_processors[0], logical_processors[1] };
						logical_processor_count_used = 2;
						SetThreadSelectedCpuSets(hThread, &logical_processors[1], 1);
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

				// remove main application threads cores from vector so they are not used for any new threads spawned from this point on.
				for (std::vector<DWORD>::const_iterator iter = logical_processors.cbegin(); iter != logical_processors.cend(); ) {

					if (0 == logical_processor_count_used) {
						break;
					}
					else {
						iter = logical_processors.erase(iter);
					}
					--logical_processor_count_used;
				}

				// Start background thread for long running tasks, setup using the second logical core of the last two hw cores if system is hyperthreaded.
				{
					int32_t const logical_processor_count((int32_t const)logical_processors.size());
					int32_t last_logical_second_core_index[2]{};

					last_logical_second_core_index[0] = logical_processor_count - 1;	// last, will be second core
					last_logical_second_core_index[1] = logical_processor_count - 3;	// second last would be 1st logical core so skip to the next hw core, second logical core

					// if there are not enough cores, set cores to zero  to disable special handling of cores for async threads
					unsigned long cores[2]{};

					if (last_logical_second_core_index[0] > 0) {
						cores[0] = logical_processors[last_logical_second_core_index[0]];
					}
					if (last_logical_second_core_index[1] > 0) {
						cores[1] = logical_processors[last_logical_second_core_index[1]];
					}

					async_threads_started = async_long_task::initialize(cores, ASYNC_THREAD_STACK_SIZE);
				}

				// no more limiting needed. Locking to hw cores only results in worse performance.
				
				// any new threads are limited to remaining hw cores not the same as the main thread, and if hyperthreading in on - limited to the actual hw cores first thread/logical core.
				if (logical_processors.size() > 1) {
					SetProcessDefaultCpuSets(hProcess, logical_processors.data(), (unsigned long)logical_processors.size());
				}
			}
		}
	}

	// fallback compatibility
	if (!async_threads_started) {

		unsigned long const cores[2]{}; // indicating zero will disable special handling of cores for async threads

		async_threads_started = async_long_task::initialize(cores, ASYNC_THREAD_STACK_SIZE);
		if (!async_threads_started) {
			FMT_LOG_FAIL(INFO_LOG, "Background Thread for long tasks could not be initialized! \n");
		}
	}

	// turn off windows managed power throttling off for this process
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

	return(num_hw_threads);
}

// *********************************************************************************************************************************************************************************************************************************************************************** //

namespace { // anonymous namespace private to this file only

	static inline struct { // <--make contigous in memory

		cVulkan					Vulkan;
		cTextureBoy				TextureBoy;
		cPostProcess			PostProcess;
		cNuklear				Nuklear;
		world::cVoxelWorld		VoxelWorld;
		world::cProcedural		Procedural;
		cUserInterface			UserInterface;
		cAudio					Audio;

	} _; // special access

} // privleged access only in Cleanup, CriticalCleanup & CriticalInit *no where else*


	// this is the main interface to these singletons
	// optimized to remove the "hidden" guarded access that would be present if accessed directly
	// the pointers are all constinit so no guard check is required
	// the actual instances are now properly assigned to the pointers.
constinit inline cVulkan* const					cMinCity::Vulkan{ &_.Vulkan };
constinit inline cTextureBoy* 					cMinCity::TextureBoy{ &_.TextureBoy };
constinit inline cPostProcess* 					cMinCity::PostProcess{ &_.PostProcess };
constinit inline cNuklear* const				cMinCity::Nuklear{ &_.Nuklear };
constinit inline world::cVoxelWorld* const		cMinCity::VoxelWorld{ &_.VoxelWorld };
constinit inline world::cProcedural*			cMinCity::Procedural{ &_.Procedural };
constinit inline cUserInterface* const			cMinCity::UserInterface{ &_.UserInterface };
constinit inline cAudio* const					cMinCity::Audio{ &_.Audio };

// privledged access only - do not use unless required //
cVulkan const& cMinCity::Priv_Vulkan() { return(_.Vulkan); }

// *********************************************************************************************************************************************************************************************************************************************************************** //

extern __declspec(noinline) void global_init_tbb_floating_point_env(tbb::task_scheduler_init*& TASK_INIT, int32_t const num_threads, uint32_t const thread_stack_size = 0);  // external forward decl
__declspec(noinline) void cMinCity::CriticalInit()
{
	// secure process:

	// setup zero dynamic code execution
	PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy{};
	policy.ProhibitDynamicCode = 1;

	SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &policy, sizeof(PROCESS_MITIGATION_DYNAMIC_CODE_POLICY));

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

	int32_t const num_hw_threads(SetupEnvironment());

	if (num_hw_threads > 0) { // successfully optimized to hardware "real" core count
		m_hwCoreCount = (size_t)num_hw_threads;
	}
	else { // automatic (fallback)
		m_hwCoreCount = std::max(m_hwCoreCount, (size_t)std::thread::hardware_concurrency()); // can return 0 on failure, so keep the highest core count. default of 1 is set for m_hwCoreCount.
	}

	global_init_tbb_floating_point_env(TASK_INIT, ((num_hw_threads - 1) > 0 ? num_hw_threads - 1 : num_hw_threads), Globals::DEFAULT_STACK_SIZE);

#ifndef NDEBUG
	RedirectIOToConsole(); // *** must be just before the splash screen
#else
	RedirectIOToFile(); // *** must be just before the splash screen
#endif
	
	// todo: for some odd reason, externals are missing in build if these singletons are defined * const
	// so for now they are defined * no const
	// -they are properly initialized even before this, in the code above (constinit initialization)
	cMinCity::TextureBoy = &_.TextureBoy;
	cMinCity::PostProcess = &_.PostProcess;
	cMinCity::Procedural = &_.Procedural;
}

__declspec(noinline) void cMinCity::Cleanup(GLFWwindow* const glfwwindow)
{
	async_long_task::cancel_all();

	// safe to bypass singleton pointers in CleanUp & CriticalCleanup *only* //

	// huge memory leak bugfix
	_.Vulkan.WaitPresentIdle();
	_.Vulkan.WaitDeviceIdle();

	SAFE_DELETE(City);

	_.Audio.CleanUp();
	_.Nuklear.CleanUp();
	_.PostProcess.CleanUp();
	_.VoxelWorld.CleanUp();
	_.TextureBoy.CleanUp();

	_.Vulkan.Cleanup(glfwwindow);  /// should be LAST //
}

__declspec(noinline) void cMinCity::CriticalCleanup()
{
	SAFE_DELETE(TASK_INIT);
}

int __stdcall _tWinMain(_In_ HINSTANCE hInstance,
					    _In_opt_ HINSTANCE hPrevInstance,
					    _In_ LPTSTR    lpCmdLine,
					    _In_ int       nCmdShow)
 
{  
	UNREFERENCED_PARAMETER(hPrevInstance);	
	g_hInstance = hInstance;

	cMinCity::CriticalInit();
	
	cMinCity::Initialize(g_glfwwindow);  // no need to check the state here, unless handling errors
									   // Running status is updated in this function if succesful

	// Loop waiting for the window to close, exit of program, etc
	while (cMinCity::isRunning()) {

		cMinCity::UpdateWorld();
		cMinCity::Render();
	}

	cMinCity::Cleanup(g_glfwwindow);
	cMinCity::CriticalCleanup();

	WaitIOClose();

	return(0);
}
