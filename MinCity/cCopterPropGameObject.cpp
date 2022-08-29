#include "pch.h"
#include "cCopterPropGameObject.h"
#include "cCopterGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cCopterPropGameObject::remove(static_cast<cCopterPropGameObject const* const>(_this));
		}
	}

	cCopterPropGameObject::cCopterPropGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_)
	{
		instance_->setOwnerGameObject<cCopterPropGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cCopterPropGameObject::OnVoxel);
	}

	cCopterPropGameObject::cCopterPropGameObject(cCopterPropGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cCopterPropGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cCopterPropGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cCopterPropGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_this = std::move(src._this);
	}
	cCopterPropGameObject& cCopterPropGameObject::operator=(cCopterPropGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cCopterPropGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cCopterPropGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cCopterPropGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_this = std::move(src._this);

		return(*this);
	}

	void cCopterPropGameObject::SetOwnerCopter(cCopterGameObject* const& owner)
	{
		_this.owner_copter = owner;
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCopterPropGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cCopterPropGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCopterPropGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		int32_t indexLight{};

		switch (voxel.Color)
		{
		case MASK_LIGHT_ONE:
			indexLight = 0;
			break;
		case MASK_LIGHT_TWO:
			indexLight = 1;
			break;
		case MASK_LIGHT_THREE:
			indexLight = 2;
			break;
		case MASK_LIGHT_FOUR:
			indexLight = 3;
			break;
		default:
			return(voxel); // any other voxel returns unchanged
		}

		// parallel friendly algorithm (read-only)

		if (_this.bLightsOn) {

			voxel.Color = _this.colorLights[indexLight];
			voxel.Emissive = (0 != voxel.Color);
		}
		else {

			voxel.Color = 0;
			voxel.Emissive = false;
		}

		// this is only reachable if this voxel is a masked light
		return(voxel);
	}

	void __vectorcall cCopterPropGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, FXMVECTOR xmLocation, float const fElevation, v2_rotation_t const& Yaw)
	{
		// some spinning for copter prop
		_this.angle -= tDelta.count() * 11.0f;

		// inhreit parent location & orientation
		(*Instance)->setLocationYaw(xmLocation, Yaw + _this.angle);
		(*Instance)->setElevation(fElevation);

		// chsnging prop light
		float const fFaster((_this.bLightsOn ? (1.0f/3.0f) : 1.0f));
		fp_seconds const fInterval(LIGHT_SWITCH_INTERVAL * fFaster);

		if ((_this.tLastLights += tDelta) > fInterval) {

			if (++_this.idleLightOnIndex == NUM_LIGHTS)
				_this.idleLightOnIndex = 0;

			_this.tLastLights -= fInterval;
		}
			
		// set all lights to zero color
		for (uint32_t iDx = 0; iDx < NUM_LIGHTS; ++iDx) {
			_this.colorLights[iDx] = 0; // lights off
		}

		// set lights that are partially or fully on
		uint32_t const lastLightOnIndex((0 == _this.idleLightOnIndex ? NUM_LIGHTS - 1 : _this.idleLightOnIndex - 1));

		_this.colorLights[lastLightOnIndex] = SFM::lerp(MASK_COLOR_WHITE, 0, 1.0f / time_to_float(fInterval));
		_this.colorLights[_this.idleLightOnIndex] = MASK_COLOR_WHITE;
	}

	cCopterPropGameObject::~cCopterPropGameObject()
	{
		if (_this.owner_copter) {
			_this.owner_copter->releasePart(this);
		}
	}
} // end ns
