#include "pch.h"
#include "cAttachableGameObject.h"
#include "voxelModelInstance.h"
#include <Math/quat_t.h>

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cAttachableGameObject::remove(static_cast<cAttachableGameObject const* const>(_this));
		}
	}

	cAttachableGameObject::cAttachableGameObject(cAttachableGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cAttachableGameObject>(this, &OnRelease);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cAttachableGameObject>(nullptr, nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
		_offset = std::move(src._offset);
		_vPitch = std::move(src._vPitch);
		_vYaw = std::move(src._vYaw);
		_vRoll = std::move(src._vRoll);
	}
	cAttachableGameObject& cAttachableGameObject::operator=(cAttachableGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cAttachableGameObject>(this, &OnRelease);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cAttachableGameObject>(nullptr, nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
		_offset = std::move(src._offset);
		_vPitch = std::move(src._vPitch);
		_vYaw = std::move(src._vYaw);
		_vRoll = std::move(src._vRoll);

		return(*this);
	}

	cAttachableGameObject::cAttachableGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _parent(nullptr), _offset{}
	{
		instance_->setOwnerGameObject<cAttachableGameObject>(this, &OnRelease);
	}

	void cAttachableGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const tD(time_to_float(tDelta));

		auto const parentInstance(_parent->getModelInstance());
		if (nullptr == instance) {

			if (nullptr != parentInstance) {
				parentInstance->destroy(milliseconds(0));
			}
			return;
		}
		
		if (nullptr == parentInstance) {
			instance->destroy(milliseconds(0));
			return;
		}
		

		// inherit  
		{ // parent translation & rotation
			
			quat_t const qOrient(parentInstance->getPitch().angle(), parentInstance->getYaw().angle(), parentInstance->getRoll().angle()); // *bugfix - using quaternion on world transform (no gimbal lock)

			XMVECTOR const xmParentLocation(parentInstance->getLocation());
			XMVECTOR xmLocation(XMVectorAdd(xmParentLocation, XMVectorScale(XMLoadFloat3A(&_offset), Iso::MINI_VOX_STEP)));

			xmLocation = v3_rotate(xmLocation, xmParentLocation, qOrient);

			instance->setLocation(xmLocation);
			instance->setPitchYawRoll(parentInstance->getPitch() + _vPitch, parentInstance->getYaw() + _vYaw, parentInstance->getRoll() + _vRoll);
		}
	}

} // end ns world

