#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "voxelAnim.h"

namespace world
{

	class cExplosionGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cExplosionGameObject>
	{
	public:
		static constexpr float const DEFAULT_BRIGHTNESS = 0.5f,
									 DEFAULT_EMISSION_THRESHOLD = 0.25f;
		
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::NonSaveable);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		float const			getBrightness() const { return(_brightness); }
		
		void				setBrightness(float const brightness) { _brightness = brightness; }
		
		float const			getEmissionThreshold() const { return(_emission_threshold); }

		void				setEmissionThreshold(float const luminance) { _emission_threshold = luminance; }
		
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cExplosionGameObject(cExplosionGameObject&& src) noexcept;
		cExplosionGameObject& operator=(cExplosionGameObject&& src) noexcept;
	private:
		Volumetric::voxelAnim<true>	_animation;
		float					    _brightness,
									_emission_threshold;
	public:
		cExplosionGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cExplosionGameObject& __restrict left, cExplosionGameObject& __restrict right) noexcept
	{
		cExplosionGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


