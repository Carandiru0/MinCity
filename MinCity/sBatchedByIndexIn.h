#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
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

// BYINDEXIN SBATCHED - *note: __streaming_store_in must be defined b4 inclusion of this header file
// eg.)   INLINE_MEMFUNC __streaming_store_in(T* const __restrict dest, T const* const __restrict src, uint32_t const (& __restrict index)[Size])
//        INLINE_MEMFUNC __streaming_store_residual_in(T* const __restrict dest, T const* const __restrict src, uint32_t const index)
//
template<typename T, uint32_t const Size>
struct sBatchedByIndexIn
{
private:
	uint32_t				indices[Size];
	uint32_t	            Count;

	// methods to use coupled with a normal pointer that is bounded by a known local range ( minimal benefit, only grouping streaming stores together in batches )
	__SAFE_BUF __inline void __vectorcall out_batched(T*& __restrict out_, T const* const __restrict in_)
	{
		__streaming_store_in<T, Size>(out_, in_, indices); // must be capable of Size output
		out_ += Size;
		Count = 0;
	}

public:
	__SAFE_BUF __inline void __vectorcall out(T*& __restrict out_, T const* const __restrict in_)
	{
		uint32_t const count(Count);
#pragma loop( ivdep )
		for (uint32_t i = 0; i < count; ++i) {
			__streaming_store_residual_in<T>(out_, in_, indices[i]);
			++out_;
		}
		Count = 0;
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(T*& __restrict out_, T const* const __restrict in_, uint32_t const index) {

		indices[Count] = index;

		if (Size == ++Count) {
			out_batched(out_, in_);
		}
	}

	constexpr sBatchedByIndexIn() = default;

	void reset() { Count = 0; }
};

// [ Extended functionality ]
template<typename T, uint32_t const Size>
struct sBatchedByIndexInReferenced : public sBatchedByIndexIn<T, Size>   // referenced thread local structure
{
	bool const referenced() const { return(_referenced); }
	void	   referenced(bool const value) { _referenced = value; sBatchedByIndexIn<T, Size>::reset(); }

private:
	bool _referenced;

public:
	constexpr sBatchedByIndexInReferenced() = default;
};

