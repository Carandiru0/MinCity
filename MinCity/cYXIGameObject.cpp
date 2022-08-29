#include "pch.h"
#include "cYXIGameObject.h"
#include "voxelModelInstance.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cYXIGameObject::remove(static_cast<cYXIGameObject const* const>(_this));
		}
	}

	cYXIGameObject::cYXIGameObject(cYXIGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cYXIGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cYXIGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_body = std::move(src._body);
	}
	cYXIGameObject& cYXIGameObject::operator=(cYXIGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cYXIGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cYXIGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_body = std::move(src._body);

		return(*this);
	}

	cYXIGameObject::cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _body{}
	{
		instance_->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cYXIGameObject::OnVoxel);

		_body.mass = (float)getModelInstance()->getVoxelCount();
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cYXIGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cYXIGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cYXIGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

#ifdef GIF_MODE

		if (MASK_GLASS_COLOR == voxel.Color) {
			voxel.Color = _glass_color;
		}
		else if (MASK_BULB_COLOR == voxel.Color) {
			voxel.Color = _bulb_color;
		}

#else


#endif
		return(voxel);
	}

	void cYXIGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const t(time_to_float(tDelta));

		// Linear //
		{
			XMVECTOR const xmThrust(XMLoadFloat3A(&_body.thrust));

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

			XMVECTOR xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

			XMVECTOR xmPosition(instance->getLocation3D());

			xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmPosition);

			instance->setLocation3D(xmPosition);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));

			XMStoreFloat3A(&_body.velocity, xmVelocity);
			XMStoreFloat3A(&_body.thrust, XMVectorZero()); // reset required
		}

		// Angular //
		{
			XMVECTOR const xmThrust(XMLoadFloat3A(&_body.angular_thrust));

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

			v2_rotation_t const& xmRoll(instance->getRoll()), xmYaw(instance->getYaw()), xmPitch(instance->getPitch());
			XMVECTOR xmDir(XMVectorSet(xmRoll.angle(), xmYaw.angle(), xmPitch.angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmDir);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.angular_force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));

			XMFLOAT3A vAngles;
			XMStoreFloat3A(&vAngles, xmDir);

			instance->setRoll(v2_rotation_t(vAngles.x));
			instance->setYaw(v2_rotation_t(vAngles.y));
			instance->setPitch(v2_rotation_t(vAngles.z));

			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
		}

		// temp
		float const fElevation(Iso::MINI_VOX_SIZE * Iso::WORLD_MAX_HEIGHT * 0.5f);
		(*Instance)->setElevation(fElevation);
	}

	void __vectorcall cYXIGameObject::applyThrust(FXMVECTOR xmThrust)
	{
		XMStoreFloat3A(&_body.thrust, XMVectorAdd(XMLoadFloat3A(&_body.thrust), xmThrust));
	}
	void __vectorcall cYXIGameObject::applyAngularThrust(FXMVECTOR xmThrust)
	{
		XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(XMLoadFloat3A(&_body.angular_thrust), xmThrust));
	}

} // end ns world

