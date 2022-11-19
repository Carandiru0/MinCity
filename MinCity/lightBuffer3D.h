#pragma once
#include "writeOnlyBuffer.h"
#include "globals.h"
#include <Math/superfastmath.h>
#include <atomic>
#include "ComputeLightConstants.h"
#include "world.h"

#include <Imaging/Imaging/Imaging.h>
#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

typedef tbb::enumerable_thread_specific< XMFLOAT3A, tbb::cache_aligned_allocator<XMFLOAT3A>, tbb::ets_key_per_instance > Bounds; // per thread instance

INLINE_MEMFUNC __streaming_store_residual(XMFLOAT4A* const __restrict dest, XMFLOAT4A const* const __restrict src, uint32_t const index) // single usage (residual/remainder)
{
	_mm_stream_ps((float* const __restrict)(dest + index), _mm_load_ps((float const* const __restrict)(src + index)));
}
INLINE_MEMFUNC __streaming_store(XMFLOAT4A* const __restrict dest, XMFLOAT4A const* const __restrict src, uint32_t const (& __restrict index)[8]) // batches by size of 8, src should be recently cached values, dest is write-combined so the streaming stores are batched effectively here.
{
	XMVECTOR const
		r0(_mm_load_ps((float const* const __restrict)(src + index[0]))),	// random locations, but should be in recently used L1/L2 cache so fast to access (from the light _cache)
		r1(_mm_load_ps((float const* const __restrict)(src + index[1]))),
		r2(_mm_load_ps((float const* const __restrict)(src + index[2]))),
		r3(_mm_load_ps((float const* const __restrict)(src + index[3]))),
		r4(_mm_load_ps((float const* const __restrict)(src + index[4]))),
		r5(_mm_load_ps((float const* const __restrict)(src + index[5]))),
		r6(_mm_load_ps((float const* const __restrict)(src + index[6]))),
		r7(_mm_load_ps((float const* const __restrict)(src + index[7])));

	// streamed directly to memory don't care when it's finished (to write-combined staging buffer memory _staging)
	_mm_stream_ps((float* const __restrict)(dest + index[0]), r0);
	_mm_stream_ps((float* const __restrict)(dest + index[1]), r1);
	_mm_stream_ps((float* const __restrict)(dest + index[2]), r2);
	_mm_stream_ps((float* const __restrict)(dest + index[3]), r3);
	_mm_stream_ps((float* const __restrict)(dest + index[4]), r4);
	_mm_stream_ps((float* const __restrict)(dest + index[5]), r5);
	_mm_stream_ps((float* const __restrict)(dest + index[6]), r6);
	_mm_stream_ps((float* const __restrict)(dest + index[7]), r7);
}

template<typename T, uint32_t const Size>
struct sBatchedByIndex
{
private:
	uint32_t				indices[Size];
	uint32_t				Count;

	__SAFE_BUF __inline void __vectorcall out_batched(T* const __restrict out_, T const* const __restrict in_)
	{
		__streaming_store(out_, in_, indices); // must be capable of Size output
		Count = 0;
	}
	
public:
	__SAFE_BUF __inline void __vectorcall out(T* const __restrict out_, T const* const __restrict in_) // use this at the end of the process - done in commit()
	{
		if (Size == Count) {
			out_batched(out_, in_); // batched output
		}
		else {
			for (uint32_t i = 0; i < Count; ++i) { // in case there are less than Size elements left, output individually.
				__streaming_store_residual(out_, in_, indices[i]);
			}
			Count = 0;
		}
	}

	template<class... Args>
	__SAFE_BUF __inline void __vectorcall emplace_back(T* const __restrict out_, T const* const __restrict in_, uint32_t const index) { // normal usage at any time after clear() and before commit()

		indices[Count] = index;

		if (++Count >= Size) {
			out_batched(out_, in_); // batched output
		}
	}
};

template<typename T>
struct references
{
public:
	tbb::concurrent_vector<T*> const& reference() const { return(_reference); }

	void reference(T*&& reference)
	{
		_reference.emplace_back(std::forward<T*&&>(reference));
		reference->referenced(true);
	}

	references() = default;

private:
	tbb::concurrent_vector<T*>	_reference;
};

template<typename T, uint32_t const Size>
struct sBatchedReferenced : public sBatchedByIndex<T, Size>
{
	bool const referenced() const { return(_referenced); }
	void	   referenced(bool const value) { _referenced = value; }

private:
	bool _referenced;
};

