#include "pch.h"

#include "optimized.h"

#define INTRINSIC_MEMFUNC_IMPL inline __PURE __SAFE_BUF void __vectorcall

#define stream true
#define store  false

// ########### CLEAR ############ //

namespace internal_mem
{
	template<bool const streaming, typename T>
	static INTRINSIC_MEMFUNC_IMPL __memclr_aligned(T* __restrict dest, size_t bytes)
	{
		__m256i const xmZero(_mm256_setzero_si256());

		if constexpr (std::is_same_v<T, __m256i>) { // size does fit into multiple of 32bytes

			static constexpr size_t const element_size = sizeof(__m256i);
			__builtin_assume_aligned(dest, 32ULL);

#ifndef NDEBUG
			assert_print(0 == (bytes % element_size), "__memclr_aligned:  size not a multiple of sizeof(__m256i) - data is not aligned\n");
			static_assert(0 == (sizeof(T) % element_size), "__memclr_aligned:  element not divisable by sizeof(__m256i)\n");
#endif
			
 // 128 bytes/iteration
			while (bytes >= (CACHE_LINE_BYTES << 1))
			{
				if constexpr (streaming) {
					_mm256_stream_si256(dest, xmZero);	// vertex buffers are very large, using streaming stores
					_mm256_stream_si256(dest + 1ULL, xmZero);	// vertex buffers are very large, using streaming stores
					_mm256_stream_si256(dest + 2ULL, xmZero);	// vertex buffers are very large, using streaming stores
					_mm256_stream_si256(dest + 3ULL, xmZero);	// vertex buffers are very large, using streaming stores
				}
				else {
					_mm256_store_si256(dest, xmZero);
					_mm256_store_si256(dest + 1ULL, xmZero);
					_mm256_store_si256(dest + 2ULL, xmZero);
					_mm256_store_si256(dest + 3ULL, xmZero);
				}
				dest += 4ULL;
				bytes -= (CACHE_LINE_BYTES << 1);
			}

 // 32 bytes/iteration
			while (bytes >= element_size)
			{
				if constexpr (streaming) {
					_mm256_stream_si256(dest, xmZero);	// vertex buffers are very large, using streaming stores
				}
				else {
					_mm256_store_si256(dest, xmZero);
				}
				++dest;
				bytes -= element_size;
			}
		}
		else { // size does not fit into multiple of 32bytes

			static constexpr size_t const element_size = sizeof(__m128i);
			__builtin_assume_aligned(dest, 16ULL);

#ifndef NDEBUG
			assert_print(0 == (bytes % element_size), "__memclr_aligned:  size not a multiple of sizeof(__m128i) - data is not aligned\n");
			static_assert(0 == (sizeof(T) % element_size), "__memclr_aligned:  element not divisable by sizeof(__m128i)\n");
#endif

 // 128 bytes/iteration
			while (bytes >= (CACHE_LINE_BYTES << 1))
			{
				if constexpr (streaming) {
					_mm_stream_si128(dest, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 1ULL, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 2ULL, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 3ULL, _mm256_castsi256_si128(xmZero)); 
					_mm_stream_si128(dest + 4ULL, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 5ULL, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 6ULL, _mm256_castsi256_si128(xmZero));
					_mm_stream_si128(dest + 7ULL, _mm256_castsi256_si128(xmZero));
				}
				else {
					_mm_store_si128(dest, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 1ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 2ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 3ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 4ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 5ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 6ULL, _mm256_castsi256_si128(xmZero));
					_mm_store_si128(dest + 7ULL, _mm256_castsi256_si128(xmZero));
				}
				dest += 8ULL;
				bytes -= (CACHE_LINE_BYTES << 1);
			}

 // 16bytes / iteration
			while (bytes >= element_size)
			{
				if constexpr (streaming) {
					_mm_stream_si128(dest, _mm256_castsi256_si128(xmZero));	
				}
				else {
					_mm_store_si128(dest, _mm256_castsi256_si128(xmZero));
				}
				++dest;
				bytes -= element_size;
			}
		}
	}
}//end ns

