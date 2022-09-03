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

		_thruster = std::move(_thruster);
		_body = std::move(src._body);
		_mainthruster = std::move(src._mainthruster);
		_activity_lights = std::move(src._activity_lights);
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

		_thruster = std::move(_thruster);
		_body = std::move(src._body);
		_mainthruster = std::move(src._mainthruster);
		_activity_lights = std::move(src._activity_lights);

		return(*this);
	}

	cYXIGameObject::cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _thruster{}, _body{}, _mainthruster(false), _activity_lights{}
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

		switch (voxel.Color) {
			case COLOR_ACTIVITY_LIGHTS:
			{
				float const t = 1.0f - SFM::saturate(time_to_float(_activity_lights.accumulator / _activity_lights.interval));
				uint32_t const luma = SFM::saturate_to_u8(t * 255.0f);
				voxel.Color = SFM::pack_rgba(luma);
				voxel.Emissive = (0 != voxel.Color);
			}
			break;
			case COLOR_THRUSTER_CORE:
			case COLOR_THRUSTER_GRADIENT1:
			case COLOR_THRUSTER_GRADIENT2:
			case COLOR_THRUSTER_GRADIENT3:
			{
				XMVECTOR xmColor;

				uvec4_v vColor;
				uvec4_t rgba;
				float luma(0.0f);

				voxel.Emissive = _mainthruster;
				if (voxel.Emissive) {
					vColor = MinCity::VoxelWorld->blackbody(_thruster.tOn); // linear thruster to temperature
					luma = SFM::luminance(vColor);

					SFM::unpack_rgba(voxel.Color, rgba);
					xmColor = XMVectorScale(uvec4_v(rgba).v4f(), luma); // no need to normalize, scale by luma, then denormalize as they all multiply together and the two cancel out (so it remains in [0-255] range
					uvec4_v(xmColor).rgba(rgba);

					voxel.Color = SFM::pack_rgba(rgba); // uses the gradient color times the luminance of the blackbody linear color for the main thruster temperature
				}
				else {
					voxel.Color = 0;
				}
			}
			break;
		}

		return(voxel);
	}

	void cYXIGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const t(time_to_float(tDelta));

		_activity_lights.accumulator += tDelta;
		if (_activity_lights.accumulator >= _activity_lights.interval) {
			_activity_lights.accumulator -= _activity_lights.interval;
		}

		{
			// existing main thruster finished?
			if (0.0f != _thruster.tOn || 0.0 != _thruster.cooldown.count()) {

				_thruster.cooldown -= tDelta;

				if (_thruster.cooldown.count() <= 0.0) {
					_thruster.Off();
					_mainthruster = false;
				}
				else {
					float const t = time_to_float(_thruster.cooldown / THRUSTER_COOL_DOWN);

					XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster.thrust), t);
					_thruster.tOn = t;

					XMStoreFloat3A(&_body.thrust, xmThrust); // apply linealy decaying thruster to main thrust force tracking for this update
				}
			}
		}

		v2_rotation_t const& vRoll(instance->getRoll()), vYaw(instance->getYaw()), vPitch(instance->getPitch());

		// Angular // Sphere Thruster Engines //
		{
			// Orient angular thrust to ship orientation always. *do not change*
			XMVECTOR const xmThrust(v3_rotate_roll(v3_rotate_yaw(v3_rotate_pitch(XMLoadFloat3A(&_body.angular_thrust), vPitch), vYaw), vRoll));
			//XMVECTOR const xmThrust(XMLoadFloat3A(&_body.angular_thrust));

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.angular_force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));

			XMVECTOR xmDir(XMVectorSet(vRoll.angle(), vYaw.angle(), vPitch.angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmDir);

			XMFLOAT3A vAngles;
			XMStoreFloat3A(&vAngles, xmDir);

			instance->setRoll(v2_rotation_t(vAngles.x));
			instance->setYaw(v2_rotation_t(vAngles.y));
			instance->setPitch(v2_rotation_t(vAngles.z));

			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
		}

		// Linear // Main Thruster //
		{
			// Orient linear thrust to ship orientation always. *do not change*
			XMVECTOR const xmThrust(v3_rotate_roll(v3_rotate_yaw(v3_rotate_pitch(XMLoadFloat3A(&_body.thrust), vPitch), vYaw), vRoll));

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));

			XMVECTOR xmPosition(instance->getLocation3D());

			xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmPosition);

			instance->setLocation3D(xmPosition);

			XMStoreFloat3A(&_body.velocity, xmVelocity);
			XMStoreFloat3A(&_body.thrust, XMVectorZero()); // reset required
		}

		// temp
		float const fElevation(Iso::MINI_VOX_SIZE * Iso::WORLD_MAX_HEIGHT * 0.5f);
		(*Instance)->setElevation(fElevation);
	}

	void __vectorcall cYXIGameObject::applyThrust(FXMVECTOR xmThrust, bool const mainthruster)
	{
		if (mainthruster && XMVectorGetZ(xmThrust) > 0.0f) { // main forward thruster
			_thruster.On(xmThrust);
			_mainthruster = true;
		}
		
		XMStoreFloat3A(&_body.thrust, XMVectorAdd(XMLoadFloat3A(&_body.thrust), xmThrust));
	}
	void __vectorcall cYXIGameObject::applyAngularThrust(FXMVECTOR xmThrust)
	{
		XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(XMLoadFloat3A(&_body.angular_thrust), xmThrust));
	}

} // end ns world

