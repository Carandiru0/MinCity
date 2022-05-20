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
	void							add_force(size_t const x, size_t const y, size_t const z) { _force_field[STAGING]->set_bit(x, y, z); }
	void __vectorcall				add_force(FXMVECTOR xmIndex) { uvec4_t coord; SFM::round_to_u32(xmIndex).xyzw(coord); _force_field[STAGING]->set_bit(coord.x, coord.y, coord.z); }
	XMVECTOR const __vectorcall		get_force(size_t const x, size_t const y, size_t const z) const;
	XMVECTOR const __vectorcall		get_force(FXMVECTOR xmIndex) const { uvec4_t coord; SFM::round_to_u32(xmIndex).xyzw(coord); return(get_force(coord.x, coord.y, coord.z)); }

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