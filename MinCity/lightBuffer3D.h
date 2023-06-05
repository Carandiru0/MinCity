#pragma once
#include "streaming_sizes.h"

template<typename T>
INLINE_MEMFUNC __streaming_store_residual(T* const __restrict dest, T const* const __restrict src, uint32_t const index) // single usage (residual/remainder)
{
	//*(dest + index) = *(src + index);
	_mm_stream_si64x((__int64* const __restrict)(dest + index), (__int64 const)*(src + index));
}
template<typename T, size_t const Size>
INLINE_MEMFUNC __streaming_store(T* const __restrict dest, T const* const __restrict src, uint32_t const (& __restrict index)[Size]) // batches by size of 8, src should be recently cached values, dest is write-combined so the streaming stores are batched effectively here.
{
#pragma loop( ivdep )
	for (uint32_t i = 0; i < Size; ++i) {

		//*(dest + index[i]) = *(src + index[i]);
		uint32_t const offset(index[i]);
		_mm_stream_si64x((__int64* const __restrict)(dest + offset), (__int64 const)*(src + offset));
	}
}

#include "sBatchedByIndex.h"
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
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedXor64)
#pragma intrinsic(_InterlockedOr64)

using lightBatch = sBatchedByIndexReferenced<uint64_t, eStreamingBatchSize::LIGHTS>;

namespace local
{
#ifdef __clang__
	extern __declspec(selectany) inline thread_local lightBatch lights{};
#else
	extern __declspec(selectany) inline thread_local lightBatch lights;
#endif
}

typedef tbb::enumerable_thread_specific< XMFLOAT3A, tbb::cache_aligned_allocator<XMFLOAT3A>, tbb::ets_key_per_instance > Bounds; // per thread instance

// 3D Version ################################################################################################ //
template< typename PackedLight, uint32_t const LightSize,                                        // uniform size for light volume
							    uint32_t const Size>			                                 // uniform size for world visible volume
struct lightBuffer3D : public writeonlyBuffer3D<PackedLight, LightSize, LightSize, LightSize> // ** meant to be used with staging buffer that uses system side memory, not gpu memory
{
	constexpr static uint32_t const DATA_MAX = 1023; // 10 bit per component 0x3FF = 1023
	constexpr static float const FDATA_MAX = (float)DATA_MAX;
	constinit static inline XMVECTORU32 const _xmMaskBits{ DATA_MAX, DATA_MAX, DATA_MAX, 0 };

