#pragma once

namespace Volumetric
{
	namespace ComputeLightConstants
	{
		static constexpr uint32_t const		// constants to determine index x,y,z of light in light probe map
			LIGHT_MOD_BITS = 2U;

		using memLayoutV = XMFLOAT4A;

		static constexpr uint32_t const
			NUM_BYTES_PER_VOXEL_LIGHT = sizeof(memLayoutV),

			SHADER_LOCAL_SIZE_BITS = 3U, // 2^3 = 8   ,   2^5 = 32    , etc.		
			SHADER_LOCAL_SIZE = (1U << SHADER_LOCAL_SIZE_BITS); // must match GROUP_SIZE_XYZ in light.comp

	} // end ns

} // end ns