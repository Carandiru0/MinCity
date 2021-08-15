#pragma once
#include <stdint.h>
#include <tbb/tbb.h>
#include <Random/superrandom.hpp>

#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)
// #define these options to 1 befote include of this header to enable

#ifndef OPTION_TRACKING_SLICE_MAX
#define OPTION_TRACKING_SLICE_MAX 0
#endif

#ifndef OPTION_TRACKING_HASH
#define OPTION_TRACKING_HASH 0
#endif

			
// 3D Version ################################################################################################ //
template< typename T, uint32_t const X, uint32_t const Y, uint32_t const Z >
struct alignas(CACHE_LINE_BYTES) writeonlyBuffer3D {

protected:
	static constexpr size_t const ATOMIC_SPREAD = 24ULL;
			// not too many, but enough to spread chance of contention on individual atomic var	
public:
	// access - readonly
#if (0 != OPTION_TRACKING_SLICE_MAX)
	uint32_t const tracked_slice_maximum() const {

		uint32_t maxSlice(0);

		for (uint32_t i = 0; i < ATOMIC_SPREAD; ++i) {
			maxSlice = SFM::max(maxSlice, (uint32_t const)_trackedSliceMax[i]);
		}
		return(maxSlice);
}
#endif
#if (0 != OPTION_TRACKING_HASH)
	uint64_t const __vectorcall tracked_hash() const {

		uint64_t hash(0);

		for (uint32_t i = 0; i < ATOMIC_SPREAD; ++i) {
			hash += _trackedHash[i];
		}
		return(hash);
	}
#endif

	STATIC_INLINE_PURE constexpr size_t const num_elements() { return(X*Y*Z); }
	STATIC_INLINE_PURE constexpr size_t const element_size() { return(sizeof(T)); }
	STATIC_INLINE_PURE constexpr size_t const size() { return(num_elements() * element_size()); }

	void clear_tracking() {
#if (0 != OPTION_TRACKING_SLICE_MAX) | (0 != OPTION_TRACKING_HASH)
		for (uint32_t i = 0; i < ATOMIC_SPREAD; ++i)
#endif
		{
#if (0 != OPTION_TRACKING_SLICE_MAX)
			_trackedSliceMax[i] = 0;
#endif
#if (0 != OPTION_TRACKING_HASH)
			_trackedHash[i] = 0;
#endif
		}
	}

protected:
#if (0 != OPTION_TRACKING_SLICE_MAX)
	__inline void __vectorcall update_tracking(uint32_t const x, uint32_t const y, uint32_t const z)
	{
		// Slice "window/range" tracking
		uint32_t const unique = PsuedoRandomNumber(0, ATOMIC_SPREAD - 1);

		_trackedSliceMax[unique] = SFM::max((uint32_t const)_trackedSliceMax, y);
	}
#endif
#if (0 != OPTION_TRACKING_HASH)
	__inline void __vectorcall update_tracking(uint32_t const index)
	{
		// Slice "window/range" tracking
		uint32_t const unique = PsuedoRandomNumber(0, ATOMIC_SPREAD - 1);

		_trackedHash[unique].fetch_and_add<tbb::release>(index);
	}
#endif

public:
	writeonlyBuffer3D()
	{
		clear_tracking();
	}
	~writeonlyBuffer3D() = default;
private:
	writeonlyBuffer3D(writeonlyBuffer3D const&) = delete;
	writeonlyBuffer3D(writeonlyBuffer3D&&) = delete;
	void operator=(writeonlyBuffer3D const&) = delete;
	void operator=(writeonlyBuffer3D&&) = delete;

protected:
#if (0 != OPTION_TRACKING_SLICE_MAX)
	tbb::atomic<uint32_t>			 _trackedSliceMax[ATOMIC_SPREAD];
#endif
#if (0 != OPTION_TRACKING_HASH)
	tbb::atomic<uint64_t>			 _trackedHash[ATOMIC_SPREAD];
#endif
};