	constinit static inline XMVECTORF32 const _xmInvWorldLimitMax{ 1.0f / float(Size),  1.0f / float(Size),  1.0f / float(Size), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmLightLimitMax{ float(LightSize),  float(LightSize),  float(LightSize), 0.0f }; // needs to be xyz
	constinit static inline XMVECTORF32 const _xmWorldLimitMax{ float(Size),  float(Size),  float(Size), 0.0f }; // needs to be xzy

public:
	// access - readonly
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMin() const { return(uvec4_v(_min_extents)); } // returns the extent minimum, (xyz, light space)
	uvec4_v const __vectorcall		getCurrentVolumeExtentsMax() const { return(uvec4_v(_max_extents)); } // returns the extent maximum, (xyz, light space)
	uvec4_v const __vectorcall		getVolumeExtentsLimit() const { return(uvec4_v(LightSize, LightSize, LightSize)); }

	// methods //
	__declspec(safebuffers) void __vectorcall clear(uint32_t const resource_index) {
		_active_resource_index = resource_index;

		// reset bounds
		_maximum = std::move<Bounds&&>(Bounds{});
		_minimum = std::move<Bounds&&>(Bounds(_xmWorldLimitMax));

		// clear internal cache
		___memset_threaded<CACHE_LINE_BYTES>(_cache, 0, LightSize * LightSize * LightSize * sizeof(PackedLight), _block_size_cache); // 2.7MB / thread (12 cores)
		// clear staging buffer (right before usage)
		if (_staging[resource_index]) {
			___memset_threaded_stream<32>(_staging[resource_index], 0, LightSize * LightSize * LightSize * sizeof(PackedLight), _block_size_cache); // 2.7MB / thread (12 cores)
		}
	}

	__declspec(safebuffers) void __vectorcall map() {
		_staging[_active_resource_index] = (PackedLight* const __restrict)_stagingBuffer[_active_resource_index].map(); // buffer is only HOST VISIBLE, requires flush when writing memory is complete.
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
			light->out((uint64_t* const __restrict)_staging[_active_resource_index], (uint64_t const* const __restrict)_cache);
		}
		
		_stagingBuffer[_active_resource_index].unmap();
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


	// -----------------------------------------------------------------
	// [light emitter 10bpc relative position]  +  [hdr 10bpc rgb color] 
	// -----------------------------------------------------------------
	//
	// 16bpc
	//             warp.x             warp.y             warp.z             warp.w
	// 0x1111111111111111 0x1111111111111111 0x1111111111111111 0x1111111111111111
	//   0xxxxxxxxxxyyyyy   0yyyyyzzzzzzzzzz   0rrrrrrrrrrggggg   0gggggbbbbbbbbbb
	//
	// (highest bit always unused for each component)
	//
	// packed component:      bit count:      packed component mask to value:
	// x : 10 bits, 0 - 1023       10,                  x : (0x7fe0 & warp.x) >> 5
	// y : "  ""   "   ""          20,                  y : ((0x1f & warp.x) << 5) | ((0x7c00 & warp.y) >> 10)
	// z : "  ""   "   ""          30,                  z : (0x3ff & warp.y)
	// r : 10 bits, 0 - 1023       40,                  r : (0x7fe0 & warp.z) >> 5
	// g : "  ""   "   ""          50,                  g : ((0x1f & warp.z) << 5) | ((0x7c00 & warp.w) >> 10)
	// b : "  ""   "   ""          60,                  b : (0x3ff & warp.w)
	// 0 : 4 bits, unused          64 bits total        0 : each high bit in each component is unused and set to 0
	//
	// --------------------------------------------------------------------
	using seed_data = union {

		struct {
			// bit order is low to high 
			uint16_t
				b : 10,
				g_low : 5,
				unused_warp_w : 1,

				g_high : 5,
				r : 10,
				unused_warp_z : 1,

				z : 10,
				y_low : 5,
				unused_warp_y : 1,

				y_high : 5,
				x : 10,
				unused_warp_x : 1;
		};

		uint64_t seed;

	};

	STATIC_INLINE_PURE uint64_t const __vectorcall pack_seed(uvec4_v const xyz, uvec4_v const rgb) // relative position & hdr 10bpc rgb color in
	{
		seed_data repacked{};

		{ // 10bpc relative position
			uvec4_t position;
			xyz.xyzw(position);

			repacked.x = position.x;
			repacked.y_high = (position.y & 0x3e0u) >> 5u;
			repacked.y_low = position.y & 0x1fu;
			repacked.z = position.z;
		}

		{ // 10bpc hdr color
			uvec4_t color;
			rgb.rgba(color);

			repacked.r = color.r;
			repacked.g_high = (color.g & 0x3e0u) >> 5u;
			repacked.g_low = color.g & 0x1fu;
			repacked.b = color.b;
		}

		return(repacked.seed);
	}

	STATIC_INLINE_PURE void __vectorcall unpack_seed(uvec4_v& __restrict xyz, uvec4_v& __restrict rgb, uint64_t const seed) // relative position & hdr 10bpc rgb color out
	{
		seed_data repacked{};
		repacked.seed = seed;

		{ // 10bpc relative position
			xyz = uvec4_v(repacked.x, (repacked.y_high << 5u) | repacked.y_low, repacked.z);
		}

		{ // 10bpc hdr color
			rgb = uvec4_v(repacked.r, (repacked.g_high << 5u) | repacked.g_low, repacked.b);
		}
	}

	__declspec(safebuffers) void __vectorcall seed_single(FXMVECTOR in, uint32_t const index, uint32_t const in_packed_color) const	// 3D emplace
	{
		// slices ordered by Z
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		// attenuate emitter linear hdr color by fractional distance to this seeds grid origin
		//XMVECTOR const xmNorm(SFM::fract(in));
		//XMVECTOR const xmDSquared(XMVectorAdd(XMVector3Dot(xmNorm,xmNorm), XMVectorReplicate(1.0f)));

		//XMVECTOR const xmColor = XMVectorDivide(_mm_cvtepi32_ps(_mm_and_si128(_mm_set_epi32(0, (in_packed_color >> 20), (in_packed_color >> 10), in_packed_color), _xmMaskBits)),
		//	                                    xmDSquared);
        
		// pack position + color
		uvec4_t unpacked_rgb;
		SFM::unpack_rgb_hdr(in_packed_color, unpacked_rgb);
		uvec4_v const rgb(unpacked_rgb);

		// pre-transform / scale "in" (location) to increase precision - must rescale final decoded / unpacked value back to WorldLimits in shader
		XMVECTOR const xyz(XMVectorScale(XMVectorMultiply(in, _xmInvWorldLimitMax), FDATA_MAX));

		uint64_t original_seed = pack_seed(SFM::floor_to_u32(xyz), rgb);

		// concurrency safe version of: *(_cache + index) = seed;
		
		__int64 prev_seed = _InterlockedExchange64((__int64 volatile* const)(_cache + index), (__int64 const)original_seed); // new light ?
		while (0 != prev_seed) { // existing light
			uvec4_v position, color;
			unpack_seed(position, color, (uint64_t)prev_seed);

			// blend
			position = uvec4_v(SFM::floor(SFM::lerp(position.v4f(), xyz, 0.5f)));
			color = uvec4_v(SFM::floor(SFM::lerp(color.v4f(), rgb.v4f(), 0.5f)));

			// update
			uint64_t const new_seed = pack_seed(position, color);
			prev_seed = _InterlockedExchange64((__int64 volatile* const)(_cache + index), (__int64 const)new_seed);
			if (prev_seed == original_seed)
				break; // done, 2 atomic operations are paired properly as one atomic operation

			// prev seed has been updated, redo from latest atomic value
			original_seed = new_seed;
		}

		local::lights.emplace_back((uint64_t* const __restrict)_staging[_active_resource_index], (uint64_t const* const __restrict)_cache, index);
	}

	// there is no bounds checking, values are expected to be within bounds esxcept handling of -1 +1 for seededing X & Z Axis for seeding puposes.
public:
	__declspec(safebuffers) void __vectorcall seed(FXMVECTOR xmPosition, uint32_t const srgbColor) const	// 3D emplace
	{
		[[likely]] if (0 != srgbColor) { // !dummy light

			static constexpr uint32_t const light_level_minimum(32 + 32 + 32); // minimums per component summed

			uvec4_t bright;
			SFM::unpack_rgba(srgbColor, bright);

			if ((bright.r + bright.g + bright.b) < light_level_minimum) // reject low-light level lights with a summed component level that is too dark to be emissive and is essentially black 
				return; // reject
		}

		// allow bounds to be updated
		const_cast<lightBuffer3D<PackedLight, LightSize, Size>* const __restrict>(this)->updateBounds(xmPosition); // ** non-swizzled - in xyz form

		// dummy light ?
		[[unlikely]] if (0 == srgbColor)
			return; // the volume bounds are still updated

		// thread_local init (only happens once/thread)
		[[unlikely]] if (!local::lights.referenced()) {
			const_cast<lightBuffer3D<PackedLight, LightSize, Size>* const __restrict>(this)->_lights.reference(&local::lights);
		}

		// transform world space position [0.0f...512.0f] to light space position [0.0f...128.0f]
		uvec4_t uiIndex;
		SFM::floor_to_u32(XMVectorMultiply(_xmLightLimitMax, XMVectorMultiply(_xmInvWorldLimitMax, xmPosition))).xyzw(uiIndex);

		// slices ordered by Z 
		// (z * xMax * yMax) + (y * xMax) + x;

		// slices ordered by Y: <---- USING Y
		// (y * xMax * zMax) + (z * xMax) + x;

		// also must convert from srgb to linear

		// swizzle position to remap position to texture matched xzy rather than xyz
		seed_single(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmPosition), (uiIndex.y * LightSize * LightSize) + (uiIndex.z * LightSize) + uiIndex.x, ImagingSRGBtoLinear(srgbColor)); // faster, accurate lut srgb-->linear conersion
	}

