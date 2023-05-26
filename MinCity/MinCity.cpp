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

#ifndef NDEBUG
#define CMDLINE_IMPLEMENTATION
#include <Utility/cmdline.h>
#endif

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
#define ASYNC_LONG_TASK_IMPLEMENTATION
#include <Random/superrandom.hpp>
#include <Noise/supernoise.hpp>
#include <Math/superfastmath.h>
#include <Utility/async_long_task.h>

#include "cVulkan.h"
#include "cTextureBoy.h"
#include "cPostProcess.h"
#include "cNuklear.h"
#include "cPhysics.h"
#include "cVoxelWorld.h"
#include "cProcedural.h"
#include "cUserInterface.h"
#include "cAudio.h"
#include "cCity.h"

// ^^^^ SINGLETON INCLUDES ^^^^ // b4 MinCity.h include
#define MINCITY_IMPLEMENTATION
#include "MinCity.h"

#include "RedirectIO.h"

#include <tracy.h>

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
	constinit static XMVECTOR xmZero{};
	constinit static float fZero{};
	constinit static uint8_t uZero{};
	constinit static bool bZero{};

		
	setDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET, xmZero);
	setDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR, xmZero);
	setDebugVariable(uint8_t, DebugLabel::RAMP_CONTROL_BYTE, uZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_1_BOOL, bZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_2_BOOL, bZero);
	setDebugVariable(bool, DebugLabel::TOGGLE_3_BOOL, bZero);
	setDebugVariable(float, DebugLabel::WORLD_PLANE_HEIGHT, fZero);

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

size_t const hardware_concurrency() // access helper (global function)
{
	return(MinCity::hardware_concurrency());
}

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

	bool const bForceVsync = (bool)GetPrivateProfileInt(L"RENDER_SETTINGS", L"FORCE_VSYNC", FALSE, szINIFile);
	if (bForceVsync) {
		Vulkan->ForceVsync();
	}

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
	glfwWindowHint(GLFW_CURSOR, GLFW_CURSOR_NORMAL);  // required to no capture mouse on window creation, it's disabled later at the right time.
	
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
	
	glfwSetInputMode(glfwwindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // *bugfix - ensure normal if it is not already. this becomes disabled at a later time.
	ClipCursor(nullptr); // part of bugfix
	
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

	Physics->Initialize();
	
	m_bNewEventsAllowed = true; // important for this to be enabled right here //
	VoxelWorld->Initialize();

	// deferred window show / focus set
	{
		glfwSetWindowIconifyCallback(glfwwindow, window_iconify_callback);
		glfwSetWindowFocusCallback(glfwwindow, window_focus_callback);

		glfwShowWindow(glfwwindow);
		glfwSetInputMode(glfwwindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // this captures the cursor completely - beware (for natural side scrolling camera movement controls)
		
		glfwPollEvents(); // required once here, which will enable rendering at the right time
		window_iconify_callback(glfwwindow, 0);  // required to triggger first time, ensures rendering is started
	}

	Vulkan->UpdateDescriptorSetsAndStaticCommandBuffer();

	// #### City pointer must be valid starting *here*
	City = new cCity(m_szCityName);

	VoxelWorld->Update(m_tNow, zero_time_duration, true, true);
	
	UserInterface->Initialize();

	if (!Audio->Initialize()) {
		FMT_LOG_FAIL(AUDIO_LOG, "FMOD was unable to initialize!\n");
	}
	
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
			m_tLastPause = now();
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
			path += L"/"; // required!

			std::wstring folder;			
			
			// make sure user folder .mincity exists //
			folder = path.wstring(); folder += USER_DIR;
			if (!std::filesystem::exists(folder)) {
				std::filesystem::create_directory(folder);
			}

			// make sure subfolders in user folder exist //
			folder = path.wstring(); folder += VIRTUAL_DIR;
			if (!std::filesystem::exists(folder)) {
				std::filesystem::create_directory(folder);
			}
			folder = path.wstring(); folder += SAVE_DIR;
			if (!std::filesystem::exists(folder)) {
				std::filesystem::create_directory(folder);
			}
		}
		else {

			FMT_LOG_FAIL(GAME_LOG, "Could not find users appdata folder!\n");

		}
	}

	return(path);
}

