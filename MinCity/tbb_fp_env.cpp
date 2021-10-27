#include <xmmintrin.h>
#include <pmmintrin.h>
#include <tbb/tbb.h>

// CALL from any thread other than main thread
__declspec(noinline) void local_init_tbb_floating_point_env()
{
	// Set Desired Behaviour of Denormals //
	uint_fast32_t control_word;
	_controlfp_s(&control_word, _DN_FLUSH, _MCW_DN);
	_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
	_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

// CALL from MAIN THREAD //
__declspec(noinline) void global_init_tbb_floating_point_env(tbb::task_scheduler_init*& TASK_INIT, int32_t const num_threads, uint32_t const thread_stack_size = 0)
{
	local_init_tbb_floating_point_env();

	//!
	//! Init the tbb task scheduler here allows correct floating point settings to be captured correctly
	//!
	TASK_INIT = new tbb::task_scheduler_init(num_threads, thread_stack_size);

	// tbb has captured the floating point context, and is in sync with same fp settings as the main thread
	// prevent unloading of tbb
	HMODULE hModule{};
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, L"tbb", &hModule);
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, L"tbbmalloc", &hModule);

}

#pragma fenv_access (off)



