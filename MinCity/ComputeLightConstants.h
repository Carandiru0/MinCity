#pragma once
#include "IsoVoxel.h"

namespace Volumetric
{
	namespace ComputeLightConstants
	{
		static constexpr uint32_t const
			LIGHT_RESOLUTION = std::min(128u, Iso::SCREEN_VOXELS << 1u);  // too high and its too taxing for gpu -------- *must* be same resolution as Iso::SCREEN_VOXELS - this synchronizes the light positions with the world movement, so there is no jerky motion! *** important *** //
		
		using memLayoutV = DirectX::PackedVector::XMUSHORTN4;

		static constexpr uint32_t const
			NUM_BYTES_PER_VOXEL_LIGHT = sizeof(memLayoutV),

			SHADER_LOCAL_SIZE_BITS_X = 2U, // 2^3 = 8   ,   2^5 = 32    , etc.	
			SHADER_LOCAL_SIZE_BITS_Y = 2U,
			SHADER_LOCAL_SIZE_BITS_Z = 2U,

			SHADER_LOCAL_SIZE_X = (1U << SHADER_LOCAL_SIZE_BITS_X), // must match GROUP_SIZE_XYZ in light.comp
			SHADER_LOCAL_SIZE_Y = (1U << SHADER_LOCAL_SIZE_BITS_Y),
			SHADER_LOCAL_SIZE_Z = (1U << SHADER_LOCAL_SIZE_BITS_Z);

	} // end ns

} // end ns