#pragma once

#ifndef GAMEOBJECT_T
#define GAMEOBJECT_T cPoliceCarGameObject
#endif
#include "cCarGameObject.h"

class cPoliceCarGameObject : public cCarGameObject // type colony becomes cPoliceCarGameObject
{
	friend cCarGameObject;

	static constexpr milliseconds const
		CAR_CREATE_INTERVAL = milliseconds(1500),
		CAR_IDLE_MAX = milliseconds(3500),
		CAR_PURSUIT_CHECK = milliseconds(5000);

	static constexpr uint32_t const
		MAX_CARS = 2;

	static constexpr float const
		MIN_SPEED = 36.0f,
		PURSUIT_SPEED = MIN_SPEED * 1.5f;

	static constexpr uint32_t const
		MASK_COLOR_BLUE = 0xff0000,		//bgra
		MASK_COLOR_RED = 0x0000ff;		//bgra

	static constexpr fp_seconds const
		LIGHT_SWITCH_INTERVAL = duration_cast<fp_seconds>(milliseconds(75));
	static constexpr float const
		INV_LIGHT_SWITCH_INTERVAL = 1.0f / LIGHT_SWITCH_INTERVAL.count();
public:
#ifndef NDEBUG
	// every child of this class should override to_string with approprate string
	virtual std::string_view const to_string() const override { return("cPoliceCarGameObject"); }
#endif
	void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	// typedef Volumetric::voxB::voxelState const(* const voxel_event_function)(void* const _this, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState);
	static Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
	Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

	static void Initialize(tTime const& __restrict tNow);
	static void UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
protected:
	void setInitialState(cCarGameObject::state&& initialState);
private:
	static void CreateCar();
public:
	cPoliceCarGameObject(cPoliceCarGameObject&& src) noexcept;
	cPoliceCarGameObject& operator=(cPoliceCarGameObject&& src) noexcept;
private:
	struct {

		tTime			checked_last;

		fp_seconds		tLastLights;
		int32_t			stateLights,
						laststateLights;
		uint32_t		colorBlueLight,
						colorRedLight;
		bool			bLightsOn;

	} _this = {};

public:
	cPoliceCarGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
};

STATIC_INLINE_PURE void swap(cPoliceCarGameObject& __restrict left, cPoliceCarGameObject& __restrict right) noexcept
{
	cPoliceCarGameObject tmp{ std::move(left) };
	left = std::move(right);
	right = std::move(tmp);

	left.revert_free_ownership();
	right.revert_free_ownership();
}


