#pragma once
#include <vector>

namespace Volumetric
{
	namespace voxB
	{
		typedef struct voxelStateGroup {

			vector<struct voxelState*> groupedstates;

			XMINT3 const	BoundsMin, BoundsMax;

			voxelStateGroup(XMINT3 const vBoundsMin, XMINT3 const vBoundsMax)
				: BoundsMin(vBoundsMin), BoundsMax(vBoundsMax)
			{}
		} voxelStateGroup;

		typedef struct voxelState {

			// State that is NOT saved out to file //
			union
			{
				struct
				{
					uint8_t
						Reserved : 4,					// bit remain : Reserved

						Video: 1,						// Videoscreen
						Emissive : 1,					// Emission
						Transparent : 1,				// Transparency
						Hidden : 1						// Hidden (not visible)
						;
				};
				uint8_t State;
			};

		} voxelState;

	} // end ns
} // end ns



