#pragma once

#include <Math/superfastmath.h>
#include <Utility/mem.h>
#include <Utility/class_helper.h>
#include "streaming_sizes.h"

#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

#pragma warning( disable : 4166 ) // __vectorcall ctor

namespace VertexDecl
{
	// all vertex declarations are explicitly aligned to 16 bytes per component
	// only use vec4 compatible data (XMVECTOR)
	// ok to use smaller type (eg uint) if last member of declaration, will still be aligned on 16byte boundary
	struct no_vtable alignas(16) just_position {

		XMVECTOR	position;

	};

	struct no_vtable alignas(32) VoxelNormal {

	public:
		XMVECTOR	worldPos;						//xyz = world position , w = hash
		XMVECTOR	uv_color;

		__forceinline explicit __vectorcall VoxelNormal(FXMVECTOR worldPos_, FXMVECTOR uv_color_, uint32_t const hash) noexcept
			: worldPos(XMVectorSetW(worldPos_, SFM::uintBitsToFloat(hash))), uv_color(uv_color_)
		{}
		constexpr VoxelNormal() = default;
		__forceinline __vectorcall VoxelNormal(VoxelNormal&& relegate) noexcept = default;
		__forceinline void __vectorcall operator=(VoxelNormal&& relegate) noexcept {
			worldPos = std::move(relegate.worldPos);
			uv_color = std::move(relegate.uv_color);
		}
		
	private:
		VoxelNormal(VoxelNormal const&) = delete;
		VoxelNormal& operator=(VoxelNormal const&) = delete;
	};
	struct no_vtable alignas(32) VoxelDynamic : public VoxelNormal {

		// same size as normal //

		__forceinline explicit __vectorcall VoxelDynamic(FXMVECTOR worldPos_, FXMVECTOR orient_color_, uint32_t const hash) noexcept
			: VoxelNormal(worldPos_, orient_color_, hash)
		{}
		constexpr VoxelDynamic() = default;
		__forceinline __vectorcall VoxelDynamic(VoxelDynamic&& relegate) noexcept = default;
		__forceinline void __vectorcall operator=(VoxelDynamic&& relegate) noexcept {

			worldPos = std::move(relegate.worldPos);
			uv_color = std::move(relegate.uv_color);
		}
		
	private:
		VoxelDynamic(VoxelDynamic const&) = delete;
		VoxelDynamic& operator=(VoxelDynamic const&) = delete;
	};

	struct no_vtable alignas(16) nk_vertex {
		XMFLOAT4A position_uv;
		uint32_t color;
	};

} // end ns VertexDec

namespace BufferDecl
{
	struct no_vtable VoxelSharedBuffer {
		XMVECTOR	average_reflection_color;
		uint32_t    average_reflection_count;
		uint32_t	new_image_layer_count_max;
	};
}

namespace UniformDecl
{
	// vbs >
	
	// BUFFER alignment should not be explicity specified on struct, rather use alignment rules of Vulkan spec and do ordering of struct members manually

	struct no_vtable VoxelSharedUniform {
		XMMATRIX	proj;
		XMMATRIX	view;
		XMVECTOR	eyePos;         // .w = camera elevation delta
		XMVECTOR	eyeDir;
		XMVECTOR	aligned_data0;	// .xy = free, .z = time, .w = frame time delta

		uint32_t	frame; // *must be last*
	};
	struct no_vtable nk_uniform {
		XMMATRIX	projection;
	};

	// push constants > 
	
	// alignment on push constants is natural (ie float = 4bytes) to maximize availabilty of restricted size available to use (128 bytes)
	// Up to **128** bytes of immediate data. (Vulkan minimum spec is 128bytes, also is my Radeon 290 Limit)
	
	typedef struct no_vtable alignas(4) TextureShaderPushConstants { // 4+4+4 = 12 bytes
		XMFLOAT2	origin;
		float		frame_or_time;	// customizable per textureshader requirements
	} TextureShaderPushConstants;

	typedef struct no_vtable alignas(4) NuklearPushConstants { // 4+4 = 8 bytes
		uint32_t array_index,
				 type;
	} NuklearPushConstants;
	
	// overlapping ranges defined per struct (inherited)

	// ###pipeline### pushes their own specific range with size of the struct, offset is manually defined at compile time in order defined for the pipelinelayout below
	typedef struct no_vtable alignas(4) ComputeLightPushConstants { // 4+4+4 = 12 bytes
		int32_t		step;
		uint32_t	index_output,
					index_input;
	} ComputeLightPushConstants;

#ifndef NDEBUG
	struct no_vtable DebugStorageBuffer {
		XMVECTOR	numbers;
		bool		toggles[4];
		float		history[1024][1024];
	};
#endif

} // end ns UniformDecl

