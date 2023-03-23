#include "pch.h"
#include "cYXIGameObject.h"
#include "voxelModelInstance.h"
#include <Math/quat_t.h>
#include "cPhysics.h"
#include "cUser.h"

#include "tracy.h"

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
		if (Validate()) {
			Instance->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cYXIGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cYXIGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) {
			_thruster[i] = std::move(_thruster[i]);
		}
		_body = std::move(src._body);
		_activity_lights = std::move(src._activity_lights);
		_parent = std::move(src._parent); src._parent = nullptr;
		_thrusterFire = std::move(src._thrusterFire); src._thrusterFire = nullptr;

	}
	cYXIGameObject& cYXIGameObject::operator=(cYXIGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cYXIGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cYXIGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		for (uint32_t i = 0 ; i < THRUSTER_COUNT ; ++i) {
			_thruster[i] = std::move(_thruster[i]);
		}

		_body = std::move(src._body);
		_activity_lights = std::move(src._activity_lights);
		_parent = std::move(src._parent); src._parent = nullptr;
		_thrusterFire = std::move(src._thrusterFire); src._thrusterFire = nullptr;

		return(*this);
	}

	cYXIGameObject::cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _thruster{ {MAIN_THRUSTER_COOL_DOWN}, {UP_THRUSTER_COOL_DOWN} }, _body{}, _activity_lights{}, _thrusterFire(nullptr),
		_parent(nullptr)
	{
		instance_->setOwnerGameObject<cYXIGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cYXIGameObject::OnVoxel);

		_body.mass = voxels_to_kg((float)getModelInstance()->getVoxelCount());

		float const fElevation(Iso::TERRAIN_MAX_HEIGHT * Iso::VOX_STEP * 0.25f);
		
		instance_->resetElevation(fElevation);
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
				//float const t = 1.0f - SFM::saturate(time_to_float(_activity_lights.accumulator / _activity_lights.interval));
				//uint32_t const luma = SFM::saturate_to_u8(t * 255.0f);
				uint32_t const luma = SFM::saturate_to_u8(255.0f);
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

	void cYXIGameObject::enableThrusterFire(float const power)
	{
		[[unlikely]] if (!Validate())
			return;

		if (nullptr == _thrusterFire) {
			_thrusterFire = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cThrusterFireGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(Instance->getVoxelIndex(),
				Volumetric::eVoxelModel::DYNAMIC::NAMED::MAIN_THRUST, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);

			if (_thrusterFire) {
				_thrusterFire->setParent(this, XMVectorSet(-3.0f, -26.0f, -46.0f * 2.0f, 0.0f));
				_thrusterFire->setCharacteristics(0.5f, 0.5f, 0.01f);
				_thrusterFire->setPitch(v2_rotation_constants::v90);
			}
		}

		if (_thrusterFire) {

			if (_thrusterFire->getIntensity() <= 0.0f) {
				_thrusterFire->resetAnimation();
			}
			_thrusterFire->setIntensity(power);
		}
	}
	void cYXIGameObject::updateThrusterFire(float const power)
	{
		_thrusterFire->setIntensity(power);
	}
	void cYXIGameObject::disableThrusterFire()
	{
		_thrusterFire->setIntensity(0.0f);
	}

	void cYXIGameObject::autoThrust(float const ground_clearance, fp_seconds const tDelta) // automatic thrusting (up thruster only)
	{
		float const tD(time_to_float(tDelta));

		static constexpr float const one_second = time_to_float(duration_cast<fp_seconds>(milliseconds(1000)));

		float const target_elevation(ground_clearance + Iso::VOX_STEP * Instance->getModel()._Extents.y); // additional clearance equal to the extent of the ship on the y axis
		float const current_elevation(Instance->getElevation());

		float const clearance(current_elevation - target_elevation); // distance to target elevation (signed direction aswell)
		// if clearance > 0, above current target elevation
		// if clearance < 0, below current  ""       ""
		//if (clearance > 0.0f)
		//	return; // direction of the change in ship elevation does not require automatic thrusting

		float const velocity(_body.velocity.y - -MIN_UP_THRUST);
		// if velocity > 0, then current velocity is exceeding gravity
		// if velocity < 0, then current   ""     is not exceeding gravity
		//if (velocity < 0.0f)
		//	return; // direction of the current velocity does not require automatic thrusting

		// v = d/t  , t = d/v
		float t = SFM::abs(clearance / velocity);
		// linearize time to 0.0f ... 1.0f range
		t = SFM::linearstep(0.0f, one_second * 1.0f, t);
		
		// up to maximum thrust (up thruster) capability
		float const counter_thrust = _body.mass * SFM::lerp(-cPhysics::GRAVITY, MAX_UP_THRUST, t*t); // exponential curve

		// get future height (output) 
		XMVECTOR const xmForce(XMVectorAdd(XMVectorSet(0.0f, counter_thrust, 0.0f, 0.0f), XMLoadFloat3A(&_body.force[in]))); // adding external forces in

		XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

		XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(one_second), xmInitialVelocity);

		XMVECTOR xmPosition(Instance->getLocation());

		xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(one_second), xmPosition);

		float const future_elevation(XMVectorGetY(xmPosition));

		if (future_elevation < target_elevation || counter_thrust > (_body.mass * MAX_UP_THRUST * 0.99f)) {
				
			float const finalThrust(SFM::min(_body.mass * MAX_UP_THRUST, counter_thrust));

			applyThrust(XMVectorSet(0.0f, finalThrust, 0.0f, 0.0f)); // up thruster - must thrust greater or equal to acceleration of gravity to maintain elevation.
			_thruster[UP].bAuto = true; // flag thruster as automatic
		}
		else {
			_thruster[UP].bAuto = false; // auto-reset to transition to thrust off
		}
	}

	bool const cYXIGameObject::collide(float const ground_clearance, fp_seconds const tDelta)
	{
		float const tD(time_to_float(tDelta));

		float const targetHeight(ground_clearance + Iso::VOX_STEP * Instance->getModel()._Extents.y); // additional clearance equal to the extent of the ship on the y axis

		float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT * 0.75f, Iso::VOX_STEP, Iso::VOX_STEP * Instance->getModel()._Extents.y * 2.0f);

		float const current_elevation(Instance->getElevation());

		float const target_elevation = SFM::linearstep(ground_clearance, maximum_clearance, current_elevation);

		// clamp height to:
		//	+ maximum elevation (volume ceiling collision)
		//  + current ground elevation (ground collision)

		if (_body.velocity.y < 0.0f) {
			if (target_elevation > current_elevation || current_elevation < targetHeight) {

				//applyForce(XMVectorSet(0.0f, -_body.thrust.y, 0.0f, 0.0f));
				XMStoreFloat3A(&_body.velocity, XMVectorSet(_body.velocity.x, -_body.velocity.y, _body.velocity.z, 0.0f));

				//_thruster[UP].On(XMVectorSet(0.0f, MIN_UP_THRUST, 0.0f, 0.0f));

				return(true);
			}
		}
		else if (_body.velocity.y > 0.0f) {
			if (current_elevation >= maximum_clearance) {

				//applyForce(XMVectorSet(0.0f, -_body.thrust.y, 0.0f, 0.0f));
				XMStoreFloat3A(&_body.velocity, XMVectorSet(_body.velocity.x, -_body.velocity.y, _body.velocity.z, 0.0f));

				_thruster[UP].Off();

				return(true);
			}
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
		ZoneScoped;

		[[unlikely]] if (!Validate()) {

			if (nullptr != _parent) {
				_parent->destroy();
			}
			return;
		}
		[[unlikely]] if (_parent->isDestroyed()) {
			Instance->destroy(milliseconds(0));
			return;
		}

		float const tD(time_to_float(tDelta));

		constinit static float sum{}, count{};

		sum += _body.velocity.y;
		++count;

		_activity_lights.accumulator += tDelta;
		if (_activity_lights.accumulator >= _activity_lights.interval) {
			_activity_lights.accumulator -= _activity_lights.interval;

			sum = sum / (count + 1.0f);
			count = 0;
		}

		if (sum < 1.0f) { // allow room in positive region to reduce flicker or on/off rapid switching
			_activity_lights.accumulator = zero_time_duration; // boost lights
		}

		uint32_t integrations(0);
		rect2D_t const rectInstance(r2D_add(Instance->getModel()._LocalArea, Instance->getVoxelIndex()));
		float const current_ground_clearance(Iso::VOX_SIZE * Iso::getRealHeight(float(world::getVoxelsAt_MaximumHeight(rectInstance, Instance->getYaw())))); // clearance simplified to maximum possible height of terrain

		bool const bCollision = collide(current_ground_clearance, tDelta);

		if (!bCollision) {

			static constexpr float const one_second = time_to_float(duration_cast<fp_seconds>(milliseconds(1000)));
			static constexpr float const one_second_part = one_second / 3.0f;

			// future ground clearance integration testing
			float maximum_future_ground_clearance(0.0f);
			for (float fwd = one_second_part; fwd < one_second; fwd += one_second_part) {

				XMVECTOR const xmFutureLocation(SFM::__fma(XMLoadFloat3A(&_body.velocity), XMVectorReplicate(fwd), Instance->getLocation()));
				rect2D_t const rectFutureInstance(r2D_add(Instance->getModel()._LocalArea, v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmFutureLocation))));

				float const future_ground_clearance(Iso::VOX_SIZE * Iso::getRealHeight(float(world::getVoxelsAt_MaximumHeight(rectFutureInstance, Instance->getYaw()))));

				maximum_future_ground_clearance = SFM::max(maximum_future_ground_clearance, future_ground_clearance);
				++integrations;
			}

			bool const bAuto(_body.velocity.y < 0.0f); // auto thrusting enable condition

			// Up "auto" thrusting
			if ((_thruster[UP].bAuto || maximum_future_ground_clearance > current_ground_clearance) && !_thruster[UP].bOn && bAuto) { // not manually on & (auto) velocity is in condition to engage auto thrusting
				// alerady on or the future projected ground clearance is greater than the current ground clearance
				autoThrust(SFM::max(current_ground_clearance, maximum_future_ground_clearance), tDelta);
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

			if (_thrusterFire) {
				updateThrusterFire(_thruster[MAIN].tOn);
			}
		}
		
		// Angular // Sphere Thruster Engines // 
		XMFLOAT3A vAngles;
		{
			XMVECTOR const xmThrust(XMLoadFloat3A(&_body.angular_thrust));

			XMVECTOR const xmForce(XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.angular_force[in]))); // adding external forces in

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(tD), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.angular_force[out], XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(tD)), _body.mass));

			XMVECTOR xmDir(XMVectorSet(Instance->getPitch().angle(), Instance->getYaw().angle(), Instance->getRoll().angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(tD), xmDir);

			XMStoreFloat3A(&vAngles, xmDir);

			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.angular_force[in], XMVectorZero()); // reset required
		}

		// Linear // Main Thruster //
		XMVECTOR xmPosition(Instance->getLocation());
		{
			// Orient linear thrust to ship orientation always. *do not change*
			quat_t const qOrient(vAngles.x, vAngles.y, vAngles.z); // *bugfix - using quaternion on world transform (no gimbal lock)
			XMVECTOR const xmThrust(v3_rotate(XMLoadFloat3A(&_body.thrust), qOrient));
			
			XMVECTOR const xmForce(XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.force[in]))); // adding external forces in

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

			XMVECTOR xmVelocity = SFM::__fma(XMVectorDivide(xmForce, XMVectorReplicate(_body.mass)), XMVectorReplicate(tD), xmInitialVelocity);

			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.force[out], XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(tD)), _body.mass));

			xmPosition = SFM::__fma(xmVelocity, XMVectorReplicate(tD), xmPosition);

			XMStoreFloat3A(&_body.velocity, xmVelocity);
			XMStoreFloat3A(&_body.thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.force[in], XMVectorZero()); // reset required
		}

		Instance->setTransform(xmPosition, v2_rotation_t(vAngles.x), v2_rotation_t(vAngles.y), v2_rotation_t(vAngles.z));
		   
		/*{
			// current forces, and each thruster and elevation normalized
			float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT * 0.75f, Iso::VOX_STEP, Iso::VOX_STEP * getModelInstance()->getModel()._Extents.y * 2.0f);

			float const elevation = SFM::linearstep(current_ground_clearance, maximum_clearance, Instance->getElevation());

			FMT_NUKLEAR_DEBUG(false, "velocity{:.3f}   main{:.3f}   up{:.3f}  elevation{:.3f} elevation{:.6f} {:d}times", 
				_body.velocity.y,
				(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_thruster[MAIN].thrust))) / _body.mass),
				(_thruster[UP].thrust.y / _body.mass),
				elevation, Instance->getElevation(), integrations
			);
		}*/
	}

	void __vectorcall cYXIGameObject::applyThrust(FXMVECTOR xmThrust)
	{
		[[unlikely]] if (!Validate())
			return;

		if (XMVectorGetZ(xmThrust) > 0.0f) { // main forward thruster
			_thruster[MAIN].On(XMVectorSubtract(xmThrust, XMLoadFloat3A(&_thruster[UP].thrust))); // total thrust available to main thruster is shared with up thruster, with up thruster always taking priority. However the main thruster has a much higher peak thrust.
			enableThrusterFire(_thruster[MAIN].tOn);
		}
		else if (XMVectorGetY(xmThrust) > 0.0f) { // up thruster 

			float const maximum_clearance = SFM::__fms(Iso::WORLD_MAX_HEIGHT * 0.75f, Iso::VOX_STEP, Iso::VOX_STEP * getModelInstance()->getModel()._Extents.y * 2.0f);
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

