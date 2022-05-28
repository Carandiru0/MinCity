#pragma once

namespace Volumetric
{
	namespace ComputeLightConstants
	{
		static constexpr uint32_t const		// constants to determine index x,y,z of light in light probe map
			LIGHT_MOD_WIDTH_BITS = 2U,
			LIGHT_MOD_WIDTH = (1 << LIGHT_MOD_WIDTH_BITS),
			LIGHT_MOD_HEIGHT_BITS = 2U,
			LIGHT_MOD_HEIGHT = (1 << LIGHT_MOD_HEIGHT_BITS),
			LIGHT_MOD_DEPTH_BITS = LIGHT_MOD_WIDTH_BITS,
			LIGHT_MOD_DEPTH = (1 << LIGHT_MOD_DEPTH_BITS);

		static constexpr float const
			INV_LIGHT_MOD_WIDTH = 1.0f / float(LIGHT_MOD_WIDTH),
			INV_LIGHT_MOD_HEIGHT = 1.0f / float(LIGHT_MOD_HEIGHT),
			INV_LIGHT_MOD_DEPTH = 1.0f / float(LIGHT_MOD_DEPTH);

		using memLayoutV = XMFLOAT4A;
		using memLayout = uint8_t;

		static constexpr uint32_t const
			NUM_BYTES_PER_VOXEL_LIGHT = sizeof(memLayoutV),
			NUM_BYTES_PER_VOXEL = sizeof(memLayout),
			SHADER_LOCAL_SIZE_BITS = 3U, // 2^3 = 8   ,   2^5 = 32    , etc.
			SHADER_LOCAL_SIZE = (1U << SHADER_LOCAL_SIZE_BITS),
			SHADER_LOCAL_SIZE_BITS_Z = 2U,
			SHADER_LOCAL_SIZE_Z = (1U << SHADER_LOCAL_SIZE_BITS_Z);
	} // end ns

} // end ns