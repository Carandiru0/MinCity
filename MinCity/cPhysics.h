#pragma once
#include <Utility/class_helper.h>
#include "voxelAlloc.h"
#include <Utility/bit_volume.h>

class no_vtable cPhysics : no_copy
{
public:
	static constexpr float const GRAVITY = -9.80665f;	// https://physics.nist.gov/cuu/Constants/index.html (exact value) m/s*s [acceleration]
	static constexpr float const MIN_FORCE = 1.0f;		// Epsilon for minimum force 1 N [force]	// smallest force to move a voxel, at 1 voxel per second

private:
	static constexpr uint32_t COHERENT = 1,
							  STAGING = 0;
	
	using force_volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;	// 16 MB
public:
	XMVECTOR const __vectorcall		get_force(FXMVECTOR xmIndex) const { return(get_force(SFM::floor_to_u32(xmIndex))); }

	void __vectorcall				add_force(FXMVECTOR xmIndex) { add_force(SFM::floor_to_u32(xmIndex)); }
	
private:
	XMVECTOR const __vectorcall		get_force(uvec4_v const xmIndex) const;
	
	void __vectorcall				add_force(uvec4_v const xmIndex) {
		uvec4_t local;
		xmIndex.xyzw(local);

		size_t const index(force_volume::get_index(local.x, local.y, local.z));

		_force_field_direction[STAGING]->set_bit(index);
	}
public:
	bool const Initialize();
	void AsyncClear();
	void Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
private:
	force_volume* __restrict _force_field_direction[2]; // 16.7MB each

	int64_t					 _AsyncClearTaskID;
public:
	cPhysics();
	~cPhysics() = default;  // uses CleanUp instead
	void CleanUp();
};