using lightBatch = sBatchedReferenced<XMFLOAT4A, 8>;

namespace local
{
	extern __declspec(selectany) inline constinit thread_local lightBatch lights;
}


// 3D Version ################################################################################################ //
template< typename XMFLOAT4A, uint32_t const LightWidth, uint32_t const LightHeight, uint32_t const LightDepth,  // non-uniform size ok for light volume
							  uint32_t const Size>				 // uniform size ok for world visible volume
struct lightBuffer3D : public writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth> // ** meant to be used with staging buffer that uses system side memory, not gpu memory
{
	constinit static inline XMVECTORU32 const _xmMaskBits{ 0x00, 0xFF, 0xFF, 0xFF };

	constinit static inline XMVECTORF32 const _xmInvWorldLimitMax{ 1.0f / float(Size),  1.0f / float(Size),  1.0f / float(Size), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmLightLimitMax{ float(LightWidth),  float(LightHeight),  float(LightDepth), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmWorldLimitMax{ float(Size),  float(Size),  float(Size), 0.0f }; // needs to be xzy

public:
	// access - readonly
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMin() const { return(uvec4_v(_min_extents)); } // returns the extent minimum, (xyz, light space)
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMax() const { return(uvec4_v(_max_extents)); } // returns the extent maximum, (xyz, light space)
	uvec4_v const __vectorcall		getVolumeExtentsLimit() const { return(uvec4_v(LightWidth, LightHeight, LightDepth)); }

	// methods //
	__declspec(safebuffers) void __vectorcall clear(uint32_t const resource_index) {
		// no need to clear main write-combined gpu staging buffer, it is fully replaced by _cache when comitted
		_active_resource_index = resource_index;

		// reset bounds
		_maximum = std::move<Bounds&&>(Bounds{});
		_minimum = std::move<Bounds&&>(Bounds(_xmWorldLimitMax));

		// clear internal memory
		___memset_threaded<CACHE_LINE_BYTES>(_internal, 0, LightWidth * LightHeight * LightDepth * sizeof(std::atomic_uint32_t), _block_size_atomic); // 700KB / thread (12 cores)
		// clear internal cache
		___memset_threaded<CACHE_LINE_BYTES>(_cache, 0, LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A), _block_size_cache); // 2.7MB / thread (12 cores)
	}

	__declspec(safebuffers) void __vectorcall map() {
		_staging[_active_resource_index] = (XMFLOAT4A* const __restrict)_stagingBuffer[_active_resource_index].map(); // buffer is only HOST VISIBLE, requires flush when writing memory is complete.
		// clear staging buffer (right before usage)
		___memset_threaded<16>(_staging[_active_resource_index], 0, LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A), _block_size_cache); // 2.7MB / thread (12 cores)
	}

	__declspec(safebuffers) void __vectorcall commit() {

		XMVECTOR xmMax(XMVectorZero());
		{ // maximum

			// update resolved max/min from thread local instances of max/min
			for (Bounds::const_iterator
				i = _maximum.begin(); i != _maximum.end(); ++i) {

				xmMax = XMVectorMax(xmMax, XMLoadFloat3A(&(*i)));
			}
		}

		XMVECTOR xmMin(_xmWorldLimitMax);
		{ // minimum

			// update resolved max/min from thread local instances of max/min
			for (Bounds::const_iterator
				i = _minimum.begin(); i != _minimum.end(); ++i) {

				xmMin = XMVectorMin(xmMin, XMLoadFloat3A(&(*i)));
			}
		}

		// convert from world space to light space
		//            &
		// storing current minimum & maximum
		uvec4_v const new_max(SFM::ceil_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMax))));  // also transforming from world-space coordinates to light volume space.
		uvec4_v const new_min(SFM::floor_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMin))));

		if (uvec4_v::all<3>(new_min < new_max)) { // *bugfix only update the publicly accessible extents if the new min/max is valid.
			new_max.xyzw(_max_extents);
			new_min.xyzw(_min_extents);
		}

		// ensure all residual lights have been output //
		for ( auto iter = _lights.reference().cbegin() ; iter != _lights.reference().cend() ; ++iter ) {
			
			auto& light(*iter);
			light->out(_staging[_active_resource_index], _cache);
		}
		___streaming_store_fence();
		_stagingBuffer[_active_resource_index].unmap(); // buffer is only HOST VISIBLE, requires flush when writing memory is complete.
		_stagingBuffer[_active_resource_index].flush();
	}

