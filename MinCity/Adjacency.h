#pragma once
#include <Math/point2D_t.h>
#include "betterenums.h"

// "voxelindices" are the (0,0) to (X,Y) integral representation of Grid Space
// To transform to ScreenSpace, it must be transformed to Isometric World Space relative to the
// TL corner of screen voxel index
// then finally offset by the Camera pre-computed pixeloffset
// it is then in pixel space or alternatively screen space
// If the result is negative or greater than screenbounds (in pixels) it's not onscreen/visible



// Naturally objects not confined to integral voxel indices eg.) missiles, dynamically moving objects, camera
// should be represented by a vec2_t in Grid Space (-x,-y)=>(X,Y) form
// So that the world representation of the world origin lays at (0.0f, 0.0f)

// Layout of World (*GridSpace Coordinates (0,0)=>(X,Y) form) // Is an Isometric plane
/*
							   (0,288)
							   / *** \
				  y axis-->   /       \
							 /         \   <--- x axis
							/           \
						   /             \
						  *             *** (288,288)
					(0,0)  \             /
							\           /
				   x axis--> \         / <--- y axis
							  \       /
							   \     /
								\   /
								 *** (288,0)
*/
// Therefore the neighbours of a voxel, follow same isometric layout/order //
	/*

							   [NBR_TR]
					[NBR_T]				   [NBR_R]
		NBR_TL					VOXEL					NBR_BR
					NBR_L					NBR_B
								NBR_BL
	*/

	// defined in same order as constexpr NBR_TL => NBR_L (Clockwise order)
namespace world
{
	static constexpr uint32_t const ADJACENT_NEIGHBOUR_COUNT = 8;
	extern const __declspec(selectany) point2D_t const ADJACENT[ADJACENT_NEIGHBOUR_COUNT] = { point2D_t(-1, 1), point2D_t(0,  1), point2D_t(1,  1), point2D_t(1,   0),
																							  point2D_t(1,   -1), point2D_t(0,   -1), point2D_t(-1,  -1), point2D_t(-1,  0) };

	
	/*

							   NBR_TR
					[NBR_T]				   [NBR_R]
		NBR_TL					VOXEL					NBR_BR
					[NBR_L]				   [NBR_B]
								NBR_BL
	*/
	static constexpr uint32_t const
		NBR_TL(0),			// use these indices into ADJACENT[] to get a relativeOffset
		NBR_T(1),			// or iterate thru a loop of ADJACENT_NEIGHBOUR_COUNT with
		NBR_TR(2),			// the loop index into the ADJACENT[] 
		NBR_R(3),
		NBR_BR(4),
		NBR_B(5),
		NBR_BL(6),
		NBR_L(7);
} // end namespace

namespace Volumetric
{
	BETTER_ENUM(adjacency, uint32_t const,  // matching the same values to the below bit shift values
		below  = 5, // not used for adjacency in voxel desc, but needed for automata or any neighbourhood search that requires "below"
		left   = 4,
		right  = 3,
		front  = 2,
		back   = 1,
		above  = 0
	);

	// for voxelModel.h
	namespace voxB
	{
		// USED DURING RUNTIME TO CULL FACES
		// 
		// match inline uint32_t const			  getAdjacency() const { return((Left << 4U) | (Right << 3U) | (Front << 2U) | (Back << 1U) | (Above)); }
		static constexpr uint32_t const
			BIT_ADJ_LEFT  = (1 << adjacency::left),				
			BIT_ADJ_RIGHT = (1 << adjacency::right),			
			BIT_ADJ_FRONT = (1 << adjacency::front),				
			BIT_ADJ_BACK  = (1 << adjacency::back),				
			BIT_ADJ_ABOVE = (1 << adjacency::above);				
	} // end ns

} // end ns