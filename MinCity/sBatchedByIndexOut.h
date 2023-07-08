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

// BYINDEXOUT SBATCHED - *note: __streaming_store_out & __streaming_store_residual_out must be defined b4 inclusion of this header file
// requires:  INLINE_MEMFUNC __streaming_store_out(T* const __restrict dest, T const* const __restrict src, uint32_t const (& __restrict index)[Size])
//            INLINE_MEMFUNC __streaming_store_residual_out(T* const __restrict dest, T const* const __restrict src, uint32_t const index) 
//
template<typename T, uint32_t const Size>
struct sBatchedByIndexOut
{
private:
	T                       data_set[Size];
	uint32_t				indices[Size];
	uint32_t             	Count;

	__SAFE_BUF __inline void __vectorcall out_batched(T* const __restrict out_)
	{
		__streaming_store_out<T, Size>(out_, data_set, indices); // must be capable of Size output
		Count = 0;
	}

public:
	__SAFE_BUF __inline void __vectorcall out(T* const __restrict out_) // use this at the end of the process - done in commit()
	{
		uint32_t const count(Count);
#pragma loop( ivdep )
		for (uint32_t i = 0; i < count; ++i) { // in case there are less than Size elements left, output individually.
			__streaming_store_residual_out<T>(out_, data_set[i], indices[i]);
		}
		Count = 0;
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(T* const __restrict out_, uint32_t const index, Args&&... args) {

		uint32_t const count(Count);

		indices[count] = index;
		data_set[count] = std::move<T&&>(T(args...));

		if (Size == ++Count) {
			out_batched(out_); // batched output
		}
	}

	constexpr sBatchedByIndexOut() = default;

	void reset() { Count = 0; }
};

// [ Extended functionality ]
template<typename T, uint32_t const Size>
struct sBatchedByIndexOutReferenced : public sBatchedByIndexOut<T, Size>  // referenced thread local structure
{
	bool const referenced() const { return(_referenced); }
	void	   referenced(bool const value) { _referenced = value; sBatchedByIndexOut<T, Size>::reset(); }

private:
	bool _referenced;

public:
	constexpr sBatchedByIndexOutReferenced() = default;
};

