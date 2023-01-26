#pragma once
#include "IsoVoxel.h"

namespace Volumetric
{
	namespace ComputeLightConstants
	{
		static constexpr uint32_t const
			LIGHT_RESOLUTION = 128;  // any higher and its too taxing for gpu // Iso::SCREEN_VOXELS_XZ >> 1;        // Half-resolution of visible volume works best //

		using memLayoutV = XMFLOAT4A;

		static constexpr uint32_t const
			NUM_BYTES_PER_VOXEL_LIGHT = sizeof(memLayoutV),

			SHADER_LOCAL_SIZE_BITS = 3U, // 2^3 = 8   ,   2^5 = 32    , etc.		
			SHADER_LOCAL_SIZE = (1U << SHADER_LOCAL_SIZE_BITS); // must match GROUP_SIZE_XYZ in light.comp

	} // end ns

} // end ns