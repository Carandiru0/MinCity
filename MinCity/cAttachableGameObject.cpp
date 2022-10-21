#include "pch.h"
#include "cAttachableGameObject.h"
#include "voxelModelInstance.h"
#include <Math/quat_t.h>

namespace world
{
	cAttachableGameObject::cAttachableGameObject(cAttachableGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		_parent = std::move(src._parent); src._parent = nullptr;
		_offset = std::move(src._offset);
		_vPitch = std::move(src._vPitch);
		_vYaw = std::move(src._vYaw);
		_vRoll = std::move(src._vRoll);
	}
	cAttachableGameObject& cAttachableGameObject::operator=(cAttachableGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		_parent = std::move(src._parent); src._parent = nullptr;
		_offset = std::move(src._offset);
		_vPitch = std::move(src._vPitch);
		_vYaw = std::move(src._vYaw);
		_vRoll = std::move(src._vRoll);

		return(*this);
	}

	cAttachableGameObject::cAttachableGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _parent(nullptr), _offset{}
	{
	}

	void cAttachableGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;
		[[unlikely]] if (!_parent->Validate()) {
			Instance->destroy(milliseconds(0));
			return;
		}

		float const tD(time_to_float(tDelta));

		auto const parentInstance(_parent->getModelInstance());

		// inherit  
		{ // parent translation & rotation
			
			quat_t const qOrient(parentInstance->getPitch().angle(), parentInstance->getYaw().angle(), parentInstance->getRoll().angle()); // *bugfix - using quaternion on world transform (no gimbal lock)

			XMVECTOR const xmParentLocation(parentInstance->getLocation());
			XMVECTOR xmLocation(XMVectorAdd(xmParentLocation, XMVectorScale(XMLoadFloat3A(&_offset), Iso::MINI_VOX_STEP)));

			xmLocation = v3_rotate(xmLocation, xmParentLocation, qOrient);

			Instance->setLocation(xmLocation);
			Instance->setPitchYawRoll(parentInstance->getPitch() + _vPitch, parentInstance->getYaw() + _vYaw, parentInstance->getRoll() + _vRoll);
		}
	}

} // end ns world

