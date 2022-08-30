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

		uvec4_t rgba;

		switch (voxel.Color) {

		case MASK_X_MAX:
			voxel.Emissive = bool(_thrusters & X_MAX_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[5].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_X_MIN:
			voxel.Emissive = bool(_thrusters & X_MIN_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[4].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Y_MAX:
			voxel.Emissive = bool(_thrusters & Y_MAX_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[3].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Y_MIN:
			voxel.Emissive = bool(_thrusters & Y_MIN_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[2].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Z_MAX:
			voxel.Emissive = bool(_thrusters & Z_MAX_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[1].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		case MASK_Z_MIN:
			voxel.Emissive = bool(_thrusters & Z_MIN_BIT);
			if (voxel.Emissive) {
				MinCity::VoxelWorld->blackbody(_thruster[0].tOn).rgba(rgba); // linear thruster to temperature
				voxel.Color = SFM::pack_rgba(rgba);
			}
			else {
				voxel.Color = 0;
			}
			break;
		}

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

	void cYXISphereGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		auto instance(*Instance);
		float const t(time_to_float(tDelta));

		{
			// existing thruster(s) finished?
			if (_thrusters) {

				for (uint32_t i = 0; i < 6; ++i) { // for each thruster
					_thruster[i].cooldown -= tDelta;

					if (_thruster[i].cooldown.count() <= 0.0) {
						_thruster[i].Off();
						_thrusters &= ~(1 << i); // reset individual thruster in mask
					}
					else {
						float const t = time_to_float(_thruster[i].cooldown / THRUSTER_COOL_DOWN);

						XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&_thruster[i].thrust), t);
						_thruster[i].tOn = t;

						XMStoreFloat3A(&_body.thrust, XMVectorAdd(XMLoadFloat3A(&_body.thrust), xmThrust)); // apply linealy decaying thruster to main thrust force tracking for this update
					}
				}
			}
		}

		XMVECTOR const xmThrust(XMVectorScale(XMLoadFloat3A(&_body.thrust), _body.mass));

		XMVECTOR const xmInitialVelocity(XMLoadFloat3A(&_body.velocity));

		XMVECTOR xmVelocity = SFM::__fma(XMVectorDivide(xmThrust, XMVectorReplicate(_body.mass)), XMVectorReplicate(t), xmInitialVelocity);

		v2_rotation_t const& xmRoll(instance->getRoll()), xmYaw(instance->getYaw()), xmPitch(instance->getPitch());
		XMVECTOR xmDir(XMVectorSet(xmRoll.angle(), xmYaw.angle(), xmPitch.angle(), 0.0f)); 
		
		xmDir = SFM::__fma(xmVelocity, XMVectorReplicate(t), xmDir);

		// save the angular force of the sphere for that force acts upon it's parent
		// finding the force              v - vi
		//                     f   = m * --------		(herbie optimized)
		//									dt
		XMStoreFloat3A(&_body.force, XMVectorScale(XMVectorDivide(XMVectorSubtract(xmVelocity, xmInitialVelocity), XMVectorReplicate(t)), _body.mass));
		
		XMFLOAT3A vAngles;
		XMStoreFloat3A(&vAngles, xmDir);
		
		instance->setRoll(v2_rotation_t(vAngles.x));
		instance->setYaw(v2_rotation_t(vAngles.y));
		instance->setPitch(v2_rotation_t(vAngles.z));

		XMStoreFloat3A(&_body.thrust, XMVectorZero()); // reset required
		XMStoreFloat3A(&_body.velocity, xmVelocity);

		// set sphere elevation to follow the same elevation
		instance->setElevation(_parent->getModelInstance()->getElevation());

		// inherit translation & rotation
		XMVECTOR const xmParent(_parent->getModelInstance()->getLocation3D());
		XMVECTOR xmSphere(XMVectorAdd(xmParent, XMVectorScale(XMLoadFloat3A(&_offset), Iso::MINI_VOX_STEP)));

		xmSphere = v3_rotate_roll(v3_rotate_yaw(v3_rotate_pitch(xmSphere, xmParent, _parent->getModelInstance()->getPitch()), xmParent, _parent->getModelInstance()->getYaw()), xmParent, _parent->getModelInstance()->getRoll());

		instance->setLocation3D(xmSphere);
	}

	XMVECTOR const __vectorcall cYXISphereGameObject::applyThrust(FXMVECTOR xmThrust)
	{
		XMStoreFloat3A(&_body.thrust, XMVectorAdd(XMLoadFloat3A(&_body.thrust), xmThrust));

		XMVECTOR const xmThruster = XMVector3Cross(xmThrust, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		XMFLOAT3A vThruster;
		XMStoreFloat3A(&vThruster, xmThruster);


		// instant start thruster, with linear cool-down after last instaneous thrust
		if (0.0f != vThruster.x) {
			if (vThruster.x > 0.0f) {
				_thrusters |= X_MAX_BIT;
				_thruster[5].On(xmThrust);
			}
			else {
				_thrusters |= X_MIN_BIT;
				_thruster[4].On(xmThrust);
			}
		}
		if (0.0f != vThruster.y) {
			if (vThruster.y > 0.0f) {
				_thrusters |= Y_MAX_BIT;
				_thruster[3].On(xmThrust);
			}
			else {
				_thrusters |= Y_MIN_BIT;
				_thruster[2].On(xmThrust);
			}
		}
		if (0.0f != vThruster.z) {
			if (vThruster.z > 0.0f) {
				_thrusters |= Z_MAX_BIT;
				_thruster[1].On(xmThrust);
			}
			else {
				_thrusters |= Z_MIN_BIT;
				_thruster[0].On(xmThrust);
			}
		}

		return(XMLoadFloat3A(&_body.force)); // the force applied from the angular force in the sphere is returned here to the parent, so that the current accumulated forces are applied to parent.
	}
} // end ns world