// special functions for declaration streaming sBatched
template<typename T>
INLINE_MEMFUNC __streaming_store(T* const __restrict dest, T const& __restrict src)
{
	// ** remeber if the program breaks here, the gpu voxel buffers are too conservative and there size needs to be increased. voxelAlloc.h [STATIC ALLOCATION SIZE]
	// 
	// VertexDecl::VoxelNormal works with _mm256 (fits size), 8 floats total / element
	_mm256_stream_ps((float* const __restrict)dest, _mm256_set_m128(src.uv_color, src.worldPos));
}

// special functions for declaration streaming sBatchedByIndexXXX variants
template<typename T>
INLINE_MEMFUNC __streaming_store_residual_out(T* const __restrict dest, T const& __restrict src, uint32_t const index) // single usage (residual/remainder)
{
	//*(dest + index) = *(src + index);
	_mm256_stream_ps((float* const __restrict)(dest + index), _mm256_set_m128(src.uv_color, src.worldPos));
}

template<typename T, size_t const Size>
INLINE_MEMFUNC __streaming_store_out(T* const __restrict dest, T const (&__restrict src)[Size], uint32_t const (&__restrict index)[Size]) // batches by size of 8, src should be recently cached values, dest is write-combined so the streaming stores are batched effectively here.
{
	static constexpr uint32_t const STREAM_BITS = 3,
		                            STREAM_COUNT = Size,
		                            STREAM_BATCH = (1 << STREAM_BITS);

#pragma loop( ivdep )
	for (uint32_t i = 0; i < STREAM_COUNT; i += STREAM_BATCH) {

		// unrolled by 8 seperating streaming stores
		__m256 const stream_in[STREAM_BATCH]{
			_mm256_set_m128(src[i].uv_color, src[i].worldPos),
			_mm256_set_m128(src[i + 1].uv_color, src[i + 1].worldPos),
			_mm256_set_m128(src[i + 2].uv_color, src[i + 2].worldPos),
			_mm256_set_m128(src[i + 3].uv_color, src[i + 3].worldPos),
			_mm256_set_m128(src[i + 4].uv_color, src[i + 4].worldPos),
			_mm256_set_m128(src[i + 5].uv_color, src[i + 5].worldPos),
			_mm256_set_m128(src[i + 6].uv_color, src[i + 6].worldPos),
			_mm256_set_m128(src[i + 7].uv_color, src[i + 7].worldPos)
		};

		//*(dest + index[i]) = *(src + index[i]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i]), stream_in[0]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 1]), stream_in[1]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 2]), stream_in[2]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 3]), stream_in[3]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 4]), stream_in[4]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 5]), stream_in[5]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 6]), stream_in[6]);
		_mm256_stream_ps((float* const __restrict)(dest + index[i + 7]), stream_in[7]);
	}
}

template<typename T>
INLINE_MEMFUNC __streaming_store_residual_in(T* const __restrict dest, T const* const __restrict src, uint32_t const index) // single usage (residual/remainder)
{
	//*(dest + index) = *(src + index);
	_mm256_stream_ps((float* const __restrict)dest, _mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index))));
}

template<typename T, size_t const Size>
INLINE_MEMFUNC __streaming_store_in(T* const __restrict dest, T const * const __restrict src, uint32_t const (&__restrict index)[Size]) // batches by size of 8, src should be recently cached values, dest is write-combined so the streaming stores are batched effectively here.
{
	static constexpr uint32_t const STREAM_BITS = 3,
		                            STREAM_COUNT = Size,
		                            STREAM_BATCH = (1 << STREAM_BITS);

#pragma loop( ivdep )
	for (uint32_t i = 0; i < STREAM_COUNT; i += STREAM_BATCH) {

		// unrolled by 8 seperating streaming stores
		__m256 const stream_in[STREAM_BATCH]{
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 1]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 2]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 3]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 4]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 5]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 6]))),
			_mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const* const __restrict)(src + index[i + 7])))
		};

		//*(dest + index[i]) = *(src + index[i]);
		_mm256_stream_ps((float* const __restrict)(dest + i), stream_in[0]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 1)), stream_in[1]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 2)), stream_in[2]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 3)), stream_in[3]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 4)), stream_in[4]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 5)), stream_in[5]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 6)), stream_in[6]);
		_mm256_stream_ps((float* const __restrict)(dest + (i + 7)), stream_in[7]);
	}
}

       