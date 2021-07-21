#pragma once

// ***********##############  use scalable_aligned_malloc / scalable_aligned_free to guarentee aligment ###################************* //
// note: alignas with usage of new() does not guarentee alignment
#undef _mm_mfence
#pragma intrinsic(__faststorefence) // https://docs.microsoft.com/en-us/cpp/intrinsics/faststorefence?view=msvc-160 -- The effect is comparable to but faster than the _mm_mfence intrinsic on all x64 platforms. Not faster than _mm_sfence.
#pragma intrinsic(_mm_sfence)
#pragma intrinsic(_mm_prefetch)
#pragma intrinsic(memcpy)	// ensure intrinsics are used for un-aligned data
#pragma intrinsic(memset)	// ensure intrinsics are used for un-aligned data

#include <stdint.h>
#include "Declarations.h"  // vertex declarations

static constexpr size_t const CACHE_LINE_BYTES = 64ULL;

#define __PURE __declspec(noalias)
#define __SAFE_BUF __declspec(safebuffers)

// forward decl for specializations //
namespace DirectX {
	struct alignas(16) XMFLOAT4A;
}// end ns

// pre-compiled with optimizations on in debug builds only for optimize.cpp
// current workaround for dog-slow for loops and copying / setting data
// fixes the huge degradation of speed while debuging, release mode is unaffected by this bug

// further specialization for extreme optimization purposes only

#define INTRINSIC_MEM_ALLOC_FUNC extern inline __PURE __SAFE_BUF __declspec(restrict) void* const const __restrict __vectorcall
#define INTRINSIC_MEM_FREE_FUNC extern inline __PURE __SAFE_BUF void
#define INTRINSIC_MEMFUNC extern inline __PURE __SAFE_BUF void __vectorcall
#define INLINE_MEMFUNC static __forceinline __PURE __SAFE_BUF void __vectorcall

// [fences] //
INLINE_MEMFUNC __streaming_store_fence();

// [clears]
INTRINSIC_MEMFUNC __memclr_aligned_32_stream(void* const const __restrict dest, size_t const bytes);
INTRINSIC_MEMFUNC __memclr_aligned_32_store(void* const const __restrict dest, size_t const bytes);
INTRINSIC_MEMFUNC __memclr_aligned_16_stream(void* const const __restrict dest, size_t const bytes);
INTRINSIC_MEMFUNC __memclr_aligned_16_store(void* const const __restrict dest, size_t const bytes);

// [specializations] with specific optimizations
INTRINSIC_MEMFUNC __memclr_aligned_stream(DirectX::XMFLOAT4A* __restrict dest, size_t numelements);

INLINE_MEMFUNC __streaming_store(DirectX::XMFLOAT4A* const __restrict dest, FXMVECTOR const src);
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelNormal* const __restrict dest, VertexDecl::VoxelNormal const&& __restrict src);
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelNormal* const __restrict dest, VertexDecl::VoxelNormal const* const __restrict src);
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelDynamic* const __restrict dest, VertexDecl::VoxelDynamic const&& __restrict src);


// [copies]
INTRINSIC_MEMFUNC __memcpy_aligned_32_stream(void* const __restrict dest, void const* const __restrict src, size_t const bytes);
INTRINSIC_MEMFUNC __memcpy_aligned_32_store(void* const __restrict dest, void const* const __restrict src, size_t const bytes);
INTRINSIC_MEMFUNC __memcpy_aligned_16_stream(void* const __restrict dest, void const* const __restrict src, size_t const bytes);
INTRINSIC_MEMFUNC __memcpy_aligned_16_store(void* const __restrict dest, void const* const __restrict src, size_t const bytes);

// [very large copies]
INTRINSIC_MEMFUNC __memcpy_aligned_32_stream_threaded(void* const __restrict dest, void const* const __restrict src, size_t bytes);
INTRINSIC_MEMFUNC __memcpy_aligned_16_stream_threaded(void* const __restrict dest, void const* const __restrict src, size_t bytes);

// [large allocations] 
// requiring physical memory and no page faults (never swapped to page file)
// Important Notes:
// SetProcessWorkingSetSize() Windows API function should be called if number of pages in program are to exceed the defaults provided to the process by the OS
// only for large allocations which are always allocated as pages of a minimum size, so there is always large overhead
// should never be done during real-time processing, only at initialization stages
// *** memory is intended for duration of program *** 
// *** does NOT need to be deleted/freed ***
// *** automagically freed by OS when process exits *** ///
INTRINSIC_MEM_ALLOC_FUNC __memalloc_large(size_t const bytes, size_t const alignment = 0ULL);
INTRINSIC_MEM_FREE_FUNC  __memfree_large(void* const const __restrict data, size_t const bytes, size_t const alignment = 0ULL);

// for prefetching a memory into virtual memory, ensuring it is not paged from disk multiple times if said memory is accessed for example a memory mapped file
// this avoids a continous amount of page faults that cause delays of millions of cycles potentially by ensuring the data pointed to by memory is cached before the first access
INTRINSIC_MEM_FREE_FUNC  __prefetch_vmem(void const* const __restrict address, size_t const bytes);

// templated versions that automatically resolve streaming or storing variant for when at compile time the number of elements is known
static constexpr size_t const MEM_CACHE_LIMIT = (1ULL << 15ULL); // (set for 32KB - size of L1 cache on Intel Haswell processors)

#define PUBLIC_MEMFUNC static __forceinline __declspec(noalias) __SAFE_BUF void __vectorcall

// ########### CLEAR ############ //

