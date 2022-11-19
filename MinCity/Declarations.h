#pragma once

#include <Math/superfastmath.h>
#include <Utility/mem.h>
#include <Utility/class_helper.h>

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
		//__forceinline explicit __vectorcall VoxelNormal(FXMVECTOR worldPos_, FXMVECTOR uv_vr_) noexcept
		//	: worldPos(worldPos_), uv_vr(uv_vr_)
		//{}
		VoxelNormal() = default;
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
		VoxelDynamic() = default;
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
		XMMATRIX    viewproj;
		XMMATRIX	view;
		XMMATRIX	inv_view;
		XMMATRIX	proj;
		XMVECTOR	eyePos;
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

// special functions for declaration streaming
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelNormal* const __restrict dest, VertexDecl::VoxelNormal const& __restrict src)
{
	// ** remeber if the program breaks here, the gpu voxel buffers are too conservative and there size needs to be increased. voxelAlloc.h [STATIC ALLOCATION SIZE]
	// 
	// VertexDecl::VoxelNormal works with _mm256 (fits size), 8 floats total / element
	_mm256_stream_ps((float* const __restrict)dest, _mm256_set_m128(src.uv_color, src.worldPos));
}
INLINE_MEMFUNC __streaming_store(VertexDecl::VoxelDynamic* const __restrict dest, VertexDecl::VoxelDynamic const& __restrict src)
{
	// ** remeber if the program breaks here, the gpu voxel buffers are too conservative and there size needs to be increased. voxelAlloc.h [DYNAMIC ALLOCATION SIZE]
	// 
	// VertexDecl::VoxelDyanmic works with _mm256 (fits size), 8 floats total / element
	_mm256_stream_ps((float* const __restrict)dest, _mm256_set_m128(src.uv_color, src.worldPos));
}



