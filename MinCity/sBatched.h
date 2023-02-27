#pragma once
#include <tbb/tbb.h>

// ##################### MAJOR THREADING CONSTANTS **************************** //

BETTER_ENUM(eThreadBatchGrainSize, uint32_t const,	// batch (*minimums*) sizes for major tbb parallel loops, representing minimum granularity of tasks in 2D or 1D

	GRID_RENDER_2D = 16U, // <--- this includes ground, is 2 dimensional and should be small (auto-partitioning - load balanced)
	MODEL = 128U,		 // minimum voxels processed in a single task, will be a lot larger as is split uniformly across affinity
	RADIAL = 32U,		 // ""        ""      ""     for a row in a single task, adaptively larger based on row size and is split uniformly across affinity
	GEN_PLOT = 1U		 // minimum unit for auto partitioner - *bugfix: must be equal to one otherwise buildings on plot intersect each other.
);

BETTER_ENUM(eStreamingBatchSize, uint32_t const,		// batch sizes for batched streaming stores

	GROUND = 8U,		// should be a multiple of eThreadBatchGrainSize::GRID_RENDER_2D
	MODEL  = 8U,		//   ""    ""  factor   "" eThreadBatchGrainSize::MODEL
	RADIAL = 8U,		//  ""    ""  factor   "" eThreadBatchGrainSize::RADIAL
	LIGHTS = 8U			// equal to maximum number of lights that can be "seeded" on a 2D plane for a single light
);


// ********************** AWESOME STREAMING STORES BATCHING IMPLEMENTATION ************************* //

// this construct significantly improves throughput of voxels, by batching the streaming stores //
// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)
// usage>
// using GroundBatch = tbb::enumerable_thread_specific< sBatched<VertexDecl::VoxelNormal, batch_size_ground>, 
//                                                      tbb::cache_aligned_allocator<sBatched<VertexDecl::VoxelNormal, batch_size_ground>>,
//                                                      tbb::ets_key_per_instance >;
//
// don't forget to out the residual/remainder at the end of the parallel process

template<typename T, uint32_t const Size>
struct sBatched
{
	T						data_set[Size];  // *BUGFIX - this structure is thread local, so use the same alignment for the type already has enough
	uint32_t				Count;

	// methods to use coupled with atomic memory pointer with a global or unknown range/bounds ( maximum benefit obtained )
	__SAFE_BUF __inline void __vectorcall out(tbb::atomic<T*>& __restrict atomic_mem_ptr)
	{
		uint32_t const count(Count);

		T* __restrict outStream = atomic_mem_ptr.template fetch_and_add<tbb::release>(count);
#pragma loop( ivdep )
		for (uint32_t i = 0; i < count; ++i) {
			__streaming_store(outStream, data_set[i]);
			++outStream;
		}
		Count = 0;
	}

	__SAFE_BUF __inline void __vectorcall emplace_back(tbb::atomic<T*>& __restrict atomic_mem_ptr, FXMVECTOR arg) {

		XMStoreFloat4A(&data_set[Count], arg);

		if (++Count >= Size) {
			out(atomic_mem_ptr);
		}
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(tbb::atomic<T*>& __restrict atomic_mem_ptr, Args&&... args) {

		data_set[Count] = std::move<T&&>(T(args...));

		if (++Count >= Size) {
			out(atomic_mem_ptr);
		}
	}

	// methods to use coupled with a normal pointer that is bounded by a known local range ( minimal benefit, only grouping streaming stores together in batches )
	__SAFE_BUF __inline void __vectorcall out(T*& __restrict mem_ptr)
	{
		uint32_t const count(Count);
#pragma loop( ivdep )
		for (uint32_t i = 0; i < count; ++i) {
			__streaming_store(mem_ptr, data_set[i]);
			++mem_ptr;
		}
		Count = 0;
	}

	__SAFE_BUF __inline void __vectorcall emplace_back(T*& __restrict mem_ptr, FXMVECTOR arg) {

		XMStoreFloat4A(&data_set[Count], arg);

		if (++Count >= Size) {
			out(mem_ptr);
		}
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(T*& __restrict mem_ptr, Args&&... args) {

		data_set[Count] = std::move<T&&>(T(args...));

		if (++Count >= Size) {
			out(mem_ptr);
		}
	}
};

// [ sBatched Extended functionality ]
template<typename T>
struct references   // for thread local instances, grouped together so they can be accessed by any thread.
{
public:
	tbb::concurrent_vector<T*> const& reference() const { return(_reference); }

	void reference(T*&& reference)
	{
		_reference.emplace_back(std::forward<T*&&>(reference));
		reference->referenced(true);
	}

	void reserve(size_t const size) {

		_reference.reserve(size);
	}

	references() = default;

private:
	tbb::concurrent_vector<T*>	_reference;
};

template<typename T, uint32_t const Size>
struct sBatchedReferenced : public sBatched<T, Size>   // referenced thread local structure
{
	bool const referenced() const { return(_referenced); }
	void	   referenced(bool const value) { _referenced = value; }

private:
	bool _referenced;
};

// see lightBuffer3D.h for example of usage.