void cMinCity::OnNew()
{
	constinit static int64_t _task_id_new(0);

	async_long_task::wait_for_all(milliseconds(async_long_task::beats::frame));
	m_eExclusivity = eExclusivity::NEW;

	DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

	async_long_task::wait<background>(_task_id_new, "new"); // wait 1st on any "new" task to complete before creating a new "new" task

	SAFE_DELETE(City);
	City = new cCity(m_szCityName);

	_task_id_new = async_long_task::enqueue<background>([&] {

		DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(1));
		VoxelWorld->NewWorld();

		// make sure user can see feedback of loading, regardless of how fast the "new" generation execution took
		Sleep(milliseconds(async_long_task::beats::full_second).count());

		FMT_LOG(GAME_LOG, "New World Generated");

		DispatchEvent(eEvent::PAUSE, new bool(false));
		DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

		DispatchEvent(eEvent::REVERT_EXCLUSIVITY);
	});
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
		Sleep(milliseconds(async_long_task::beats::full_second).count());

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
		Sleep(milliseconds(async_long_task::beats::full_second).count());
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

void cMinCity::New()
{
	OnNew();
}
void cMinCity::Reset()
{
	VoxelWorld->ResetWorld();
}
void cMinCity::Load()
{
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
	int32_t const quit_state(Nuklear->getLastSelectionForWindow<eWindowType::MAIN>());

	if (!bQueryStateOnly) {

		int32_t const load_state(Nuklear->getLastSelectionForWindow<eWindowType::LOAD>());
		int32_t const save_state(Nuklear->getLastSelectionForWindow<eWindowType::SAVE>());

		if (quit_state < 0) // disabled
		{
			if (!Nuklear->enableWindow<eWindowType::MAIN>(true)) { // main window not currently shown (returns previous state)

				Pause(true); // matching unpause is in shutdown()
			}
		}
		else if ( eWindowQuit::IDLE == quit_state ) { // main window already shown, back to game

			Nuklear->enableWindow<eWindowType::MAIN>(false); // back to game
			Pause(false); // always
		}

		if (eWindowLoad::IDLE == load_state) {

			Nuklear->enableWindow<eWindowType::LOAD>(false);
			Nuklear->enableWindow<eWindowType::MAIN>(true); // back to main menu
		}

		if (eWindowSave::IDLE == save_state) { 

			Nuklear->enableWindow<eWindowType::SAVE>(false);
			Nuklear->enableWindow<eWindowType::MAIN>(true); // back to main menu
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
		Sleep(uiSleepTime - 1);						// prevent sleep if not running flag is current state
	}

	return(GradualStartUpOngoing); // signal as gradual startup complete
}

void cMinCity::UpdateWorld()
{
	ZoneScopedN("UpdateWorld");

	constinit static bool bWasPaused(false);
	constinit static tTime
		tLast{ zero_time_point },
		tLastGUI{ zero_time_point };

	duration tDeltaFixedStep(critical_delta());
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
		tNow = tNow; // resume as if no time elapsed
		m_tCriticalNow = tNow; // synchronize
		bWasPaused = false;
	}

	// *first*
	Audio->Update(); // done 1st as this is asynchronous, other tasks can occur simultaneously

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

	// add to fixed timestamp n fixed steps, while also removing the fixed step from the 
	m_tDelta = tDeltaFixedStep;
	bool bTick(false);
	while (tAccumulate >= critical_delta()) {

		m_tNow += m_tDelta;  // pause-able time step
		m_tCriticalNow += critical_delta();

		tAccumulate -= critical_delta();
		
		bool bJustLoaded(false);
		
		if (!bTick) { // limited to the fixed timestep of one iteration per frame
			
			bJustLoaded = VoxelWorld->UpdateOnce(m_tNow, m_tDelta, bPaused);

			bTick = true;
		}
		// *bugfix - it's absoletly critical to keep this in the while loop, otherwise frame rate independent motion will be broken.
		VoxelWorld->Update(m_tNow, m_tDelta, bPaused, bJustLoaded); // world/game uses regular timing, with a fixed timestep (best practice)
	}
	
	// fractional amount for render path (uniform shader variables)
	tAccumulate = std::max(zero_time_duration, tAccumulate); // *bugfix - ensure no negative values
	VoxelWorld->UpdateUniformState(time_to_float(fp_seconds(tAccumulate) / fp_seconds(critical_delta()))); // always update everyframe - this is exploited between successive renders with no update() in between (when tAccumulate < delta() or bFirstUpdate is true)

	// *fifth*
	// reserved spot
		
	// *sixth*
	if (bInputDelta) {  
		Nuklear->UpdateGUI(); // gui requires critical timing (always on, never pause gui)
		tLastGUI = tCriticalNow;
	}
	
	// *seventh*
	UserInterface->Paint(m_tNow, m_tDelta); // must be done after all updates to VoxelWorld
	
	// *last*
	ProcessEvents();

	//---------------------------------------------------------------------------------------------------------------------------------------//
	tLast = tCriticalNow;
}

