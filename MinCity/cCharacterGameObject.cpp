#include "pch.h"
#include "cCharacterGameObject.h"
#include "voxelModelInstance.h"
#include "MinCity.h"
#include "cPhysics.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cCharacterGameObject::remove(static_cast<cCharacterGameObject const* const>(_this));
		}
	}

	cCharacterGameObject::cCharacterGameObject(cCharacterGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src)), _animation(std::move(src._animation))
	{
		src.free_ownership();

		// important
		{
			if (Validate()) {
				Instance->setOwnerGameObject<cCharacterGameObject>(this, &OnRelease);
				Instance->setVoxelEventFunction(&cCharacterGameObject::OnVoxel);
			}
		}
		// important
		{
			if (src.Validate()) {
				Instance->setOwnerGameObject<cCharacterGameObject>(nullptr, nullptr);
				Instance->setVoxelEventFunction(nullptr);
			}
		}
	}
	cCharacterGameObject& cCharacterGameObject::operator=(cCharacterGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		{
			if (Validate()) {
				Instance->setOwnerGameObject<cCharacterGameObject>(this, &OnRelease);
				Instance->setVoxelEventFunction(&cCharacterGameObject::OnVoxel);
			}
		}
		// important
		{
			if (src.Validate()) {
				Instance->setOwnerGameObject<cCharacterGameObject>(nullptr, nullptr);
				Instance->setVoxelEventFunction(nullptr);
			}
		}

		_animation = std::move(src._animation);
		
		return(*this);
	}

	cCharacterGameObject::cCharacterGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _animation(instance_)
	{
		instance_->setOwnerGameObject<cCharacterGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cCharacterGameObject::OnVoxel);

		// random start angle
		//instance_->setYaw(v2_rotation_t(PsuedoRandomFloat() * XM_2PI));

		_animation.setRepeatFrameIndex(0);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCharacterGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cCharacterGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCharacterGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		//voxel.Color = 0x00ffffff;
		voxel.Emissive = true;

		return(voxel);
	}

	void cCharacterGameObject::setElevation(float const elevation)
	{
		[[unlikely]] if (!Validate())
			return;

		Instance->setElevation(elevation);
	}

	void cCharacterGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;

		if (_animation.update(*Instance, tDelta)) {

			return;
		}
	}


} // end ns world

