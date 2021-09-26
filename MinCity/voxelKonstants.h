#pragma once
#include <stdint.h>


namespace Volumetric
{
	namespace voxB
	{
		static constexpr bool const DYNAMIC = true,
									STATIC = false;
	}

	BETTER_ENUM(eVoxelType, uint32_t const,
		opaque = 0,
		trans
	);
	BETTER_ENUM(eVoxelTransparency, uint32_t const,
		ALPHA_25 = 63,
		ALPHA_50 = 127,
		ALPHA_75 = 191,
		ALPHA_100 = 255
	);

	BETTER_ENUM(eVoxelModelInstanceFlags, uint32_t const,
		RESERVED = 0,
		INSTANT_CREATION = (1 << 0),
		UPDATEABLE = (1 << 1),
		GROUND_CONDITIONING = (1 << 2),
		DESTROY_EXISTING_STATIC = (1 << 3),
		DESTROY_EXISTING_DYNAMIC = (1 << 4),
		CHILD_ONLY = (1 << 5),
		// Last
		EMPTY_INSTANCE = (1u << 31u)
	);

	namespace Konstants
	{
		static constexpr uint32_t const NO_TRANSPARENCY = eVoxelTransparency::ALPHA_100;
		static constexpr uint32_t const DEFAULT_TRANSPARENCY = eVoxelTransparency::ALPHA_25; // very good default *don't change*

		static constexpr uint32_t const PALETTE_WINDOW_INDEX = 0x00FFFFFF;  // (pure white)

		static constexpr uint32_t const DESTRUCTION_SEQUENCE_LENGTH(42 >> 1);										// base sequence length, will be used as: # of slices (height) * DESTRUCTION_SEQUENCE_LENGTH
		static constexpr uint32_t const CREATION_SEQUENCE_LENGTH((DESTRUCTION_SEQUENCE_LENGTH + (DESTRUCTION_SEQUENCE_LENGTH << 1)) << 1);			// ""				""	""					""
		
		static constexpr float const VOXEL_RAIN_SCALE = 0.5f;
	
	} // end ns
} // end ns
