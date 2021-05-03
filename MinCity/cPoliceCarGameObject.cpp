#include "pch.h"
#include "eDirection.h"
#include "cPoliceCarGameObject.h"

static constexpr int32_t const PURSUIT_CHANCE = 33;

namespace light_state
{
	static constexpr int32_t const
		DEFAULT = 0,
		CHANGING = -1,
		OPPOSITE = 1;
}// end ns

static void OnRelease(void const* const __restrict _this) // private to this file
{
	if (_this) {
		cPoliceCarGameObject::remove(static_cast<cPoliceCarGameObject const* const>(_this));
	}
}

cPoliceCarGameObject::cPoliceCarGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
	: cCarGameObject(instance_)
{
	instance_->setOwnerGameObject<cPoliceCarGameObject>(this, &OnRelease);
	instance_->setVoxelEventFunction(&cPoliceCarGameObject::OnVoxel);

}

cPoliceCarGameObject::cPoliceCarGameObject(cPoliceCarGameObject&& src) noexcept
	: cCarGameObject(std::forward<cCarGameObject&&>(src))
{
	// important 
	src.free_ownership();

	// important
	if (Instance && *Instance) {
		(*Instance)->setOwnerGameObject<cPoliceCarGameObject>(this, &OnRelease);
		(*Instance)->setVoxelEventFunction(&cPoliceCarGameObject::OnVoxel);
	}
	// important
	if (src.Instance && *src.Instance) {
		(*src.Instance)->setOwnerGameObject<cPoliceCarGameObject>(nullptr, nullptr);
		(*src.Instance)->setVoxelEventFunction(nullptr);
	}

	_this = std::move(src._this);
}
cPoliceCarGameObject& cPoliceCarGameObject::operator=(cPoliceCarGameObject&& src) noexcept
{
	cCarGameObject::operator=(std::forward<cCarGameObject&&>(src));
	// important 
	src.free_ownership();

	// important
	if (Instance && *Instance) {
		(*Instance)->setOwnerGameObject<cPoliceCarGameObject>(this, &OnRelease);
		(*Instance)->setVoxelEventFunction(&cPoliceCarGameObject::OnVoxel);
	}
	// important
	if (src.Instance && *src.Instance) {
		(*src.Instance)->setOwnerGameObject<cPoliceCarGameObject>(nullptr, nullptr);
		(*src.Instance)->setVoxelEventFunction(nullptr);
	}

	_this = std::move(src._this);

	return(*this);
}

// If currently visible event:
Volumetric::voxB::voxelState const cPoliceCarGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
{
	return(reinterpret_cast<cPoliceCarGameObject const* const>(_this)->OnVoxel(voxel, rOriginalVoxelState, vxl_index));
}
// ***** watchout - thread safety is a concern here this method is executed in parallel ******
Volumetric::voxB::voxelState const cPoliceCarGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
{
	cCarGameObject::OnVoxel(voxel, rOriginalVoxelState, vxl_index);

	Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);

	if (_this.bLightsOn) {

		if (MASK_COLOR_BLUE == voxel.Color) {
			voxel.Color = _this.colorBlueLight;
		}
		else if (MASK_COLOR_RED == voxel.Color) {
			voxel.Color = _this.colorRedLight;
		}
	}
	else {
		if (MASK_COLOR_BLUE == voxel.Color || MASK_COLOR_RED == voxel.Color) {
			voxel.Color = 0x00000000; // lights off
			voxelState.Emissive = false;
		}
	}
		
	return(voxelState);
}

void __vectorcall cPoliceCarGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	cCarGameObject::OnUpdate(tNow, tDelta);

	if (tNow - _this.checked_last > CAR_PURSUIT_CHECK) {

		_this.checked_last = tNow;

		bool const next_pursuit = PsuedoRandomNumber(0, 100) < PURSUIT_CHANCE;

		if (isMoving() && next_pursuit) { // only start new pursuit if car is moving
			_speed = PURSUIT_SPEED;
			_this.bLightsOn = true;
		}
		else {
			_speed = MIN_SPEED;
			_this.bLightsOn = false;
		}
	}

	if (_this.bLightsOn) {
		if ((_this.tLastLights += tDelta) > LIGHT_SWITCH_INTERVAL) {

			// update last full state
			if (light_state::CHANGING != _this.stateLights) {
				_this.laststateLights = _this.stateLights;
			}

			// setup new state
			switch (_this.stateLights)
			{
			case light_state::DEFAULT:
			case light_state::OPPOSITE:
				_this.stateLights = light_state::CHANGING;
				break;
			default:
				_this.stateLights = (light_state::DEFAULT == _this.laststateLights ? light_state::OPPOSITE : light_state::DEFAULT);
				break;
			}

			// set colors for new full state
			switch (_this.stateLights)
			{
			case light_state::DEFAULT:
				_this.colorBlueLight = MASK_COLOR_BLUE;
				_this.colorRedLight = MASK_COLOR_RED;
				break;
			case light_state::OPPOSITE:
				_this.colorBlueLight = MASK_COLOR_RED;
				_this.colorRedLight = MASK_COLOR_BLUE;
				break;
				// if state is changing, leave it unchanged here, change in transition below
			}
			_this.tLastLights -= LIGHT_SWITCH_INTERVAL;
		}
		else { // no switch

			if (light_state::CHANGING == _this.stateLights) {

				float const tFade(_this.tLastLights.count() * INV_LIGHT_SWITCH_INTERVAL);

				_this.colorBlueLight = SFM::lerp(_this.colorBlueLight, 0, tFade);
				_this.colorRedLight = SFM::lerp(_this.colorRedLight, 0, tFade);
			}
		}
	}
}

void cPoliceCarGameObject::setInitialState(cCarGameObject::state&& initialState)
{
	cCarGameObject::setInitialState(std::forward<cCarGameObject::state&&>(initialState));

	_speed = MIN_SPEED;
}

// STATIC METHODS : //

void cPoliceCarGameObject::Initialize(tTime const& __restrict tNow)
{
	reserve(MAX_CARS);
}
void cPoliceCarGameObject::UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	if (size() < MAX_CARS) {

		static tTime tLastCreation(zero_time_point);

		if (tNow - tLastCreation > milliseconds(CAR_CREATE_INTERVAL)) {
			CreateCar();
			tLastCreation = tNow;
		}
	}

	auto it = begin();
	while (end() != it) {

		it->OnUpdate(tNow, tDelta);
		++it;
	}
}

void cPoliceCarGameObject::CreateCar()
{
	cCarGameObject::CreateCar<cPoliceCarGameObject>(Volumetric::eVoxelModels_Indices::POLICE_CAR);
}

