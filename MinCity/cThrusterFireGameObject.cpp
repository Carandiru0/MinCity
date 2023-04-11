#include "pch.h"
#include "cThrusterFireGameObject.h"
#include "voxelModelInstance.h"
#include "MinCity.h"
#include "cPhysics.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cThrusterFireGameObject::remove(static_cast<cThrusterFireGameObject const* const>(_this));
		}
	}

	cThrusterFireGameObject::cThrusterFireGameObject(cThrusterFireGameObject&& src) noexcept
		: cAttachableGameObject(std::forward<cAttachableGameObject&&>(src)), _animation(std::move(src._animation))
	{
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cThrusterFireGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cThrusterFireGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cThrusterFireGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_intensity = std::move(src._intensity);
		_characteristics = std::move(src._characteristics);
	}
	cThrusterFireGameObject& cThrusterFireGameObject::operator=(cThrusterFireGameObject&& src) noexcept
	{
		cAttachableGameObject::operator=(std::forward<cAttachableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cThrusterFireGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cThrusterFireGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cThrusterFireGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_animation = std::move(src._animation);
		_intensity = std::move(src._intensity);
		_characteristics = std::move(src._characteristics);

		return(*this);
	}

	cThrusterFireGameObject::cThrusterFireGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: cAttachableGameObject(instance_), _animation(instance_), _characteristics{}, _intensity(0.0f)
	{
		instance_->setOwnerGameObject<cThrusterFireGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cThrusterFireGameObject::OnVoxel);

		resetAnimation();
	}

	void cThrusterFireGameObject::setCharacteristics(float const temperature_boost_, float const flame_boost_, float const emission_threshold_)
	{
		_characteristics.temperature_boost = temperature_boost_;
		_characteristics.flame_boost = flame_boost_;
		_characteristics.emission_threshold = emission_threshold_;
	}

	static XMVECTOR const __vectorcall blackbody_space(float const t)
	{
		static constexpr float const NORMALIZE = 1.0f / float(UINT8_MAX);

		XMVECTOR xmColor(XMVectorZero());

		uvec4_v const vColor(MinCity::VoxelWorld->blackbody(t)); // convert temperature to color
		uvec4_t rgba[2];
		float const luma(SFM::luminance(vColor));

		SFM::unpack_rgba(cThrusterFireGameObject::COLOR_THRUSTER_GRADIENT_MIN, rgba[0]);
		SFM::unpack_rgba(cThrusterFireGameObject::COLOR_THRUSTER_GRADIENT_MAX, rgba[1]);
		xmColor = SFM::lerp(uvec4_v(rgba[0]).v4f(), uvec4_v(rgba[1]).v4f(), luma); // no need to normalize, scale by luma, then denormalize as they all multiply together and the two cancel out (so it remains in [0-255] range
		xmColor = XMVectorScale(xmColor, luma * NORMALIZE);

		return(xmColor);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cThrusterFireGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cThrusterFireGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cThrusterFireGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		static constexpr float const NORMALIZE = 1.0f / float(UINT8_MAX);
		static constexpr float const DENORMALIZE = float(UINT8_MAX);

		// density - temperature - flames 
		uint32_t const udensity(voxel.Color & 0xff);
		uint32_t const utemperature((voxel.Color >> 8) & 0xff);
		uint32_t const uflames((voxel.Color >> 16) & 0xff);

		XMVECTOR xmColor(XMVectorZero()); // default color is always initialized to "background" (pure black)

		voxel.Hidden = true;
		voxel.Transparent = true;

		if (_intensity <= 0.0f) {
			return(voxel);
		}

		float density(0.0f);
		if (udensity) {
			density = (float)udensity * NORMALIZE;
			xmColor = XMVectorReplicate(density);
			voxel.Hidden = false;
		}

		if (utemperature) { // temperature present?

			float const temperature((float)utemperature * NORMALIZE);

			xmColor = XMVectorAdd(xmColor, blackbody_space(temperature + _characteristics.temperature_boost));

			voxel.Emissive = true;
			voxel.Hidden = false;
		}
		if (uflames) { // flames present?

			float const flames = ((float)uflames * NORMALIZE);

			xmColor = XMVectorMultiply(xmColor, blackbody_space(flames + _characteristics.flame_boost));

			voxel.Emissive = true;
			voxel.Hidden = false;
		}

		if (!voxel.Hidden) {

			if (voxel.Emissive) {
				voxel.Emissive = SFM::XMColorRGBToLuminance(xmColor) > _characteristics.emission_threshold;
			}
		}

		// area's that are less dense are brighter than denser areas - creates dark "spots" where density is high
		xmColor = XMVectorScale(xmColor, (1.0f - density) * _intensity); // also apply current intensity/power

		xmColor = SFM::saturate(xmColor);

		xmColor = XMVectorScale(xmColor, DENORMALIZE);
		uvec4_t rgba;
		uvec4_v(xmColor).rgba(rgba);

		voxel.Color = SFM::pack_rgba(rgba); // uses the gradient color times the luminance of the blackbody linear color for the main thruster temperature

		// (mass of voxel is 1.0) f = ma   So FORCE = ACCELERATION
		// therefore this is setup to be in voxels/second squared [acceleration]
		// obey cPhysics:MIN_FORCE and cPhysics:MAX_FORCE limits or else the explosion will explode (as suggested by github pilot)
		MinCity::Physics->add_force(xmIndex);

		return(voxel);
	}

	void cThrusterFireGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		cAttachableGameObject::OnUpdate(tNow, tDelta);

		[[unlikely]] if (!Validate())
			return;

		if (_animation.update(*Instance, tDelta)) {
			_animation.setRepeatFrameIndex(20);
		}

		Instance->setTransparentCount(Instance->getCount()); // all transparent

		//setYaw(getYaw() + time_to_float(tDelta) * 10.0f);
	}

	void cThrusterFireGameObject::resetAnimation()
	{
		_animation.reset();
		_animation.setReverse(true); // all thrusters are instant-on and then theres a cooldown to off. vdb animation needs to be reversed.
	}
} // end ns world