INTRINSIC_MEMFUNC_IMPL __memclr_aligned_32_stream(void* const __restrict dest, size_t const bytes)
{
	internal_mem::__memclr_aligned<stream>((__m256i* const __restrict)dest, bytes);
}
INTRINSIC_MEMFUNC_IMPL __memclr_aligned_32_store(void* const __restrict dest, size_t const bytes)
{
	internal_mem::__memclr_aligned<store>((__m256i* const __restrict)dest, bytes);
}

INTRINSIC_MEMFUNC_IMPL __memclr_aligned_16_stream(void* const __restrict dest, size_t const bytes)
{
	internal_mem::__memclr_aligned<stream>((__m128i* const __restrict)dest, bytes);
}
INTRINSIC_MEMFUNC_IMPL __memclr_aligned_16_store(void* const __restrict dest, size_t const bytes)
{
	internal_mem::__memclr_aligned<store>((__m128i* const __restrict)dest, bytes);
}

// - specializations - //
INTRINSIC_MEMFUNC_IMPL __memclr_aligned_stream(XMFLOAT4A* __restrict dest, size_t numelements)  // hack for processing double XMFLOAT4A's / iteration
{		
	static constexpr size_t const CACHE_LINE_ELEMENTS = CACHE_LINE_BYTES / sizeof(XMFLOAT4A);

	__m256i const xmZero(_mm256_setzero_si256()); 

	__builtin_assume_aligned(dest, 32ULL);

 // 128 bytes/iteration
	while (numelements >= (CACHE_LINE_ELEMENTS << 1))  // // 8x XMFLOAT4A / iteration
	{
		_mm256_stream_si256((__m256i* const __restrict)dest, xmZero);
		_mm256_stream_si256((__m256i* const __restrict)(dest + 2ULL), xmZero);
		_mm256_stream_si256((__m256i* const __restrict)(dest + 4ULL), xmZero);
		_mm256_stream_si256((__m256i* const __restrict)(dest + 6ULL), xmZero);

		dest += 8ULL;
		numelements -= (CACHE_LINE_ELEMENTS << 1);
	}

 // 32 bytes/iteration
	while (numelements >= 2ULL) // // 2x XMFLOAT4A / iteration
	{
		_mm256_stream_si256((__m256i*const __restrict)dest, xmZero);
		dest += 2ULL;
		numelements -= 2ULL;
	}

 // 16 bytes/iteration
	while (0ULL != numelements) {	// residual if what is left not multiple // 1x XMFLOAT4A / iteration
		_mm_stream_si128((__m128i* const __restrict)(dest), _mm256_castsi256_si128(xmZero));
		++dest;
		--numelements;
	}
}


