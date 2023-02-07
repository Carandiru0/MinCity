#include "pch.h"
#include "cYXISphereGameObject.h"
#include "voxelModelInstance.h"
#include "cYXIGameObject.h"
#include <Math/quat_t.h>
#include "cPhysics.h"
#include "MinCity.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cYXISphereGameObject::remove(static_cast<cYXISphereGameObject const* const>(_this));
		}
	}

	cYXISphereGameObject::cYXISphereGameObject(cYXISphereGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cYXISphereGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
		_thrusterFire = std::move(src._thrusterFire); src._thrusterFire = nullptr;
		_offset = std::move(src._offset);
		_thrusters = std::move(src._thrusters);
		for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) {
			_thruster[i] = std::move(src._thruster[i]);
		}
		_body = std::move(src._body);
	}
	cYXISphereGameObject& cYXISphereGameObject::operator=(cYXISphereGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cYXISphereGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
		_thrusterFire = std::move(src._thrusterFire); src._thrusterFire = nullptr;
		_offset = std::move(src._offset);
		_thrusters = std::move(src._thrusters);
		for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) {
			_thruster[i] = std::move(src._thruster[i]);
		}
		_body = std::move(src._body);

		return(*this);
	}

	cYXISphereGameObject::cYXISphereGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _parent(nullptr), _thrusterFire(nullptr), _offset{}, _thrusters{}, _thruster{}, _body{}
	{
		instance_->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);

		_body.mass = voxels_to_kg((float)getModelInstance()->getVoxelCount());
	}

	void cYXISphereGameObject::enableThrusterFire(float const power)
	{
		[[unlikely]] if (!Validate())
			return;
		[[unlikely]] if (!_parent->Validate()) {
			Instance->destroy(milliseconds(0));
			return;
		}

		if (nullptr == _thrusterFire) {
			_thrusterFire = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cThrusterFireGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(Instance->getVoxelIndex(),
				Volumetric::eVoxelModel::DYNAMIC::NAMED::UP_THRUST, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);

			if (_thrusterFire) {
				_thrusterFire->setParent(this, XMVectorSet(0.0f, -12.0f, 0.0f, 0.0f));
				_thrusterFire->setCharacteristics(0.5f, 0.5f, 0.01f);
			}
		}

		if (_thrusterFire->getIntensity() <= 0.0f) {
			_thrusterFire->resetAnimation();
		}
		_thrusterFire->setIntensity(power);
	}
	void cYXISphereGameObject::updateThrusterFire(float const power)
	{
		_thrusterFire->setIntensity(power);
	}
	void cYXISphereGameObject::disableThrusterFire()
	{
		_thrusterFire->setIntensity(0.0f);
	}
	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cYXISphereGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cYXISphereGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cYXISphereGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		int32_t iThruster(-1);

		switch (voxel.Color) {

		case MASK_X_MAX:
			voxel.Emissive = bool(_thrusters & X_MAX_BIT);
			if (voxel.Emissive) {
				iThruster = 5;
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_X_MIN:
			voxel.Emissive = bool(_thrusters & X_MIN_BIT);
			if (voxel.Emissive) {
				iThruster = 4;
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Y_MAX:
			voxel.Emissive = bool(_thrusters & Y_MAX_BIT);
			if (voxel.Emissive) {
				iThruster = 3;
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Y_MIN:
			voxel.Emissive = bool(_thrusters & Y_MIN_BIT);
			if (voxel.Emissive) {
				iThruster = 2;
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Z_MAX:
			voxel.Emissive = bool(_thrusters & Z_MAX_BIT);
			if (voxel.Emissive) {
				iThruster = 1;
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Z_MIN:
			voxel.Emissive = bool(_thrusters & Z_MIN_BIT);
			if (voxel.Emissive) {
				iThruster = 0;
			}
			else {
				voxel.Color = 0;
			}
			break;
		}

		if (iThruster >= 0) {

			XMVECTOR xmColor;
			uvec4_v vColor;
			uvec4_t rgba[2];
			float luma(0.0f);

			vColor = MinCity::VoxelWorld->blackbody(_thruster[iThruster].tOn); // linear thruster to temperature
			luma = SFM::luminance(vColor);

			SFM::unpack_rgba(COLOR_THRUSTER_GRADIENT_MIN, rgba[0]);
			SFM::unpack_rgba(COLOR_THRUSTER_GRADIENT_MAX, rgba[1]);
			xmColor = SFM::lerp(uvec4_v(rgba[0]).v4f(), uvec4_v(rgba[1]).v4f(), luma); // no need to normalize, scale by luma, then denormalize as they all multiply together and the two cancel out (so it remains in [0-255] range
			xmColor = XMVectorScale(xmColor, luma);
			uvec4_v(xmColor).rgba(rgba[0]);

			voxel.Color = SFM::pack_rgba(rgba[0]); // uses the gradient color times the luminance of the blackbody linear color for the main thruster temperature
		}
		return(voxel);
	}

	void cYXISphereGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;
		[[unlikely]] if (!_parent->Validate() || _parent->isDestroyed()) {
			Instance->destroy(milliseconds(0));
			return;
		}

		float const tD(time_to_float(tDelta));

		auto const parentInstance(_parent->getModelInstance());

		{
			// existing thruster(s) finished?
			if (_thrusters) {

				for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) { // for each thruster

					if (!_thruster[i].autoleveling) {
						_thruster[i].cooldown -= tDelta;

						if (_thruster[i].cooldown.count() <= 0.0) {
							_thruster[i].Off();
							_thrusters &= ~(1 << i); // reset individual thruster in mask
						}
						else {
							float const t = time_to_float(_thruster[i].cooldown / THRUSTER_COOL_DOWN);

							XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster[i].thrust), t);
							_thruster[i].tOn = t;

							XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.angular_thrust))); // apply linealy decaying thrust to thruster force tracking for this update
						}
					}
					else {
						// auto level this axis
						static constexpr uint32_t const
							X_MAX = 5,
							X_MIN = 4,
							Y_MAX = 3,
							Y_MIN = 2,
							Z_MAX = 1,
							Z_MIN = 0;

						XMFLOAT3A vParentAngularVelocity;
						XMStoreFloat3A(&vParentAngularVelocity, XMVectorAbs(_parent->getAngularVelocity()));

						float angle(0.0f);
						bool autolevelContinue(false);

						switch (i)
						{
						case X_MAX:
						case X_MIN:
							autolevelContinue = (vParentAngularVelocity.x - cPhysics::VELOCITY_EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getPitch().angle();
							break;
						case Y_MAX:
						case Y_MIN:
							autolevelContinue = (vParentAngularVelocity.y - cPhysics::VELOCITY_EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getYaw().angle();
							break;
						case Z_MAX:
						case Z_MIN:
							autolevelContinue = (vParentAngularVelocity.z - cPhysics::VELOCITY_EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getRoll().angle();
							break;
						}

						if (autolevelContinue) {

							// range is -XM_PI to XM_PI to [-1.0f ... 1.0f]
							float t(angle / XM_PI); 
							// need distance from center as 0.0f and 1.0f are the angles -180 and 180 (which are the same)
							// range is 0 to XM_2PI using [0.0f ... 1.0f]
							t = t * 0.5f + 0.5f;
							// distance from 0.5f to [-1.0f ... 1.0f]
							t = SFM::abs(2.0f * (t - 0.5f)); // abs
							t = 1.0f - SFM::saturate(t);

							XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster[i].thrust), t);
							_thruster[i].tOn = t;

							XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(xmThrust, XMLoadFloat3A(&_body.angular_thrust))); // keep applying auto-leveling thrust per frame until the condition is satisfied
						}
						else {

							// transition to non-auto levelling state smoothly
							_thruster[i].CounterOff();
						}

						_thruster[i].autoleveling = autolevelContinue; // when condition is satisfied continue with the natural cooldown turn off process (when false - turns off auto leveling)
					}
				}
			}
		}

		// Angular //
		XMFLOAT3A vAngles;
		{
			// radius (offset from origin to edge is where force is applied, affecting the force directly, greater radius means greater force) 
			// T = F x r (Torque) [not using cross product, already done, just scale]
			float const displacement(voxels_to_meters(Instance->getModel()._Radius) * cPhysics::TORQUE_OFFSET_SCALAR);
			
			XMVECTOR const xmThrust(XMVectorScale(XMLoadFloat3A(&_body.angular_thrust), _body.mass * displacement)); // T = F * r , T = Torque, F = Force, r = Radius/Offset

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(tD), xmInitialVelocity);

			// save the angular force of the sphere for that force acts upon it's parent
			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//								    dt
			XMStoreFloat3A(&_body.angular_force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(tD)), _body.mass));

			XMVECTOR xmDir(XMVectorSet(Instance->getPitch().angle(), Instance->getYaw().angle(), Instance->getRoll().angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(tD), xmDir);

			XMStoreFloat3A(&vAngles, xmDir);

			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
		}

		// inherit  
		XMVECTOR xmPosition(Instance->getLocation());
		{ // parent translation (which includes all forces that are applied to parent) (rotation is not inherited - these are "gyro" sphere engines)
			
			quat_t const qOrient(parentInstance->getPitch().angle(), parentInstance->getYaw().angle(), parentInstance->getRoll().angle()); // *bugfix - using quaternion on world transform (no gimbal lock)

			XMVECTOR const xmParentLocation(parentInstance->getLocation());
			XMVECTOR const xmSphere(XMVectorAdd(xmParentLocation, XMVectorScale(XMLoadFloat3A(&_offset), Iso::MINI_VOX_STEP)));

			xmPosition = v3_rotate(xmSphere, xmParentLocation, qOrient);
		}

		Instance->setTransform(xmPosition, v2_rotation_t(vAngles.x), v2_rotation_t(vAngles.y), v2_rotation_t(vAngles.z));
	}

	void __vectorcall cYXISphereGameObject::startThruster(FXMVECTOR xmThrust, bool const auto_leveling_enable)
	{
		bool bCancelled(false);

		XMFLOAT3A vParentAngularVelocity, vThruster;
		XMStoreFloat3A(&vParentAngularVelocity, _parent->getAngularVelocity());
		XMStoreFloat3A(&vThruster, xmThrust);

		// instant start thruster, with linear cool-down after last instaneous thrust
		if (0.0f != vThruster.x) {
			if (vThruster.x > 0.0f) {

				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.x > 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[4].CounterOff();
					_thruster[5].CounterOff(); // turn off any currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= X_MAX_BIT;
					_thruster[5].On(xmThrust, auto_leveling_enable);
				}
			}
			else {
				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.x < 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[4].CounterOff();
					_thruster[5].CounterOff(); // turn off any currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= X_MIN_BIT;
					_thruster[4].On(xmThrust, auto_leveling_enable);
				}
			}
		}
		if (0.0f != vThruster.y) {

			if (vThruster.y > 0.0f) {

				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.y > 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[2].CounterOff();
					_thruster[3].CounterOff(); // turn off if currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= Y_MAX_BIT;
					_thruster[3].On(xmThrust, auto_leveling_enable);
				}
			}
			else {
				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.y < 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[2].CounterOff();
					_thruster[3].CounterOff(); // turn off if currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= Y_MIN_BIT;
					_thruster[2].On(xmThrust, auto_leveling_enable);
				}
			}
		}
		if (0.0f != vThruster.z) {
			
			if (vThruster.z > 0.0f) {

				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.z > 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[0].CounterOff();
					_thruster[1].CounterOff(); // turn off any currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= Z_MAX_BIT;
					_thruster[1].On(xmThrust, auto_leveling_enable);
				}
			}
			else {
				if (auto_leveling_enable) {
					bCancelled = vParentAngularVelocity.z < 0.0f; // same direction for counter-thrust ?
				}
				else { // user-controlled thrust
					_thruster[0].CounterOff();
					_thruster[1].CounterOff(); // turn off any currently countering thrust (same axis)
				}

				if (!bCancelled) {
					_thrusters |= Z_MIN_BIT;
					_thruster[0].On(xmThrust, auto_leveling_enable);
				}
			}
		}
	}

	XMVECTOR const __vectorcall cYXISphereGameObject::applyAngularThrust(FXMVECTOR xmThrust, bool const auto_leveling_enable)
	{
		XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(XMLoadFloat3A(&_body.angular_thrust), xmThrust));

		startThruster(xmThrust, auto_leveling_enable);

		return(XMLoadFloat3A(&_body.angular_force)); // the force applied from the angular force in the sphere is returned here to the parent, so that the current accumulated forces are applied to parent.
	}
} // end ns world