	void create(size_t const hardware_concurrency)
	{
		size_t size(0);

		size = LightSize * LightSize * LightSize * sizeof(PackedLight);
		_cache = (PackedLight * __restrict)scalable_aligned_malloc(size, CACHE_LINE_BYTES); // *bugfix - over-aligned for best performance (wc copy) and to prevent false sharing
		_block_size_cache = (uint32_t)(size / hardware_concurrency);

		_lights.reserve(hardware_concurrency); // one thread local reference / thread

		clear(1); clear(0); // *bugfix - setup first active resource index to be ready
	}

	lightBuffer3D(vku::double_buffer<vku::GenericBuffer> const& stagingBuffer_)
		: _stagingBuffer(stagingBuffer_), _cache(nullptr), _active_resource_index(0), _maximum{}, _minimum(_xmWorldLimitMax), _block_size_cache(0), 
		_max_extents{ 1, 1, 1, 0 }, _min_extents{} // *bugfix - initialize initial extents to min volume (prevents an intermittent copy attempt of zero volume on startup)
	{}
	~lightBuffer3D()
	{
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
	PackedLight* __restrict											_cache;
	PackedLight* __restrict									        _staging[2];
	vku::double_buffer<vku::GenericBuffer> const& __restrict        _stagingBuffer;

	using lightBatchReferences = references<lightBatch>;
	lightBatchReferences									 _lights{};

	Bounds 								  _maximum,		// ** xzy form for all bounds / extents !!!
		                                  _minimum;

	uvec4_t								  _max_extents,
		                                  _min_extents;

	uint32_t							  _active_resource_index;
	uint32_t				              _block_size_cache;
};