// ########### COPY ############ //
namespace internal_mem
{
	template<bool const streaming, typename T>
	static INTRINSIC_MEMFUNC_IMPL __memcpy_aligned(T* __restrict dest, T const* __restrict src, size_t bytes)
	{
		if constexpr (std::is_same_v<T, __m256i>) {
			static constexpr size_t const element_size = sizeof(__m256i);
			__builtin_assume_aligned(dest, 32ULL);

#ifndef NDEBUG
			assert_print(0 == (bytes % element_size), "__memcpy_aligned:  size not a multiple of sizeof(__m256i) - data is not aligned\n");
			static_assert(0 == (sizeof(T) % element_size), "__memcpy_aligned:  element not divisable by sizeof(__m256i)\n");
#endif

 // 128 bytes / iteration
			while (bytes >= (CACHE_LINE_BYTES << 1))
			{
				if constexpr (streaming) {
					_mm_prefetch((const CHAR*)(src + 2), _MM_HINT_NTA);
					_mm256_stream_si256(dest, _mm256_stream_load_si256(src));
					_mm256_stream_si256(dest + 1ULL, _mm256_stream_load_si256(src + 1ULL));
					_mm_prefetch((const CHAR*)(src + 2), _MM_HINT_NTA);
					_mm256_stream_si256(dest + 2ULL, _mm256_stream_load_si256(src + 2ULL));
					_mm256_stream_si256(dest + 3ULL, _mm256_stream_load_si256(src + 3ULL));
				}
				else {
					_mm_prefetch((const CHAR*)(src + 2), _MM_HINT_NTA);
					_mm256_store_si256(dest, _mm256_load_si256(src));
					_mm256_store_si256(dest + 1ULL, _mm256_load_si256(src + 1ULL));
					_mm_prefetch((const CHAR*)(src + 2), _MM_HINT_NTA);
					_mm256_store_si256(dest + 2ULL, _mm256_load_si256(src + 2ULL));
					_mm256_store_si256(dest + 3ULL, _mm256_load_si256(src + 3ULL));
				}
				dest += 4ULL; src += 4ULL;
				bytes -= (CACHE_LINE_BYTES << 1);
			}

 // 32 bytes / iteration
			while (bytes >= element_size)
			{
				if constexpr (streaming) {
					_mm256_stream_si256(dest, _mm256_stream_load_si256(src));
				}
				else {
					_mm256_store_si256(dest, _mm256_load_si256(src));
				}
				++dest; ++src;
				bytes -= element_size;
			}
		}
		else {
			static constexpr size_t const element_size = sizeof(__m128i);
			__builtin_assume_aligned(dest, 16ULL);

#ifndef NDEBUG
			assert_print(0 == (bytes % element_size), "__memcpy_aligned:  size not a multiple of sizeof(__m128i) - data is not aligned\n");
			static_assert(0 == (sizeof(T) % element_size), "__memcpy_aligned:  element not divisable by sizeof(__m128i)\n");
#endif

 // 128 bytes / iteration
			while (bytes >= (CACHE_LINE_BYTES << 1))
			{
				if constexpr (streaming) {
					_mm_prefetch((const CHAR*)(src + 4), _MM_HINT_NTA);
					_mm_stream_si128(dest, _mm_stream_load_si128(src));
					_mm_stream_si128(dest + 1ULL, _mm_stream_load_si128(src + 1ULL));
					_mm_stream_si128(dest + 2ULL, _mm_stream_load_si128(src + 2ULL));
					_mm_stream_si128(dest + 3ULL, _mm_stream_load_si128(src + 3ULL));
					_mm_prefetch((const CHAR*)(src + 4), _MM_HINT_NTA);
					_mm_stream_si128(dest + 4ULL, _mm_stream_load_si128(src + 4ULL));
					_mm_stream_si128(dest + 5ULL, _mm_stream_load_si128(src + 5ULL));
					_mm_stream_si128(dest + 6ULL, _mm_stream_load_si128(src + 6ULL));
					_mm_stream_si128(dest + 7ULL, _mm_stream_load_si128(src + 7ULL));
				}
				else {
					_mm_prefetch((const CHAR*)(src + 4), _MM_HINT_NTA);
					_mm_store_si128(dest, _mm_load_si128(src));
					_mm_store_si128(dest + 1ULL, _mm_load_si128(src + 1ULL));
					_mm_store_si128(dest + 2ULL, _mm_load_si128(src + 2ULL));
					_mm_store_si128(dest + 3ULL, _mm_load_si128(src + 3ULL));
					_mm_prefetch((const CHAR*)(src + 4), _MM_HINT_NTA);
					_mm_store_si128(dest + 4ULL, _mm_load_si128(src + 4ULL));
					_mm_store_si128(dest + 5ULL, _mm_load_si128(src + 5ULL));
					_mm_store_si128(dest + 6ULL, _mm_load_si128(src + 6ULL));
					_mm_store_si128(dest + 7ULL, _mm_load_si128(src + 7ULL));
				}
				dest += 8ULL; src += 8ULL;
				bytes -= (CACHE_LINE_BYTES << 1);
			}

 // 16 bytes / iteration
			while (bytes >= element_size)
			{
				if constexpr (streaming) {
					_mm_stream_si128(dest, _mm_stream_load_si128(src));
				}
				else {
					_mm_store_si128(dest, _mm_load_si128(src));
				}
				++dest; ++src;
				bytes -= element_size;
			}
		}
	}
}//end ns

