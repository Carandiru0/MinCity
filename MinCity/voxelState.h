#pragma once
#include <vector>
#include "adjacency.h"

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
						Left : 1,						// dynamic adjacency/material
						Right : 1,
						Front : 1,
						Back : 1,
						Above : 1,
						Material : 3;

					uint8_t
						Reserved : 4,					// bit remain : Reserved

						Video: 1,						// Videoscreen
						Emissive : 1,					// Emission
						Transparent : 1,				// Transparency
						Hidden : 1						// Hidden (not visible)
						;
				};
				uint16_t State;
			};

			inline uint32_t const			  getAdjacency() const { return((Left << 4U) | (Right << 3U) | (Front << 2U) | (Back << 1U) | (Above)); }
			inline void						  setAdjacency(uint32_t const Adj) {
				Left = (0 != (BIT_ADJ_LEFT & Adj));
				Right = (0 != (BIT_ADJ_RIGHT & Adj));
				Front = (0 != (BIT_ADJ_FRONT & Adj));
				Back = (0 != (BIT_ADJ_BACK & Adj));
				Above = (0 != (BIT_ADJ_ABOVE & Adj));
			}

		} voxelState;

	} // end ns
} // end ns