#ifdef DEBUG_PERF_LIGHT_RELOADS
	static inline tbb::atomic<uint32_t>
		num_lights = 0,
		reloads = 0,
		iteration_reload_max = 0,
		reloads_counting = 0;
#endif

	// continous average!
	//float addToAverage(float average, float size, float value)
	//{
	//	return (size * average + value) / (size + 1);
	//}
	// access - writeonly
	// does not require mutex - data is inherntly ok out of order accumulation so it doesn't matter withe the accumulating average
	// also the size is tracked atomically which makes this work flawlessly lock-free !!! proven with vtune. the streaming stores are also an atomic operation inherently.

	// there is no bounds checking, values are expected to be within bounds
private:
	__declspec(safebuffers) __inline void __vectorcall updateBounds(FXMVECTOR position)
	{
		// max/min
		Bounds::reference
			localMax(_maximum.local()),
			localMin(_minimum.local());

		XMStoreFloat3A(&localMax, XMVectorMax(position, XMLoadFloat3A(&localMax)));
		XMStoreFloat3A(&localMin, XMVectorMin(position, XMLoadFloat3A(&localMin)));
	}

	__declspec(safebuffers) void __vectorcall seed_single(FXMVECTOR in, uint32_t const index, uint32_t const in_packed_color) const	// 3D emplace
	{
		// slices ordered by Z
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		std::atomic_uint32_t* const __restrict area(_internal + index);

		uint32_t size(area->fetch_add(1, std::memory_order_relaxed));
		if (0 == size) {	// first instance

			// write-only position and packed color to gpu staging buffer
			// stores are used instead of streaming, could potentially bbe a new load very soon
			XMVECTOR const xmLight(XMVectorSetW(in, float(in_packed_color)));
			XMStoreFloat4A(_cache + index, xmLight);
			local::lights.emplace_back(_staging[_active_resource_index], _cache, index);
		}
		else { // existing

#ifdef DEBUG_PERF_LIGHT_RELOADS
			uint32_t reload_count = 0;
#endif
			for (;;)
			{
				XMVECTOR const xmSize(_mm_cvtepi32_ps(_mm_set1_epi32(size)));
				XMVECTOR const xmInvSizePlusOne(SFM::rcp(_mm_cvtepi32_ps(_mm_set1_epi32(size + 1))));

				XMVECTOR xmColor = _mm_cvtepi32_ps(_mm_and_si128(_mm_setr_epi32(/*(in_packed_color >> 24)*/0, (in_packed_color >> 16), (in_packed_color >> 8), in_packed_color), _xmMaskBits));

				XMVECTOR xmPosition;
				{
					XMVECTOR const xmCombined = XMLoadFloat4A(_cache + index); // ok if staging cpu buffer ONLY

					{
						uint32_t const exist_packed_color = uint32_t(XMVectorGetW(xmCombined));

						//  (size * average + value) / (size + 1);
						xmColor = XMVectorMultiply(SFM::__fma(_mm_cvtepi32_ps(_mm_and_si128(_mm_setr_epi32(/*(exist_packed_color >> 24)*/0, (exist_packed_color >> 16), (exist_packed_color >> 8), exist_packed_color), _xmMaskBits)),
							xmSize, xmColor), xmInvSizePlusOne);
					}

					//  (size * average + value) / (size + 1);
					xmPosition = XMVectorMultiply(SFM::__fma(xmCombined, xmSize, in), xmInvSizePlusOne);
				}

				// pack color
				uvec4_t color;
				SFM::saturate_to_u8(xmColor, color);
				// A //         // B //            // G //        // R //
				uint32_t const new_packed_color(/*(color.x << 24) |*/ (color.y << 16) | (color.z << 8) | color.w);

				{
					if (area->load(std::memory_order_relaxed) != size) { // invalid
						_mm_pause(); // tight loop pause
						size = area->load(std::memory_order_relaxed); // acquire latest value after pause
#ifdef DEBUG_PERF_LIGHT_RELOADS
						++reload_count;
#endif				
					}
					else { // done
						// write-only position and packed color to gpu staging buffer
						XMVECTOR const xmLight(XMVectorSetW(xmPosition, float(new_packed_color)));
						XMStoreFloat4A(_cache + index, xmLight);
						local::lights.emplace_back(_staging[_active_resource_index], _cache, index);
						break;
					}
				}

			}

#ifdef DEBUG_PERF_LIGHT_RELOADS
			iteration_reload_max.store<tbb::relaxed>(std::max(reload_count, iteration_reload_max.load<tbb::relaxed>()));
			reloads_counting.fetch_and_add<tbb::release>(reload_count);
			reloads.fetch_and_add<tbb::release>(reload_count);
#endif
		}
#ifdef DEBUG_PERF_LIGHT_RELOADS
		num_lights.fetch_and_add<tbb::release>(1);
#endif

	}

	// there is no bounds checking, values are expected to be within bounds esxcept handling of -1 +1 for seededing X & Z Axis for seeding puposes.