void cMinCity::StageResources(uint32_t const resource_index)
{
	// resources uploaded to the gpu only change if exclusiity is not taken away from default
	// prevents threading errors while doing simultaneous tasks accessing the same data in a write(staging) and read/write(saving & loading) condition
	// instead the currently used state of resources on the gpu is re-used; does not change
	[[unlikely]] if (eExclusivity::DEFAULT != m_eExclusivity) {

		// bring down cpu & power usage when standing by //
		[[unlikely]] if (eExclusivity::STANDBY == m_eExclusivity) {
			
			Sleep(((DWORD const)duration_cast<milliseconds>(critical_delta()).count()));
		}
		return;
	}

	// Updating the voxel lattice by "rendering" it to the staging buffers, light probe image, etc
	VoxelWorld->Render(resource_index);
	Vulkan->checkStaticCommandsDirty(resource_index);
}

void cMinCity::Render()
{
	ZoneScopedN("Vulkan Render");

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

	tbb::concurrent_queue<std::pair<uint32_t, void*>> stillPendingEvents;

	while (!m_events.empty()) {
		while (m_events.try_pop(new_event))
		{
			bool stillPending(false); // if event should still be pending, or was not handled (perhaps there is a delay) set stillPending to true so that the event persists until such condition is met for the event where it is pending no more.

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
				New();
				break;
			case eEvent::RESET:
				if ((*((nanoseconds*)new_event.second) -= critical_delta()) < nanoseconds(0)) {
					Reset();
				}
				else {
					stillPending = true;
				}
				break;
			case eEvent::SHOW_IMPORT_WINDOW:
				Pause(true); // always
				Nuklear->enableWindow<eWindowType::IMPORT>(true);
				break;
			case eEvent::SHOW_MAIN_WINDOW:
				Pause(true); // always
				Nuklear->enableWindow<eWindowType::MAIN>(true);
				break;
			case eEvent::ESCAPE:
				MinCity::Quit(); // this cancels any modal windows or any active windows and gets back to the game
				break;
				//exit & expedited shutdown
			case eEvent::EXIT:
				Shutdown(Quit(true)); // exits the game if the quitting state selection and the current selection are equal.
				break;
				//last
			case eEvent::EXPEDITED_SHUTDOWN:
				Shutdown(0, true);
				break;
			default:
				break;
			}

			if (!stillPending) {
				SAFE_DELETE(new_event.second);
			}
			else {
				stillPendingEvents.emplace(new_event); // note any data paired with event is still valid
			}
		}
	}

	// append all still pending events to main queue
	while (!stillPendingEvents.empty()) {
		while (stillPendingEvents.try_pop(new_event))
		{
			m_events.push(std::move(new_event));
		}
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

void cMinCity::OnRenderComplete(uint32_t const resource_index)
{
	// anything that should not be repeated for the short final frame goes here //
	// idea is the clears are uneccessary due to RenderGrid not actually being called on the short finaL frame. so they are already cleared from the previous frame. nothing has changed but a blit of the final short frame.
	// do not put anything that isn't related to rendering in here. eg.) clearing physics here would be a bad idea.
	
	// asynchronous clears go here
	VoxelWorld->AsyncClears(resource_index);
	// corresponding wait is in RenderGrid of the next frame.
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

	int32_t hardware_concurrency(-1); // -1 will have tbb automatically fallback, in cases where it can't be optimized here.00000000000000002111111111111111/
	HANDLE const hProcess(GetCurrentProcess());
	HANDLE const hThread(GetCurrentThread());

	{
		HANDLE hToken;
		if (OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		{
			SetProcessPrivilege(hToken, SE_INC_BASE_PRIORITY_NAME, true);
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

				hardware_concurrency = (int32_t)std::max(logical_processors.size(), hw_cores.size()); // std::hardware_concurrency() matches logical processor count
				
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

				// remove main application threads cores from vector so they are not used for any new threads spawned from this point on.
				for (std::vector<DWORD>::const_iterator iter = logical_processors.cbegin(); iter != logical_processors.cend(); ) {

					if (0 == logical_processor_count_used) {
						break;
					}
	
					iter = logical_processors.erase(iter);
					--logical_processor_count_used;
				}

				// any new threads are limited to remaining hw cores not the same as the main thread
				if (logical_processors.size() > 1) {
					SetProcessDefaultCpuSets(hProcess, logical_processors.data(), (unsigned long)logical_processors.size());
				}
				
				// Start background thread for long running tasks, setup using the second logical core of the last two hw cores if system is hyperthreaded.
				{
					int32_t const logical_processor_count_remaining((int32_t const)logical_processors.size());
					int32_t last_logical_second_core_index[2]{};

					last_logical_second_core_index[0] = logical_processor_count_remaining - 1;	// last, will be second core
					last_logical_second_core_index[1] = logical_processor_count_remaining - 3;	// second last would be 1st logical core so skip to the next hw core, second logical core

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

				// - no more limiting needed. Locking to hw cores only results in worse performance.  
				// - Turning off Hyperthreading in bios, yields no thread convoying and greatly decreased cache thrashing. performmance is observed to be the same, or slightly faster with HT on, go figure....
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

	return(hardware_concurrency);
}

// *********************************************************************************************************************************************************************************************************************************************************************** //

namespace { // anonymous namespace private to this file only

	static inline struct { // <--make contigous in memory & ...

		cVulkan					Vulkan;
		cTextureBoy				TextureBoy;
		cPostProcess			PostProcess;
		cNuklear				Nuklear;
		cPhysics				Physics;
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
cVulkan* const					cMinCity::Vulkan{ &_.Vulkan };
cTextureBoy* const				cMinCity::TextureBoy{ &_.TextureBoy };
cPostProcess* const			    cMinCity::PostProcess{ &_.PostProcess };
cNuklear* const				    cMinCity::Nuklear{ &_.Nuklear };
cPhysics* const				    cMinCity::Physics{ &_.Physics };
world::cVoxelWorld* const		cMinCity::VoxelWorld{ &_.VoxelWorld };
world::cProcedural* const		cMinCity::Procedural{ &_.Procedural };
cUserInterface* const			cMinCity::UserInterface{ &_.UserInterface };
cAudio* const					cMinCity::Audio{ &_.Audio };

// privledged access only - do not use unless required //
cVulkan const& cMinCity::Priv_Vulkan() { return(_.Vulkan); }

// *********************************************************************************************************************************************************************************************************************************************************************** //

extern __declspec(noinline) void global_init_tbb_floating_point_env(tbb::task_scheduler_init*& TASK_INIT, uint32_t const thread_stack_size = 0);  // external forward decl
__declspec(noinline) void cMinCity::CriticalInit()
{
#ifdef SECURE_DYNAMIC_CODE_NOT_ALLOWED
	// secure process:
	// *bugfix - dynamic code execution required by *nvidia* driver usage in specifically - vkCmdBindDescriptorSets() uses dynamic code? Disabling the code below fixes the issue.
	// bug should be fixed in a newer driver....
	// setup zero dynamic code execution
	PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy{};
	policy.ProhibitDynamicCode = 1;

	SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &policy, sizeof(PROCESS_MITIGATION_DYNAMIC_CODE_POLICY));
#endif

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

	int32_t const hardware_concurrency(SetupEnvironment());

	if (hardware_concurrency > 0) { // successfully optimized threading in setupenvironment()
		m_hwCoreCount = hardware_concurrency;
	}
	else { // automatic (fallback)
		m_hwCoreCount = std::max(m_hwCoreCount, (size_t)std::thread::hardware_concurrency()); // can return 0 on failure, so keep the highest core count. default of 1 is set for m_hwCoreCount.
	}

	global_init_tbb_floating_point_env(TASK_INIT, Globals::DEFAULT_STACK_SIZE);

#if !defined(NDEBUG) || defined(DEBUG_CONSOLE)
	RedirectIOToConsole();	// always on in debug builds, conditionally on in release builds with DEBUG_CONSOLE
#else
	RedirectIOToFile(); // default log all output to file
#endif
	
	// **bugfix - these variables get optimized out incorrectly, this prevents that from happening
	// -they are properly initialized even before this, in the code above (constinit initialization)
	if (nullptr == cMinCity::TextureBoy) {
		FMT_LOG_FAIL(INFO_LOG, "TEX {:d}", (void* const)cMinCity::TextureBoy);
	}
	if (nullptr == cMinCity::PostProcess) {
		FMT_LOG_FAIL(INFO_LOG, "PP {:d}", (void* const)cMinCity::PostProcess);
	}
	if (nullptr == cMinCity::Procedural) {
		FMT_LOG_FAIL(INFO_LOG, "PRO {:d}", (void* const)cMinCity::Procedural);
	}
}

__declspec(noinline) void cMinCity::Cleanup(GLFWwindow* const glfwwindow)
{
	async_long_task::cancel_all(); // cancel any outstanding or running tasks

	// safe to bypass singleton pointers in CleanUp & CriticalCleanup *only* //

	// huge memory leak bugfix
	_.Vulkan.WaitDeviceIdle();

	Sleep(10); // *bugfix - sometimes a huge power spike can happen here while shutting down, resulting in the psu experiencing uneccessary stress. slowing it down with an unnoticable amount of time.
	
	async_long_task::wait_for_all(); // *bugfix for safe cleanup, all background tasks must complete

	SAFE_DELETE(City);

	_.Audio.CleanUp();
	_.Nuklear.CleanUp();
	_.PostProcess.CleanUp();
	_.Physics.CleanUp();
	_.VoxelWorld.CleanUp();
	_.TextureBoy.CleanUp();

	Sleep(10); // *bugfix - sometimes a huge power spike can happen here while shutting down, resulting in the psu experiencing uneccessary stress. slowing it down with an unnoticable amount of time.
	
	_.Vulkan.WaitDeviceIdle();
	_.Vulkan.Cleanup(glfwwindow);  /// should be LAST //
}

__declspec(noinline) void cMinCity::CriticalCleanup()
{
	Sleep(10); // *bugfix - sometimes a huge power spike can happen here while shutting down, resulting in the psu experiencing uneccessary stress. slowing it down with an unnoticable amount of time.
	
	SAFE_DELETE(TASK_INIT);
}

int __stdcall _tWinMain(_In_ HINSTANCE hInstance,
					    [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance,
						[[maybe_unused]] _In_ LPTSTR lpCmdLine,
						[[maybe_unused]] _In_ int nCmdShow)
 
{  
	(void)UNREFERENCED_PARAMETER(hPrevInstance);
	(void)UNREFERENCED_PARAMETER(lpCmdLine);
	(void)UNREFERENCED_PARAMETER(nCmdShow);
	g_hInstance = hInstance;

	__SetThreadName("main thread");

	cMinCity::CriticalInit();

#ifndef NDEBUG // use quick_exit(0) at point where bug has been successfully passed, quick_exit(1) happens in the validation callback when BREAK_ON_VALIDATION_ERROR is equal to 1 in vku.hpp (for isolating sync validation errors with automation using debug_sync program)
	cmdline::arguments(__wargv, __argc);
#endif
		
	cMinCity::Initialize(g_glfwwindow);  // no need to check the state here, unles.s handling errors
										// Running status is updated in this function if succesful

	// Loop waiting for the window to close, exit of program, etc
	while (cMinCity::isRunning()) {

		ZoneScopedN("Root");

		cMinCity::UpdateWorld();
		cMinCity::Render();
		FrameMark;
	}

	cMinCity::Cleanup(g_glfwwindow);
	cMinCity::CriticalCleanup();


	WaitIOClose();

	return(0);
}
