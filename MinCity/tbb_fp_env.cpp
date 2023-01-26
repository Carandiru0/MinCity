#include <xmmintrin.h>
#include <pmmintrin.h>
#include <tbb/tbb.h>
#include <locale.h>

// CALL from any thread other than main thread
__declspec(noinline) void local_init_tbb_floating_point_env()
{
	// important optimization 
	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	_wsetlocale(LC_ALL, L"en-US");
	
	// Set Desired Behaviour of Denormals //
	uint_fast32_t control_word;
	_controlfp_s(&control_word, _DN_FLUSH, _MCW_DN);
	_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
	_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

// CALL from MAIN THREAD //
__declspec(noinline) void global_init_tbb_floating_point_env(tbb::task_scheduler_init*& TASK_INIT, uint32_t const thread_stack_size = 0)
{
	local_init_tbb_floating_point_env();
	
	//!
	//! Init the tbb task scheduler here allows correct floating point settings to be captured correctly
	//!
	TASK_INIT = new tbb::task_scheduler_init(-1, thread_stack_size); // *bugfix: "-1" is automatic mode, no longer oversubscribed, and is recommended for production release!

	// tbb has captured the floating point context, and is in sync with same fp settings as the main thread
	// prevent unloading of tbb
	HMODULE hModule{};
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, L"tbb", &hModule);
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, L"tbbmalloc", &hModule);

}

#pragma fenv_access (off)