public:
	__declspec(safebuffers) void __vectorcall seed(XMVECTOR xmPosition, uint32_t const srgbColor) const	// 3D emplace
	{
		const_cast<lightBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth, Size>* const __restrict>(this)->updateBounds(xmPosition); // ** non-swizzled - in xyz form

		// thread_local init (only happens once/thread)
		[[unlikely]] if (!local::lights.referenced()) {
			const_cast<lightBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth, Size>* const __restrict>(this)->_lights.reference(&local::lights);
		}
		
		// transform world space position [0.0f...512.0f] to light space position [0.0f...128.0f]
		uvec4_t uiIndex;
		SFM::floor_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmPosition))).xyzw(uiIndex);   

		// slices ordered by Z 
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		uint32_t const index((uiIndex.y * LightWidth * LightDepth) + (uiIndex.z * LightWidth) + uiIndex.x);

		// *must convert from srgb to linear

		// swizzle position to remap position to texture matched xzy rather than xyz
		seed_single(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmPosition), index, ImagingSRGBtoLinear(srgbColor)); // faster, accurate lut srgb-->linear conersion
	}

	void create(size_t const hardware_concurrency)
	{
		writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth>::clear_tracking();

		uint32_t size(0);

		size = LightWidth * LightHeight * LightDepth * sizeof(std::atomic_uint32_t);
		_internal = (std::atomic_uint32_t * __restrict)scalable_aligned_malloc(size, CACHE_LINE_BYTES); // *bugfix - over-aligned for best performance and to prevent false sharing
		_block_size_atomic = size_t(size) / hardware_concurrency;

		size = LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A);
		_cache = (XMFLOAT4A * __restrict)scalable_aligned_malloc(size, CACHE_LINE_BYTES); // *bugfix - over-aligned for best performance (wc copy) and to prevent false sharing
		_block_size_cache = size_t(size) / hardware_concurrency;

		clear(0);
	}

	lightBuffer3D(vku::double_buffer<vku::GenericBuffer> const& stagingBuffer_)
		: _stagingBuffer(stagingBuffer_), _internal(nullptr), _cache(nullptr), _active_resource_index(0), _maximum{}, _minimum(_xmWorldLimitMax), _block_size_atomic(0), _block_size_cache(0)
	{}
	~lightBuffer3D()
	{
		if (_internal) {
			scalable_aligned_free(_internal); _internal = nullptr;
		}
		if (_cache) {
			scalable_aligned_free(_cache); _cache = nullptr;
		}
	}

private:
	lightBuffer3D(lightBuffer3D const&) = delete;
	lightBuffer3D(lightBuffer3D&&) = delete;
	void operator=(lightBuffer3D const&) = delete;
	void operator=(lightBuffer3D&&) = delete;
private:
	std::atomic_uint32_t* __restrict								_internal;
	XMFLOAT4A* __restrict											_cache;

	using lightBatchReferences = references<lightBatch>;

	lightBatchReferences									 _lights{};
	vku::double_buffer<vku::GenericBuffer> const& __restrict _stagingBuffer;
	XMFLOAT4A* __restrict									 _staging[2];

	uint32_t							  _active_resource_index;
	uint32_t				              _block_size_atomic,
										  _block_size_cache;
	
	Bounds 								  _maximum,		// ** xzy form for all bounds / extents !!!
									      _minimum;

	uvec4_t								  _max_extents,
										  _min_extents;
};


