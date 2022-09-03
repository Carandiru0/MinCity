#include "pch.h"
#include "cYXISphereGameObject.h"
#include "voxelModelInstance.h"
#include "cYXIGameObject.h"

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
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cYXISphereGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
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
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cYXISphereGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_parent = std::move(src._parent); src._parent = nullptr;
		_offset = std::move(src._offset);
		_thrusters = std::move(src._thrusters);
		for (uint32_t i = 0; i < THRUSTER_COUNT; ++i) {
			_thruster[i] = std::move(src._thruster[i]);
		}
		_body = std::move(src._body);

		return(*this);
	}

	cYXISphereGameObject::cYXISphereGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _parent(nullptr), _offset{}, _thrusters{}, _thruster{}, _body{}
	{
		instance_->setOwnerGameObject<cYXISphereGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cYXISphereGameObject::OnVoxel);

		_body.mass = (float)getModelInstance()->getVoxelCount();
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
			uvec4_v(xmColor).rgba(rgba[0]);

			voxel.Color = SFM::pack_rgba(rgba[0]); // uses the gradient color times the luminance of the blackbody linear color for the main thruster temperature
		}
		return(voxel);
	}

	void cYXISphereGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const t(time_to_float(tDelta));

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

							XMStoreFloat3(&_body.angular_thrust, xmThrust); // apply linealy decaying thrust to main thruster force tracking for this update
						}
					}
					else {
						// auto level this axis
						static constexpr float const EPSILON = Iso::MINI_VOX_SIZE * 0.5f;

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
							autolevelContinue = (vParentAngularVelocity.x - EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getRoll().angle();
							break;
						case Y_MAX:
						case Y_MIN:
							autolevelContinue = (vParentAngularVelocity.y - EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getYaw().angle();
							break;
						case Z_MAX:
						case Z_MIN:
							autolevelContinue = (vParentAngularVelocity.z - EPSILON) >= 0.0f;
							angle = _parent->getModelInstance()->getPitch().angle();
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
							t = SFM::saturate(t);

							XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster[i].thrust), t);
							_thruster[i].tOn = t;

							XMStoreFloat3(&_body.angular_thrust, xmThrust); // keep applying auto-leveling thrust per frame until the condition is satisfied
						}
						_thruster[i].autoleveling = autolevelContinue; // when condition is satisfied continue with the natural cooldown turn off process (when false - turns off auto leveling)
					}
				}
			}
		}

		// Angular //
		{
			XMVECTOR const xmThrust(XMVectorScale(XMLoadFloat3A(&_body.angular_thrust), _body.mass));

			XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.angular_velocity));

			XMVECTOR const xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

			// save the angular force of the sphere for that force acts upon it's parent
			// finding the force              v - vi
			//                     f   = m * --------		(herbie optimized)
			//									dt
			XMStoreFloat3A(&_body.angular_force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));

			XMVECTOR xmDir(XMVectorSet(instance->getRoll().angle(), instance->getYaw().angle(), instance->getPitch().angle(), 0.0f));

			xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmDir);

			XMFLOAT3A vAngles;
			XMStoreFloat3A(&vAngles, xmDir);

			instance->setRoll(v2_rotation_t(vAngles.x));
			instance->setYaw(v2_rotation_t(vAngles.y));
			instance->setPitch(v2_rotation_t(vAngles.z));

			XMStoreFloat3A(&_body.angular_thrust, XMVectorZero()); // reset required
			XMStoreFloat3A(&_body.angular_velocity, xmVelocity);
		}

		// inherit  
		{ // parent translation
			auto const parentInstance(_parent->getModelInstance());
			v2_rotation_t const& vParentRoll(parentInstance->getRoll()), vParentYaw(parentInstance->getYaw()), vParentPitch(parentInstance->getPitch());

			XMVECTOR const xmParentLocation(parentInstance->getLocation3D());
			XMVECTOR xmSphere(XMVectorAdd(xmParentLocation, XMVectorScale(XMLoadFloat3A(&_offset), Iso::MINI_VOX_STEP)));

			xmSphere = v3_rotate_roll(v3_rotate_yaw(v3_rotate_pitch(xmSphere, xmParentLocation, vParentPitch), xmParentLocation, vParentYaw), xmParentLocation, vParentRoll);

			instance->setLocation3D(xmSphere);
		}
	}

	void __vectorcall cYXISphereGameObject::startThruster(FXMVECTOR xmThrust, bool const auto_leveling_enable)
	{
		XMFLOAT3A vThruster;
		XMStoreFloat3A(&vThruster, xmThrust);

		// instant start thruster, with linear cool-down after last instaneous thrust
		if (0.0f != vThruster.x) {
			if (vThruster.x > 0.0f) {
				_thrusters |= X_MAX_BIT;
				_thruster[5].On(xmThrust, auto_leveling_enable);
			}
			else {
				_thrusters |= X_MIN_BIT;
				_thruster[4].On(xmThrust, auto_leveling_enable);
			}
		}
		if (0.0f != vThruster.y) {
			if (vThruster.y > 0.0f) {
				_thrusters |= Y_MAX_BIT;
				_thruster[3].On(xmThrust, auto_leveling_enable);
			}
			else {
				_thrusters |= Y_MIN_BIT;
				_thruster[2].On(xmThrust, auto_leveling_enable);
			}
		}
		if (0.0f != vThruster.z) {
			if (vThruster.z > 0.0f) {
				_thrusters |= Z_MAX_BIT;
				_thruster[1].On(xmThrust, auto_leveling_enable);
			}
			else {
				_thrusters |= Z_MIN_BIT;
				_thruster[0].On(xmThrust, auto_leveling_enable);
			}
		}
	}

	XMVECTOR const __vectorcall cYXISphereGameObject::applyAngularThrust(FXMVECTOR xmThrust, bool const auto_leveling_enable)
	{
		XMStoreFloat3A(&_body.angular_thrust, XMVectorAdd(XMLoadFloat3A(&_body.angular_thrust), xmThrust));

		startThruster(xmThrust, auto_leveling_enable);

		return(XMLoadFloat3(&_body.angular_force)); // the force applied from the angular force in the sphere is returned here to the parent, so that the current accumulated forces are applied to parent.
	}
} // end ns world

