#pragma once
#include <tbb/tbb.h>
#include "references.h"

// ********************** AWESOME STREAMING STORES BATCHING IMPLEMENTATION ************************* //

// this construct significantly improves throughput of voxels, by batching the streaming stores //
// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)
// usage>
// using GroundBatch = tbb::enumerable_thread_specific< sBatched<VertexDecl::VoxelNormal, batch_size_ground>, 
//                                                      tbb::cache_aligned_allocator<sBatched<VertexDecl::VoxelNormal, batch_size_ground>>,
//                                                      tbb::ets_key_per_instance >;
//
// don't forget to out the residual/remainder at the end of the parallel process

// REGULAR SBATCHED - *note: __streaming_store must be defined b4 inclusion of this header file
// eg.)   INLINE_MEMFUNC __streaming_store(T* const __restrict dest, T const& __restrict src)
//
template<typename T, uint32_t const Size>
struct sBatched
{
private:
	T						data_set[Size];  // *BUGFIX - this structure is thread local, so use the same alignment for the type already has enough
	uint32_t      			Count;

public:
	// methods to use coupled with atomic memory pointer with a global or unknown range/bounds ( maximum benefit obtained )
	__SAFE_BUF __inline void __vectorcall out(tbb::atomic<T*>& __restrict atomic_mem_ptr)
	{
		uint32_t const count(Count);

		T* __restrict outStream = atomic_mem_ptr.fetch_and_add(count);
#pragma loop( ivdep )
		for (uint32_t i = 0; i < count; ++i) {
			__streaming_store<T>(outStream, data_set[i]);
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
			__streaming_store<T>(mem_ptr, data_set[i]);
			++mem_ptr;
		}
		Count = 0;
	}

	__SAFE_BUF __inline void __vectorcall emplace_back(T*& __restrict mem_ptr, FXMVECTOR arg) {

		XMStoreFloat4A(&data_set[Count], arg);

		if (Size == ++Count) {
			out(mem_ptr);
		}
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(T*& __restrict mem_ptr, Args&&... args) {

		data_set[Count] = std::move<T&&>(T(args...));

		if (Size == ++Count) {
			out(mem_ptr);
		}
	}

	constexpr sBatched()
		: Count{}
	{}
};

// [ Extended functionality ]
template<typename T, uint32_t const Size>
struct sBatchedReferenced : public sBatched<T, Size>   // referenced thread local structure
{
	bool const referenced() const { return(_referenced); }
	void	   referenced(bool const value) { _referenced = value; }

private:
	bool _referenced;

public:
	constexpr sBatchedReferenced() = default;
};

