#include "pch.h"
#include "cYXIGameObject.h"
#include "voxelModelInstance.h"
#include <Math/quat_t.h>
#include "cPhysics.h"
#include "cUser.h"


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

		for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) {
			_thruster[i] = std::move(_thruster[i]);
		}
		_body = std::move(src._body);
		_activity_lights = std::move(src._activity_lights);
		_parent = std::move(src._parent); src._parent = nullptr;

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

		for (uint32_t i = 0 ; i < THRUSTER_COUNT ; ++i) {
			_thruster[i] = std::move(_thruster[i]);
		}
		_body = std::move(src._body);
		_activity_lights = std::move(src._activity_lights);
		_parent = std::move(src._parent); src._parent = nullptr;

		return(*this);
	}

	cYXIGameObject::cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _thruster{ {MAIN_THRUSTER_COOL_DOWN}, {UP_THRUSTER_COOL_DOWN} }, _body{}, _activity_lights{},
		_parent(nullptr)
	{
		instance_->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cYXIGameObject::OnVoxel);

		_body.mass = voxels_to_kg((float)getModelInstance()->getVoxelCount());

		float const fElevation(Iso::WORLD_MAX_HEIGHT * Iso::VOX_STEP * 0.5f);
		
		instance_->resetElevation(fElevation);

		XMStoreFloat3A(&_body.future_position, instance_->getLocation());
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
				voxel.Emissive = _thruster[MAIN].bOn;
				if (voxel.Emissive) {

					XMVECTOR xmColor;

					uvec4_v vColor;
					uvec4_t rgba;
					float luma(0.0f);

					vColor = MinCity::VoxelWorld->blackbody(_thruster[MAIN].tOn); // linear thruster to temperature
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
			case COLOR_THRUSTER_UP:
			{
				voxel.Emissive = _thruster[UP].bOn;
				if (voxel.Emissive) {
					XMVECTOR xmColor;
					uvec4_v vColor;
					uvec4_t rgba[2];
					float luma(0.0f);

					vColor = MinCity::VoxelWorld->blackbody(_thruster[UP].tOn); // linear thruster to temperature
					luma = SFM::luminance(vColor);

					SFM::unpack_rgba(COLOR_THRUSTER_GRADIENT_MIN, rgba[0]);
					SFM::unpack_rgba(COLOR_THRUSTER_GRADIENT_MAX, rgba[1]);
					xmColor = SFM::lerp(uvec4_v(rgba[0]).v4f(), uvec4_v(rgba[1]).v4f(), luma); // no need to normalize, scale by luma, then denormalize as they all multiply together and the two cancel out (so it remains in [0-255] range
					xmColor = XMVectorScale(xmColor, luma);
					uvec4_v(xmColor).rgba(rgba[0]);

					voxel.Color = SFM::pack_rgba(rgba[0]); // uses the gradient color times the luminance of the blackbody linear color for the main thruster temperature
				}
				else {
					voxel.Color = 0;
				}
			}
			break;
		}

		return(voxel);
	}

	void cYXIGameObject::autoThrust(float const ground_clearance, fp_seconds const tDelta) // automatic thrusting (up thruster only)
	{
		auto const instance(*Instance);
		float const tD(time_to_float(tDelta));

		// negative velocity current on y-axis?
		if (_body.velocity.y + cPhysics::VELOCITY_EPSILON < 0.0f) {

			static constexpr float const one_second = time_to_float(duration_cast<fp_seconds>(milliseconds(1000)));

			float const targetHeight(ground_clearance + Iso::VOX_STEP * instance->getModel()._Extents.y); // additional clearance equal to the extent of the ship on the y axis

			float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT, Iso::VOX_STEP, Iso::VOX_STEP * instance->getModel()._Extents.y);
			float const elevation(_body.future_position.y);

			// up to maximum thrust (up thruster) capability
			float const counter_thrust = _body.mass * SFM::min(MAX_UP_THRUST, SFM::lerp(0.0f, MAX_UP_THRUST, SFM::smoothstep(0.0f, 1.0f, (1.0f - SFM::saturate(elevation / maximum_clearance)))));

			// get future height (output) 
			XMVECTOR const xmForce(XMVectorAdd(XMVectorSet(0.0f, counter_thrust, 0.0f, 0.0f), XMLoadFloat3A(&_body.force[in]))); // adding external forces in

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(one_second), xmInitialVelocity);

			XMVECTOR xmPosition(instance->getLocation());

			xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(one_second), xmPosition);

			float const futureHeight(XMVectorGetY(xmPosition));

			if (futureHeight < targetHeight) {
				applyThrust(XMVectorSet(0.0f, SFM::min(_body.mass * MAX_UP_THRUST, counter_thrust), 0.0f, 0.0f)); // up thruster - must thrust greater or equal to acceleration of gravity to maintain elevation.
				_thruster[UP].bAuto = true; // flag thruster as automatic
			}
			else {
				//FMT_NUKLEAR_DEBUG_OFF();
				_thruster[UP].bAuto = false; // auto-reset to transition to thrust off
			}
		}
	}

	bool const cYXIGameObject::collide(float const ground_clearance, fp_seconds const tDelta)
	{
		auto instance(*Instance);
		float const tD(time_to_float(tDelta));

		XMFLOAT3A vPosition;
		XMStoreFloat3A(&vPosition, XMLoadFloat3A(&_body.future_position));

		float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT, Iso::VOX_STEP, Iso::VOX_STEP * instance->getModel()._Extents.y);

		// clamp height to:
		//	+ maximum elevation (volume ceiling collision)
		//  + current ground elevation (ground collision)
		if ((vPosition.y < ground_clearance)) {

			// collision on ground or volume ceiling, reduce velocity on y axis to zero w/o affecting other axis and there is no collision response on ceiling contact. @todo ground collision response.
			destroy();

			return(true);
		}
		else if (vPosition.y > maximum_clearance) {
			
			applyForce(XMVectorSet(0.0f, -_body.thrust.y, 0.0f, 0.0f));
			vPosition.y = SFM::clamp(vPosition.y, ground_clearance, maximum_clearance);
			XMStoreFloat3A(&_body.future_position, XMLoadFloat3A(&vPosition));
			XMStoreFloat3A(&_body.velocity, XMVectorSet(_body.velocity.x, 0.0f, _body.velocity.z, 0.0f));
			_thruster[UP].Off();

			return(true);
		}

		return(false);
	}

	void cYXIGameObject::setParent(cUser* const pParent)
	{
		_parent = pParent;
	}
	bool const cYXIGameObject::isDestroyed() const
	{
		return(_parent->isDestroyed());
	}
	void cYXIGameObject::destroy()
	{
		_parent->destroy();
	}
	void cYXIGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const tD(time_to_float(tDelta));

		if (nullptr == instance || _parent->isDestroyed()) {
			_parent->destroy();
			return;
		}

		_activity_lights.accumulator += tDelta;
		if (_activity_lights.accumulator >= _activity_lights.interval) {
			_activity_lights.accumulator -= _activity_lights.interval;
		}

		float const ground_clearance(SFM::__fma(instance->getModel()._Extents.y, Iso::VOX_STEP, Iso::getRealHeight(float(Iso::MAX_HEIGHT_STEP)))); // clearance simplified to maximum possible height of terrain

		if (!collide(ground_clearance, tDelta)) {
			// Up "auto" thrusting
			if (!_thruster[UP].bOn || _thruster[UP].bAuto) {
				autoThrust(ground_clearance, tDelta);
			}
		}

		{
			for (uint32_t i = 0 ; i < THRUSTER_COUNT ; ++i) {
				// thruster finished?
				if (_thruster[i].bOn || 0.0f != _thruster[i].tOn || 0.0 != _thruster[i].cooldown.count()) {

					_thruster[i].cooldown -= tDelta;

					if (_thruster[i].cooldown.count() <= 0.0) {
						_thruster[i].Off();
					}
					else {
						float const t = time_to_float(_thruster[i].cooldown / _thruster[i].THRUSTER_COOL_DOWN);

						XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster[i].thrust), t);
						_thruster[i].tOn = t;

						XMStoreFloat3A(&_body.thrust, XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.thrust))); // apply linealy decaying thruster to main thrust force tracking for this update
					}
				}
			}
		}
		
		// Angular // Sphere Thruster Engines // 
		{
			XMVECTOR const xmThrust(XMLoadFloat3A(&_body.angular_thrust));

			XMVECTOR const xmForce(XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.angular_force[in]))); // adding external forces in

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(tD), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.angular_force[out], XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(tD)), _body.mass));

			XMVECTOR xmDir(XMVectorSet(instance->getPitch().angle(), instance->getYaw().angle(), instance->getRoll().angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(tD), xmDir);

			XMFLOAT3A vAngles;
			XMStoreFloat3A(&vAngles, xmDir);

			instance->setPitchYawRoll(v2_rotation_t(vAngles.x), v2_rotation_t(vAngles.y), v2_rotation_t(vAngles.z));

			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.angular_force[in], XMVectorZero()); // reset required
		}

		// Linear // Main Thruster //
		{
			// Orient linear thrust to ship orientation always. *do not change*
			quat_t const qOrient(instance->getPitch().angle(), instance->getYaw().angle(), instance->getRoll().angle()); // *bugfix - using quaternion on world transform (no gimbal lock)
			XMVECTOR const xmThrust(v3_rotate(XMLoadFloat3A(&_body.thrust), qOrient));
			
			XMVECTOR const xmForce(XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.force[in]))); // adding external forces in

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(tD), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.force[out], XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(tD)), _body.mass));

			XMVECTOR xmPosition(instance->getLocation());

			xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(tD), xmPosition);

			instance->setLocation(XMLoadFloat3A(&_body.future_position));
			XMStoreFloat3A(&_body.future_position, xmPosition);

			XMStoreFloat3A(&_body.velocity, xmVelocity);
			XMStoreFloat3A(&_body.thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.force[in], XMVectorZero()); // reset required
		}

		/*{
			// current forces, and each thruster and elevation normalized
			float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT, Iso::VOX_STEP, Iso::VOX_STEP * getModelInstance()->getModel()._Extents.y);

			float const elevation = SFM::linearstep(ground_clearance, maximum_clearance, instance->getElevation());

			FMT_NUKLEAR_DEBUG(false, "{:.3f}   main{:.3f}   up{:.3f}  elevation{:.3f}", 
				(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_body.force[out]))) / _body.mass),
				(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_thruster[MAIN].thrust))) / _body.mass),
				(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_thruster[UP].thrust))) / _body.mass),
				elevation
			);
		}*/
	}

	void __vectorcall cYXIGameObject::applyThrust(FXMVECTOR xmThrust)
	{
		if (XMVectorGetZ(xmThrust) > 0.0f) { // main forward thruster
			_thruster[MAIN].On(XMVectorSubtract(xmThrust, XMLoadFloat3A(&_thruster[UP].thrust))); // total thrust available to main thruster is shared with up thruster, with up thruster always taking priority. However the main thruster has a much higher peak thrust.
		}
		else if (XMVectorGetY(xmThrust) > 0.0f) { // up thruster 

			float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT, Iso::VOX_STEP, Iso::VOX_STEP * getModelInstance()->getModel()._Extents.y);
			if (getModelInstance()->getElevation() < maximum_clearance) {
				_thruster[UP].On(xmThrust); 
			}
		}

		XMStoreFloat3A(&_body.thrust, XMVectorAdd(XMLoadFloat3A(&_body.thrust), xmThrust));
	}
	void __vectorcall cYXIGameObject::applyAngularThrust(FXMVECTOR xmThrust)
	{
		XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(XMLoadFloat3A(&_body.angular_thrust), xmThrust));
	}

	void __vectorcall cYXIGameObject::applyForce(FXMVECTOR xmForce)
	{
		XMStoreFloat3A(&_body.force[in], XMVectorAdd(XMLoadFloat3A(&_body.force[in]), xmForce));
	}
	void __vectorcall cYXIGameObject::applyAngularForce(FXMVECTOR xmForce)
	{
		XMStoreFloat3A(&_body.angular_force[in], XMVectorAdd(XMLoadFloat3A(&_body.angular_force[in]), xmForce));
	}
} // end ns world

