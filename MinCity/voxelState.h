#pragma once
#include <vector>
#include "adjacency.h"

namespace Volumetric
{
	namespace voxB
	{
		// not used anymore
		/*
		typedef struct voxelStateGroup {

			vector<struct voxelState*> groupedstates;

			XMINT3 const	BoundsMin, BoundsMax;

			voxelStateGroup(XMINT3 const vBoundsMin, XMINT3 const vBoundsMax)
				: BoundsMin(vBoundsMin), BoundsMax(vBoundsMax)
			{}
		} voxelStateGroup;

		typedef struct voxelState {

			// Dynamic State that can change a voxel's attributes //
			union
			{
				struct
				{
					uint8_t
						Reserved : 4,					// bit remain : Reserved

						Video : 1,						// Videoscreen
						Emissive : 1,					// Emission
						Transparent : 1,				// Transparency
						Hidden : 1;						// Hidden (not visible)
				};
				uint8_t State;
			};

		} voxelState;
		*/
	} // end ns
} // end ns



