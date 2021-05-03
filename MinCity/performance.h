#pragma once
#include "globals.h"

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#define PERFORMANCE_TRACKING_ENABLED
#define MAX_THREADS_TRACKED 24
#endif

#ifdef PERFORMANCE_TRACKING_ENABLED
#include "tTime.h"
#include <tbb/tbb.h>
#include <set>

// nanoseconds used to ehance precision - division on averages etc, then on display is casted after the divide to microseconds
typedef struct Performance
{
	size_t	operations,
			iterations;

	nanoseconds	operation_duration,
				iteration_duration;

	tbb::tbb_thread::id thread_id;

	Performance()
		: operations(0), iterations(0),
		operation_duration(nanoseconds(0)),
		iteration_duration(nanoseconds(0))
	{
	}
} Performance;
using PerformanceType = tbb::enumerable_thread_specific< Performance >;

volatile struct PerformanceResult
{
	size_t	total_operations,
			max_iterations,
			avg_iterations;

	nanoseconds 
		max_operation_duration,
		avg_operation_duration,
		max_iteration_duration,
		avg_iteration_duration;

	nanoseconds
		grid_duration;

	tbb::atomic<tbb::tbb_thread::id> unique_thread_ids[MAX_THREADS_TRACKED]{};

	size_t total_result_size,
		   grid_count;

	size_t const getThreadCount() const {
		size_t count(0);
		tbb::tbb_thread::id const zero{};

		for (uint32_t i = 0; i < MAX_THREADS_TRACKED; ++i) {
			if (zero != unique_thread_ids[i]) {
				++count;
			}
		}
		return(count);
	}

	void merge_thread_id(tbb::tbb_thread::id const current_id)
	{
		bool found(false);
		size_t current_count(getThreadCount());

		for (uint32_t k = 0; k < MAX_THREADS_TRACKED; ++k) {
			if (current_id == unique_thread_ids[k]) {
				found = true;
				break;
			}
		}

		if (!found) {
			//guard
			if (current_count < MAX_THREADS_TRACKED) {
				unique_thread_ids[current_count++] = current_id;
			}
			else {
				FMT_LOG_FAIL("PERF", "monitoring thread id count exceeds maximum count");
			}
		}
	}

	PerformanceResult& operator+=(PerformanceType const& operation)
	{
		PerformanceResult combined_operation;
		for (PerformanceType::const_iterator i = operation.begin(); i != operation.end(); ++i) {

			combined_operation.total_operations += i->operations;
			combined_operation.max_iterations = std::max(combined_operation.max_iterations, i->iterations);
			combined_operation.avg_iterations += i->iterations;

			combined_operation.max_operation_duration = std::max(combined_operation.max_operation_duration, i->operation_duration);
			combined_operation.avg_operation_duration += i->operation_duration;
			combined_operation.max_iteration_duration = std::max(combined_operation.max_iteration_duration, i->iteration_duration);
			combined_operation.avg_iteration_duration += i->iteration_duration;

			// merge, uniques appended
			merge_thread_id(i->thread_id);
			
		}
		// Resolve the combined set
		size_t const size = operation.size();
		combined_operation.avg_iterations /= size;
		combined_operation.avg_iteration_duration /= size;
		combined_operation.avg_operation_duration /= size;

		// Add combined set to *this
		total_operations += combined_operation.total_operations;
		max_iterations = std::max(max_iterations, combined_operation.max_iterations);
		avg_iterations += combined_operation.avg_iterations;

		max_operation_duration = std::max(max_operation_duration, combined_operation.max_operation_duration);
		avg_operation_duration += combined_operation.avg_operation_duration;
		max_iteration_duration = std::max(max_iteration_duration, combined_operation.max_iteration_duration);
		avg_iteration_duration += combined_operation.avg_iteration_duration;

		// Resolve of *this should happen only after all the += of PerformanceTypes is completed
		++total_result_size;
		return(*this);
	}

	PerformanceResult& operator+=(PerformanceResult& result) // for correct result do not resolve input result first, only resolve *this
	{
		total_operations += result.total_operations;
		max_iterations = std::max(max_iterations, result.max_iterations);
		avg_iterations += result.avg_iterations;

		max_operation_duration = std::max(max_operation_duration, result.max_operation_duration);
		avg_operation_duration += result.avg_operation_duration;
		max_iteration_duration = std::max(max_iteration_duration, result.max_iteration_duration);
		avg_iteration_duration += result.avg_iteration_duration;

		for (uint32_t k = 0; k < MAX_THREADS_TRACKED; ++k) {
			merge_thread_id(result.unique_thread_ids[k]);
		}

		// Resolve of *this should happen only after all the += of PerformanceTypes is completed
		total_result_size += result.total_result_size;

		if (nanoseconds(0) != result.grid_duration) {
			grid_duration += result.grid_duration;
			++grid_count;
		}

		return(*this);
	}
	PerformanceResult& resolve();
	void reset();

	PerformanceResult()
		: total_operations(0),
		  max_iterations(0), avg_iterations(0),
		  max_operation_duration(0), avg_operation_duration(0),
		  max_iteration_duration(0), avg_iteration_duration(0),
		grid_duration(0), total_result_size(0), grid_count(0)
	{
	}

};
#endif
