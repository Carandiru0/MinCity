#pragma once
#include <Utility/class_helper.h>
#include "voxelAlloc.h"
#include <Utility/bit_volume.h>

class no_vtable cPhysics : no_copy
{
	static constexpr float const GRAVITY_EARTH = -9.80665f;	// https://physics.nist.gov/cuu/Constants/index.html (exact value) m/s*s [acceleration]
	static constexpr float const GRAVITY_MOON = -1.625f;	// (exact value) m/s*s [acceleration]
public:
	static constexpr float const GRAVITY = GRAVITY_MOON;
	static constexpr float const MIN_FORCE = 1.0f;		// Epsilon for minimum force 1 N [force]	// smallest force to move a voxel, at 1 voxel per second
	static constexpr float const TORQUE_OFFSET_SCALAR = 0.33333333f; // affects how forceful rotation is (torque). compare with linear force. set as desired.
	static constexpr float const VELOCITY_EPSILON = Iso::MINI_VOX_SIZE * 0.5f;
	static constexpr float const FORCE_EPSILON = 0.1f;

	// convert # of voxels to meters
	static constexpr float const voxels_to_meters(float const voxel_count) {
		return(voxel_count * Iso::VOX_STEP * 0.92f); // adjustment factor to more closely match an accurate rate of acceleration. [within ~250ms of a real fall of equivalent time and gravity]
	}
	// convert # of voxels to mass
	static constexpr float const voxels_to_kg(float const voxel_count) {
		return(voxels_to_meters(voxel_count)); // same scale
	}
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

#define voxels_to_meters cPhysics::voxels_to_meters
#define voxels_to_kg cPhysics::voxels_to_kg