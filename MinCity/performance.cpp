#include "pch.h"
#include "performance.h"


#ifdef PERFORMANCE_TRACKING_ENABLED

PerformanceResult& PerformanceResult::resolve()
{
	if (0 != grid_count) {
		grid_duration /= grid_count;

		grid_count = 1; // so continous use of the result over consecutive frames can be used
	}

	if (0 != total_result_size) {
		avg_iterations /= total_result_size;
		avg_iteration_duration /= total_result_size;
		avg_operation_duration /= total_result_size;

		total_result_size = 1; // so continous use of the result over consecutive frames can be used
	}
	
	return(*this);
}

void PerformanceResult::reset() // if desired, reset of all parameters to zero
{
	tbb::tbb_thread::id const zero{};
	for (uint32_t k = 0; k < MAX_THREADS_TRACKED; ++k) {
		unique_thread_ids[k] = zero;
	}
	
	avg_iterations = 0;
	max_iterations = 0;
	total_operations = 0;
	max_operation_duration = nanoseconds(0);
	avg_operation_duration = nanoseconds(0);
	max_iteration_duration = nanoseconds(0);
	avg_iteration_duration = nanoseconds(0);
	
	grid_duration = nanoseconds(0);
	grid_count = 0;

	total_result_size = 0;
}

#endif