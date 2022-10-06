#pragma once

#include "cAttachableGameObject.h"
#include "voxelAnim.h"

namespace world
{
	class cThrusterFireGameObject : public cAttachableGameObject, public type_colony<cThrusterFireGameObject>
	{
	public:
		static constexpr uint32_t const  // bgr
			COLOR_THRUSTER_GRADIENT_MIN = 0xff0000,
			COLOR_THRUSTER_GRADIENT_MAX = 0xb08651;
	private:
		static constexpr float const
			DEFAULT_TEMPERATURE_BOOST = 0.0f,
			DEFAULT_FLAME_BOOST = 0.0f,
			DEFAULT_EMISSION_THRESHOLD = 0.9f; // higher = more darkness optimized out (doesn't emit light for area's with color that is dark enough to have ~nil luminance) (tuned - if too high regulation of emission threshold blows up (becomes to sensitive))

	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;
	public:
		float const      getIntensity() const { return(_intensity); }

		void             setIntensity(float const intensity_) { _intensity = intensity_; }
		void             resetAnimation();
		
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		cThrusterFireGameObject(cThrusterFireGameObject&& src) noexcept;
		cThrusterFireGameObject& operator=(cThrusterFireGameObject&& src) noexcept;

	private:
		Volumetric::voxelAnim<true>	_animation;
		float                       _intensity;

		struct {

			float
				temperature_boost,
				flame_boost,
				emission_threshold;

		} _characteristics;

	public:
		void setCharacteristics(float const temperature_boost_ = DEFAULT_TEMPERATURE_BOOST, float const flame_boost_ = DEFAULT_FLAME_BOOST, float const emission_threshold_ = DEFAULT_EMISSION_THRESHOLD);

	public:
		cThrusterFireGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cThrusterFireGameObject& __restrict left, cThrusterFireGameObject& __restrict right) noexcept
	{
		cThrusterFireGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns



