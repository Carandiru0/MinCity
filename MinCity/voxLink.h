#pragma once
#include "volumetricOpacity.h"
#include "volumetricVisibility.h"
#include "voxelAlloc.h"
#include "voxelKonstants.h"


// forward declarations //
namespace world {
	class cVoxelWorld;
} // end ns world


namespace Volumetric
{
#pragma warning( disable : 4359 )						// 512x512x512 (World Volume Size) //
	using voxelOpacity = Volumetric::volumetricOpacity<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;
	using voxelVisibility = Volumetric::volumetricVisibility;

	typedef struct voxLink
	{
		world::cVoxelWorld& __restrict		World;
		voxelOpacity const& __restrict		Opacity;
		voxelVisibility const& __restrict	Visibility;

	} voxLink;

	extern voxLink* VolumetricLink;
} // end ns

