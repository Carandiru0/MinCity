#pragma once
#include "writeOnlyBuffer.h"
#include "globals.h"
#include <Math/superfastmath.h>
#include <atomic>
#include "ComputeLightConstants.h"

typedef tbb::enumerable_thread_specific< XMFLOAT3A > Bounds; // per thread instance

// 3D Version ################################################################################################ //
template< typename XMFLOAT4A, uint32_t const LightWidth, uint32_t const LightHeight, uint32_t const LightDepth,
							  uint32_t const Width, uint32_t const Height, uint32_t const Depth>
struct alignas(CACHE_LINE_BYTES) lightBuffer3D : public writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth> // ** meant to be used with staging buffer that uses system side memory, not gpu memory
{
	static inline XMVECTORU32 const _xmMaskBits{ 0x00, 0xFF, 0xFF, 0xFF };

	static inline XMVECTORF32 const _xmInvWorldLimitMax{ 1.0f / float(Width),  1.0f / float(Height),  1.0f / float(Depth), 0.0f }; // needs to be xyz
	static inline XMVECTORF32 const _xmLightLimitMax{ float(LightWidth),  float(LightHeight),  float(LightDepth), 0.0f }; // needs to be xyz
	static inline XMVECTORF32 const _xmWorldLimitMax{ float(Width),  float(Depth),  float(Height), 0.0f }; // needs to be xzy
	
public:
	// access - readonly
	uvec4_v const   getCurrentVolumeExtentsMin() const { return(uvec4_v(_min_extents)); } // returns the extent minimum, (xyz, light space)
	uvec4_v const   getCurrentVolumeExtentsMax() const { return(uvec4_v(_max_extents)); } // returns the extent maximum, (xyz, light space)

	// methods //
	__declspec(safebuffers) void __vectorcall clear_memory() {
		// no need to clear main write-combined gpu staging buffer, it is fully replaced by _cache when comitted

		// reset bounds
		_maximum = std::move<Bounds&&>(Bounds{});
		_minimum = std::move<Bounds&&>(Bounds(_xmWorldLimitMax));

		// clear internal memory
		__memclr_aligned_32<LightWidth * LightHeight * LightDepth, true>(_internal);

		// clear internal cache
		__memclr_aligned_stream(_cache, LightWidth * LightHeight * LightDepth);
	}

	__declspec(safebuffers) void __vectorcall commit(vku::GenericBuffer const& stagingBuffer) {

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
		uvec4_v(SFM::ceil_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMax)))).xyzw(_max_extents);
		uvec4_v(SFM::floor_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmMin)))).xyzw(_min_extents);

		// copy internal cache to write-combined memory (staging buffer) using streaming stores
		// this fully replaces the volume for the stagingBuffer irregardless of current light bounds min & max (important) (this effectively reduces the need to clear the gpu - writecombined *staging* buffer)
		__memcpy_aligned_32_stream(stagingBuffer.map(), _cache, LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A)); // faster copy with 32 - guarnteed that size is multiple of XMFLOAT4A (16) x2 (32) - due to the entire volume using a size that is always an even number. Otherwise this would be 1 off and the _16 would have to be used
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

			const_cast<lightBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth, Width, Height, Depth>* const __restrict>(this)->update_tracking(index);
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
	__declspec(safebuffers) void __vectorcall seed(FXMVECTOR xmPosition, uint32_t const resolvedColor) const	// 3D emplace
	{
		const_cast<lightBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth, Width, Height, Depth>* const __restrict>(this)->updateBounds(xmPosition); // ** non-swizzled - in xyz form

		// transform world space position [0.0f...512.0f] to light space position [0.0f...128.0f]
		// floor to ints
		ivec4_v const xmIndex(SFM::round_to_i32(XMVectorMultiply(_xmLightLimitMax,XMVectorMultiply(_xmInvWorldLimitMax, xmPosition))));
		ivec4_t iIndex;
		xmIndex.xyzw(iIndex);

		// slices ordered by Z 
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		uint32_t const index((iIndex.y * LightWidth * LightDepth) + (iIndex.z * LightWidth) + iIndex.x);

		// swizzle position to remap position to texture matched xzy rather than xyz
		seed_single(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmPosition), index, resolvedColor);
	}
	// other types::
	// todo
	
	lightBuffer3D()
		: _internal(nullptr), _maximum{}, _minimum(_xmWorldLimitMax)
	{
		writeonlyBuffer3D<XMFLOAT4A, LightWidth, LightHeight, LightDepth>::clear_tracking();

		_internal = (std::atomic_uint32_t* __restrict)__memalloc_large(LightWidth * LightHeight * LightDepth * sizeof(std::atomic_uint32_t), CACHE_LINE_BYTES);
		__memclr_aligned_32<LightWidth * LightHeight * LightDepth, true>(_internal);

		_cache = (XMFLOAT4A*)__memalloc_large(LightWidth * LightHeight * LightDepth * sizeof(XMFLOAT4A), CACHE_LINE_BYTES);
		__memclr_aligned_stream(_cache, LightWidth * LightHeight * LightDepth);
	}
	~lightBuffer3D()
	{
		// memory is now automagically freed on process exit scalable_aligned_free(internal);
		_internal = nullptr;
		_cache = nullptr;
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

	std::atomic_uint32_t* __restrict	  _internal;
	XMFLOAT4A* __restrict				  _cache;
};


