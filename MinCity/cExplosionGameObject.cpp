#include "pch.h"
#include "cExplosionGameObject.h"
#include "voxelModelInstance.h"
#include "MinCity.h"
#include "cPhysics.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cExplosionGameObject::remove(static_cast<cExplosionGameObject const* const>(_this));
		}
	}

	cExplosionGameObject::cExplosionGameObject(cExplosionGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src)), _animation(std::move(src._animation))
	{
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cExplosionGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_temperatureBoost = std::move(src._temperatureBoost);
		_flameBoost = std::move(src._flameBoost);
		_emission_threshold[0] = std::move(src._emission_threshold[0]);
		_emission_threshold[1] = std::move(src._emission_threshold[1]);
		_emission_samples = std::move(src._emission_samples);
	}
	cExplosionGameObject& cExplosionGameObject::operator=(cExplosionGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cExplosionGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_animation = std::move(src._animation);
		_temperatureBoost = std::move(src._temperatureBoost);
		_flameBoost = std::move(src._flameBoost);
		_emission_threshold[0] = std::move(src._emission_threshold[0]);
		_emission_threshold[1] = std::move(src._emission_threshold[1]);
		_emission_samples = std::move(src._emission_samples);
		
		return(*this);
	}

	cExplosionGameObject::cExplosionGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _animation(instance_), _temperatureBoost(DEFAULT_TEMPERATURE_BOOST), _flameBoost(DEFAULT_FLAME_BOOST), _emission_threshold{DEFAULT_EMISSION_THRESHOLD, 0.0f}, _emission_samples(1)
	{
		instance_->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);

		// random start angle
		instance_->setYaw(v2_rotation_t(PsuedoRandomFloat() * XM_2PI));
#ifdef DEBUG_EXPLOSION_WINDOW
		debug_explosion_game_object = this;
#endif
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cExplosionGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cExplosionGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cExplosionGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		static constexpr float const NORMALIZE = 1.0f / float(UINT8_MAX);
		static constexpr float const DENORMALIZE = float(UINT8_MAX);
		
		// density - temperature - flames 
		uint32_t const udensity(voxel.Color & 0xff);
		uint32_t const utemperature((voxel.Color >> 8) & 0xff);
		uint32_t const uflames((voxel.Color >> 16) & 0xff);
		
		XMVECTOR xmColor(XMVectorZero()); // default color is always initialized to "background" (pure black)
		
		float density(0.0f);
		if (udensity) {
			density = (float)udensity * NORMALIZE;
			xmColor = XMVectorReplicate(density);
			// smoke voxels are "checkerboarded"
			voxel.Hidden = (voxel.x & 1) | (voxel.y & 1) | (voxel.z & 1);
		}
		
		if (uflames) { // flames present?
			
			float const temperature((float)uflames * NORMALIZE);

			uvec4_v const black_body(MinCity::VoxelWorld->blackbody(temperature + _flameBoost)); // convert temperature to color

			xmColor = XMVectorAdd(xmColor, XMVectorScale(black_body.v4f(), NORMALIZE));
			voxel.Hidden = false;
		}
		
		if (utemperature) { // temperature present?

			float const temperature((float)utemperature * NORMALIZE);

			uvec4_v const black_body(MinCity::VoxelWorld->blackbody(temperature + _temperatureBoost)); // convert temperature to color

			xmColor = XMVectorAdd(xmColor, XMVectorScale(black_body.v4f(), NORMALIZE));
			voxel.Hidden = false;
		}

		// area's that are less dense are brighter than denser areas - creates dark "spots" where density is high
		xmColor = XMVectorScale(xmColor, (1.0f - density)); // darken
		
		float const luma(SFM::XMColorRGBToLuminance(xmColor));

		voxel.Emissive = luma > _emission_threshold[1]; // some light does not need to be added (optimization)
		if (voxel.Emissive) {
			const_cast<cExplosionGameObject* const>(this)->_emission_threshold[0] += luma;     // @todo this breaks the rules with parallel execution of onVoxel (read-only access is safe, nothing else)
			++const_cast<cExplosionGameObject* const>(this)->_emission_samples;
		}
		
		uvec4_t rgba;
		SFM::saturate_to_u8(XMVectorScale(xmColor, DENORMALIZE), rgba);
		voxel.Color = 0x00ffffff & SFM::pack_rgba(rgba);

		// (mass of voxel is 1.0) f = ma   So FORCE = ACCELERATION
		// therefore this is setup to be in voxels/second squared [acceleration]
		// obey cPhysics:MIN_FORCE and cPhysics:MAX_FORCE limits or else the explosion will explode (as suggested by github pilot)
		MinCity::Physics->add_force(xmIndex);
		
		return(voxel);
	}

	void cExplosionGameObject::setElevation(float const elevation)
	{
		[[unlikely]] if (!Validate())
			return;

		Instance->setElevation(elevation);
	}

	void cExplosionGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;

		if (_animation.update(*Instance, tDelta)) {

			Instance->destroy(milliseconds(0));
			return;
		}
			
		Instance->setYaw(getModelInstance()->getYaw() + v2_rotation_t(tDelta.count() * XM_2PI * 0.05f));
		
		// ** stable feedback auto regulating emission threshold. Dependent on both the temperature + flames boost levels/inputs.
		// ** do not change ** provides some dynamic range to the emission/lighting. also optimizes out dark lights that have ~nil emission.
		float const new_emission_threshold = _emission_threshold[0] / ((float)_emission_samples);
		_emission_threshold[0] = SFM::abs(new_emission_threshold - _emission_threshold[1]); // the "step" to the new emission threshold
		_emission_threshold[1] = SFM::lerp(_emission_threshold[1], SFM::lerp(DEFAULT_EMISSION_THRESHOLD * (1.0f + _emission_threshold[0] * new_emission_threshold * 3.0f), new_emission_threshold, SFM::saturate(SFM::__pow(_emission_threshold[0], 3.0f))), _emission_threshold[0]);
		
		_emission_samples = 1;
	}


} // end ns world

