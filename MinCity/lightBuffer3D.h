#pragma once
#include "writeOnlyBuffer.h"
#include "globals.h"
#include <Math/superfastmath.h>
#include <atomic>
#include "ComputeLightConstants.h"

#include <Imaging/Imaging/Imaging.h>
#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

typedef tbb::enumerable_thread_specific< XMFLOAT3A > Bounds; // per thread instance

// 3D Version ################################################################################################ //
template< typename XMFLOAT4A, uint32_t const LightWidth, uint32_t const LightHeight, uint32_t const LightDepth,
							  uint32_t const Width, uint32_t const Height, uint32_t const Depth>
struct alignas(CACHE_LINE_BYTES) lightBuffer3D : public writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth> // ** meant to be used with staging buffer that uses system side memory, not gpu memory
{
	constinit static inline XMVECTORU32 const _xmMaskBits{ 0x00, 0xFF, 0xFF, 0xFF };

	constinit static inline XMVECTORF32 const _xmInvWorldLimitMax{ 1.0f / float(Width),  1.0f / float(Height),  1.0f / float(Depth), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmLightLimitMax{ float(LightWidth),  float(LightHeight),  float(LightDepth), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmWorldLimitMax{ float(Width),  float(Depth),  float(Height), 0.0f }; // needs to be xzy
	
public:
	// access - readonly
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMin() const { return(uvec4_v(_min_extents)); } // returns the extent minimum, (xyz, light space)
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMax() const { return(uvec4_v(_max_extents)); } // returns the extent maximum, (xyz, light space)
	uvec4_v const __vectorcall		getVolumeExtentsLimit() const {	return(uvec4_v(LightWidth, LightHeight, LightDepth)); }

	// methods //
	__declspec(safebuffers) void __vectorcall clear() {
		// no need to clear main write-combined gpu staging buffer, it is fully replaced by _cache when comitted

		// reset bounds
		_maximum = std::move<Bounds&&>(Bounds{});
		_minimum = std::move<Bounds&&>(Bounds(_xmWorldLimitMax));

		tbb::parallel_invoke(
			[&] {
				// clear internal memory
				__memclr_stream<CACHE_LINE_BYTES>(_internal, LightWidth * LightHeight * LightDepth * sizeof(std::atomic_uint32_t));
			},
			[&] {
				// clear internal cache
				__memclr_stream<CACHE_LINE_BYTES>(_cache, LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A));
			});
	}

	__declspec(safebuffers) void __vectorcall commit(vku::GenericBuffer const& __restrict stagingBuffer, size_t const hw_concurrency) {

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
		uvec4_v const new_max( SFM::ceil_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMax))) );
		uvec4_v const new_min( SFM::floor_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMin))) );

		if (uvec4_v::all<3>(new_min < new_max)) { // *bugfix only update the publicly accessible extents if the new min/max is valid.
			new_max.xyzw(_max_extents);
			new_min.xyzw(_min_extents);
		}

		// copy internal cache to write-combined memory (staging buffer) using streaming stores
		// this fully replaces the volume for the stagingBuffer irregardless of current light bounds min & max (important) (this effectively reduces the need to clear the gpu - writecombined *staging* buffer)
		size_t const size(LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A));
		__memcpy_threaded<32>(stagingBuffer.map(), _cache, size, 
						      size / hw_concurrency); // faster copy with 32 - guarnteed that size is multiple of XMFLOAT4A (16) x2 (32) - due to the entire volume using a size that is always an even number. Otherwise this would be 1 off and the _16 would have to be used
		stagingBuffer.unmap();
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
			XMStoreFloat4A(_cache + index, XMVectorSetW(in, float(in_packed_color)));
		}
		else { // existing
			
#ifdef DEBUG_PERF_LIGHT_RELOADS
			uint32_t reload_count = 0;
#endif
			for(;;)
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
						XMStoreFloat4A(_cache + index, XMVectorSetW(xmPosition, float(new_packed_color)));
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
	__declspec(safebuffers) void __vectorcall seed(FXMVECTOR xmPosition, uint32_t const srgbColor) const	// 3D emplace
	{
		const_cast<lightBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth, Width, Height, Depth>* const __restrict>(this)->updateBounds(xmPosition); // ** non-swizzled - in xyz form

		// transform world space position [0.0f...512.0f] to light space position [0.0f...128.0f]
		uvec4_t uiIndex;
		SFM::round_to_u32(XMVectorMultiply(_xmLightLimitMax,XMVectorMultiply(_xmInvWorldLimitMax, xmPosition))).xyzw(uiIndex);

		// slices ordered by Z 
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		uint32_t const index((uiIndex.y * LightWidth * LightDepth) + (uiIndex.z * LightWidth) + uiIndex.x);

		// *must convert from srgb to linear


		// swizzle position to remap position to texture matched xzy rather than xyz
 		seed_single(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmPosition), index, ImagingSRGBtoLinear(srgbColor)); // faster, accurate lut srgb-->linear conersion
	}
	
	lightBuffer3D()
		: _internal(nullptr), _cache(nullptr), _maximum{}, _minimum(_xmWorldLimitMax)
	{
		writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth>::clear_tracking();

		_internal = (std::atomic_uint32_t * __restrict)scalable_aligned_malloc(LightWidth * LightHeight * LightDepth * sizeof(std::atomic_uint32_t), CACHE_LINE_BYTES);
		_cache = (XMFLOAT4A*)scalable_aligned_malloc(LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A), CACHE_LINE_BYTES);

		clear();
	}
	~lightBuffer3D()
	{
		// memory is now automagically freed on process exit scalable_aligned_free(internal);
		//_internal = nullptr;
		//_cache = nullptr;

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
	Bounds 								  _maximum,		// ** xzy form for all bounds / extents !!!
								          _minimum;

	uvec4_t								  _max_extents,
										  _min_extents;

	alignas(CACHE_LINE_BYTES) std::atomic_uint32_t* __restrict	_internal;
	alignas(CACHE_LINE_BYTES) XMFLOAT4A* __restrict				_cache;
};


