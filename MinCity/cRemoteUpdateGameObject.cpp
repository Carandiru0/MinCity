#include "pch.h"
#include "cRemoteUpdateGameObject.h"
#include "voxelModelInstance.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cRemoteUpdateGameObject::remove(static_cast<cRemoteUpdateGameObject const* const>(_this));
		}
	}

	cRemoteUpdateGameObject::cRemoteUpdateGameObject(cRemoteUpdateGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cRemoteUpdateGameObject>(this, &OnRelease);
			setUpdateFunction(src._eOnUpdate);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cRemoteUpdateGameObject>(nullptr, nullptr);
			src.setUpdateFunction(nullptr);
		}
	}
	cRemoteUpdateGameObject& cRemoteUpdateGameObject::operator=(cRemoteUpdateGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cRemoteUpdateGameObject>(this, &OnRelease);
			setUpdateFunction(src._eOnUpdate);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cRemoteUpdateGameObject>(nullptr, nullptr);
			src.setUpdateFunction(nullptr);
		}

		return(*this);
	}

	cRemoteUpdateGameObject::cRemoteUpdateGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _eOnUpdate(nullptr)
	{
		instance_->setOwnerGameObject<cRemoteUpdateGameObject>(this, &OnRelease);
	}

	void cRemoteUpdateGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		if (_eOnUpdate) {

			auto const [xmLocation, vYaw] = _eOnUpdate((*Instance)->getLocation(), (*Instance)->getYaw(), tNow, tDelta, (*Instance)->getHash());

			// extract elevation, and swizzle back to 2D vector for function
			//(*Instance)->setLocation3DYaw(xmLocation, vYaw);
		}
	}


} // end ns world

