#include "pch.h"
#include "cBeaconGameObject.h"
#include <Random/superrandom.hpp>

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cBeaconGameObject::remove(static_cast<cBeaconGameObject const* const>(_this));
		}
	}

	cBeaconGameObject::cBeaconGameObject(cBeaconGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cBeaconGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cBeaconGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cBeaconGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_activity_light = std::move(src._activity_light);
	}
	cBeaconGameObject& cBeaconGameObject::operator=(cBeaconGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cBeaconGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cBeaconGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cBeaconGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_activity_light = std::move(src._activity_light);

		return(*this);
	}

	cBeaconGameObject::cBeaconGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _activity_light{}
	{
		instance_->setOwnerGameObject<cBeaconGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cBeaconGameObject::OnVoxel);

		_activity_light.interval = fp_seconds(SFM::lerp(INTERVAL_MIN, INTERVAL_MAX, PsuedoRandomFloat()));
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cBeaconGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cBeaconGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cBeaconGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		if (voxel.Emissive && COLOR_LIGHT == voxel.Color) { // is light voxel

			float const t = SFM::smoothstep(0.0f, 1.0f, 1.0f - SFM::saturate(time_to_float(_activity_light.accumulator / _activity_light.interval))) + 0.5f;
			uint32_t const luma = SFM::saturate_to_u8(t * 255.0f);
			voxel.Color = SFM::pack_rgba(luma);
			voxel.Emissive = (0 != voxel.Color);
		}
		return(voxel);
	}

	void cBeaconGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;

		_activity_light.accumulator += tDelta;
		if (_activity_light.accumulator >= _activity_light.interval) {
			_activity_light.accumulator -= _activity_light.interval;
		}
	}

} // end ns world

