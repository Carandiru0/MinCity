#include "pch.h"
#include "cLightGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cLightGameObject::remove(static_cast<cLightGameObject const* const>(_this));
		}
	}

	cLightGameObject::cLightGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tNonUpdateableGameObject(instance_), _State(true), _Color(0x00ffffff)
	{
		instance_->setOwnerGameObject<cLightGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cLightGameObject::OnVoxel);
	}

	cLightGameObject::cLightGameObject(cLightGameObject&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cLightGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cLightGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cLightGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_State = std::move(src._State); src._State = false;
		_Color = std::move(src._Color); src._Color = 0;
	}
	cLightGameObject& cLightGameObject::operator=(cLightGameObject&& src) noexcept
	{
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cLightGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cLightGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cLightGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_State = std::move(src._State); src._State = false;
		_Color = std::move(src._Color); src._Color = 0;
		return(*this);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLightGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cLightGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLightGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		voxel.Emissive = _State; // the light can be turned off with just this bit

		voxel.Color = _Color;

		return(voxel);
	}



	cLightGameObject::~cLightGameObject()
	{

	}
} // end ns
