#pragma once
#include <Utility/class_helper.h>
#include "voxelAlloc.h"
#include <Utility/bit_volume.h>

class no_vtable cPhysics : no_copy
{
	static constexpr uint32_t COHERENT = 1,
							  STAGING = 0;
	
	using force_volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;
public:
	void							add_force(uvec4_v const xmIndex) { uvec4_t xyzw; xmIndex.xyzw(xyzw); _force_field[STAGING]->set_bit(xyzw.x, xyzw.y, xyzw.z); }
	void __vectorcall				add_force(FXMVECTOR xmIndex) { add_force(SFM::floor_to_u32(xmIndex)); }
	XMVECTOR const __vectorcall		get_force(uvec4_v const xmIndex) const;
	XMVECTOR const __vectorcall		get_force(FXMVECTOR xmIndex) const { return(get_force(SFM::floor_to_u32(xmIndex))); }

public:
	bool const Initialize();
	void Update();
private:
	force_volume* __restrict _force_field[2];
public:
	cPhysics();
	~cPhysics() = default;  // uses CleanUp instead
	void CleanUp();
};