INTRINSIC_MEMFUNC_IMPL __memcpy_aligned_32_stream(void* const __restrict dest, void const* const __restrict src, size_t const bytes)
{
	internal_mem::__memcpy_aligned<stream>((__m256i* const __restrict)dest, (__m256i* const __restrict)src, bytes);
}
INTRINSIC_MEMFUNC_IMPL __memcpy_aligned_32_store(void* const __restrict dest, void const* const __restrict src, size_t const bytes)
{
	internal_mem::__memcpy_aligned<store>((__m256i* const __restrict)dest, (__m256i* const __restrict)src, bytes);
}
INTRINSIC_MEMFUNC_IMPL __memcpy_aligned_16_stream(void* const __restrict dest, void const* const __restrict src, size_t const bytes)
{
	internal_mem::__memcpy_aligned<stream>((__m128i* const __restrict)dest, (__m128i* const __restrict)src, bytes);
}
INTRINSIC_MEMFUNC_IMPL __memcpy_aligned_16_store(void* const __restrict dest, void const* const __restrict src, size_t const bytes)
{
	internal_mem::__memcpy_aligned<store>((__m128i* const __restrict)dest, (__m128i* const __restrict)src, bytes);
}

static constexpr size_t const MINIMUM_PAGE_SIZE = 4096; // Windows Only, cannot be changed by user //

// large allocations //
INTRINSIC_MEM_ALLOC_FUNC __memalloc_large(size_t const bytes, size_t const alignment)
{
	if (bytes >= MINIMUM_PAGE_SIZE) { // guard so only large allocations actually use VirtualAlloc2

		size_t const pageBytes(SFM::roundToMultipleOf<true>((int64_t)bytes, (int64_t)MINIMUM_PAGE_SIZE));

		// alignment parameter is ignored
		// it will always be less than the page size
		// for VirtualAlloc2 alignment is used for greater than page size allocations
		// structures inside program are aligned on 4, 8, 16, 32 upto 64 byte (cache-line) boundaries
		// pages inside program are aligned on 4096, 8192, etc byte boundaries

		// so desired alignment is already guaranteed as alignment is a multiple of the pagesize and pagesize is greater than desired alignment
		void* const __restrict vmem = ::VirtualAlloc2(nullptr, nullptr, pageBytes,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE,
			nullptr, 0);

		if (nullptr != vmem) { 
			
			::VirtualLock(vmem, pageBytes); // physical memory only, no swappage crap!!!!

			return(vmem);
		}
#ifndef NDEBUG
		FMT_LOG_WARN(GAME_LOG, "VirtualAlloc2 or VirtualLock returned no memory, falling back to regular allocation.");
#endif
	}

	// still succeed with an allocation
	if (0 == alignment) {
		return(::scalable_malloc(bytes));
	}
	return(::scalable_aligned_malloc(bytes, alignment));
}

INTRINSIC_MEM_FREE_FUNC __memfree_large(void* const __restrict data, size_t const bytes, size_t const alignment)
{
	if (bytes >= MINIMUM_PAGE_SIZE) { // guard so only large allocations actually use VirtualAlloc2

		size_t const pageBytes(SFM::roundToMultipleOf<true>((int64_t)bytes, (int64_t)MINIMUM_PAGE_SIZE));

		::VirtualUnlock(data, pageBytes);
		::VirtualFree(data, 0, MEM_RELEASE);
	}
	else { // memory was not allocated with VirtualAlloc2

		// still succeed with free, no leaks!
		if (0 == alignment) {
			::scalable_free(data);
		}
		else {
			::scalable_aligned_free(data);
		}
	}
}

INTRINSIC_MEM_FREE_FUNC  __prefetch_vmem(void const* const __restrict address, size_t const bytes)
{
	WIN32_MEMORY_RANGE_ENTRY entry{ const_cast<PVOID>(address), bytes };

	PrefetchVirtualMemory(GetCurrentProcess(), 1, &entry, 0);
}