template<size_t const numelements, bool const stream = false, typename T>
PUBLIC_MEMFUNC __memclr_aligned_32(T* const __restrict dest)					// nunmelements known at compile time allows check for stream at compile time
{
	constexpr size_t const size(sizeof(T) * numelements);

	if constexpr (stream || size > (MEM_CACHE_LIMIT))
	{
		__memclr_aligned_32_stream((void* const const __restrict)dest, size);
	}
	else {

		__memclr_aligned_32_store((void* const const __restrict)dest, size);
	}
}
template<bool const stream, typename T>
PUBLIC_MEMFUNC __memclr_aligned_32(T* const __restrict dest, size_t const size)	// must explicitly state stream / store
{
	if constexpr (stream)
	{
		__memclr_aligned_32_stream((void* const const __restrict)dest, size);
	}
	else {

		__memclr_aligned_32_store((void* const const __restrict)dest, size);
	}
}

template<size_t const numelements, bool const stream = false, typename T>
PUBLIC_MEMFUNC __memclr_aligned_16(T* const __restrict dest)					// nunmelements known at compile time allows check for stream at compile time
{
	constexpr size_t const size(sizeof(T) * numelements);

	if constexpr (stream || size > (MEM_CACHE_LIMIT))
	{
		__memclr_aligned_16_stream((void* const const __restrict)dest, size);
	}
	else {

		__memclr_aligned_16_store((void* const const __restrict)dest, size);
	}
}
template<bool const stream, typename T>
PUBLIC_MEMFUNC __memclr_aligned_16(T* const __restrict dest, size_t const size)	// must explicitly state stream / store
{
	if constexpr (stream)
	{
		__memclr_aligned_16_stream((void* const const __restrict)dest, size);
	}
	else {

		__memclr_aligned_16_store((void* const const __restrict)dest, size);
	}
}

// ########### COPY ############ //

template<size_t const numelements, bool const stream = false, typename T>	// nunmelements known at compile time allows check for stream at compile time
PUBLIC_MEMFUNC __memcpy_aligned_32(T* const __restrict dest, T const* const __restrict src)
{
	constexpr size_t const size(sizeof(T) * numelements);

	if constexpr (stream || size > (MEM_CACHE_LIMIT)) {

		__memcpy_aligned_32_stream((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
	else {

		__memcpy_aligned_32_store((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
}

template<bool const stream, typename T>	// must explicitly state stream / store
PUBLIC_MEMFUNC __memcpy_aligned_32(T* const __restrict dest, T const* const __restrict src, size_t const size) 
{
	if constexpr (stream) {
	
		__memcpy_aligned_32_stream((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
	else {

		__memcpy_aligned_32_store((void* const const __restrict)dest, (uint8_t const * const __restrict)src, size);
	}
}

template<size_t const numelements, bool const stream = false, typename T>	// nunmelements known at compile time allows check for stream at compile time
PUBLIC_MEMFUNC __memcpy_aligned_16(T* const __restrict dest, T const* const __restrict src)
{
	constexpr size_t const size(sizeof(T) * numelements);

	if constexpr (stream || size > (MEM_CACHE_LIMIT)) {

		__memcpy_aligned_16_stream((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
	else {

		__memcpy_aligned_16_store((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
}

template<bool const stream, typename T>	// must explicitly state stream / store
PUBLIC_MEMFUNC __memcpy_aligned_16(T* const __restrict dest, T const* const __restrict src, size_t const size)
{
	if constexpr (stream) {

		__memcpy_aligned_16_stream((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
	else {

		__memcpy_aligned_16_store((void* const const __restrict)dest, (void const* const const __restrict)src, size);
	}
}


// INLINE_MEMFUN - defintions //

INLINE_MEMFUNC __streaming_store(DirectX::XMFLOAT4A* const __restrict dest, FXMVECTOR const src)
{
	_mm_stream_ps((float* const __restrict)std::assume_aligned<16>(dest), src);
}

INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelNormal* const __restrict dest, VertexDecl::VoxelNormal const&& __restrict src)
{
	// VertexDecl::VoxelNormal works with _mm256 (fits size), 8 floats total / element
	_mm256_stream_ps((float* const __restrict)std::assume_aligned<32>(dest), _mm256_set_m128(src.uv_vr, src.worldPos));
}
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelNormal* const __restrict dest, VertexDecl::VoxelNormal const* const __restrict src)
{
	// VertexDecl::VoxelNormal works with _mm256 (fits size), 8 floats total / element
	_mm256_stream_si256((__m256i* const __restrict)std::assume_aligned<32>(dest), _mm256_stream_load_si256((__m256i const* const __restrict)std::assume_aligned<32>(src)));
}

INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelDynamic* __restrict dest, VertexDecl::VoxelDynamic const&& __restrict src)
{
	dest = std::assume_aligned<16>(dest);

	_mm_stream_ps((float* const __restrict)dest, src.worldPos);
	_mm_stream_ps(((float* const __restrict)dest) + 4, src.uv_vr);
	_mm_stream_ps(((float* const __restrict)dest) + 8, src.orient_reserved);

	/* bug, read access violation - not the correct alignment!
	// Base Class VertexDecl::VoxelNormal works with __m256 (fits size), 8 floats total / element
	_mm256_stream_ps((float* const __restrict)dest, _mm256_set_m128(src.uv_vr, src.worldPos));
	// Derived Class VertexDecl::VoxelDynamic remainder works with __m128 (fits size), 4 floats total / ele
	_mm_stream_ps(((float* const __restrict)dest) + 8, src.orient_reserved);
	*/
}

INLINE_MEMFUNC __streaming_store_fence()
{
	_mm_sfence();
}