#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "voxelAnim.h"

namespace world
{

	class cExplosionGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cExplosionGameObject>
	{
#ifdef DEBUG_EXPLOSION_WINDOW
	public:
		static inline cExplosionGameObject* debug_explosion_game_object = nullptr;
#endif
	public:
		static constexpr float const DEFAULT_TEMPERATURE_BOOST = 0.333f,
									 DEFAULT_FLAME_BOOST = 0.666f,			
									 DEFAULT_EMISSION_THRESHOLD = 0.15f; // higher = more darkness optimized out (doesn't emit light for area's with color that is dark enough to have ~nil luminance) (tuned - if too high regulation of emission threshold blows up (becomes to sensitive))
		
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::ExplosionGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		float const			getTemperatureBoost() const { return(_temperatureBoost); }
		
		void				setTemperatureBoost(float const temperature) { _temperatureBoost = temperature; }
		
		float const			getFlameBoost() const { return(_flameBoost); }

		void				setFlameBoost(float const temperature) { _flameBoost = temperature; }
		
		float const			getEmissionThreshold() const { return(_emission_threshold[1]); }

		void				setEmissionThreshold(float const luminance) { _emission_threshold[1] = luminance; }
		
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cExplosionGameObject(cExplosionGameObject&& src) noexcept;
		cExplosionGameObject& operator=(cExplosionGameObject&& src) noexcept;
	private:
		Volumetric::voxelAnim<true>	_animation;
		float					    _temperatureBoost, _flameBoost,
									_emission_threshold[2];
		uint32_t					_emission_samples;
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


