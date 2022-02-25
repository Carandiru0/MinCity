#pragma once

// must be defined 1st
#ifndef GAMEOBJECT_T
#define GAMEOBJECT_T cPoliceCarGameObject
#endif

#include "cCarGameObject.h"

namespace world
{
	class cPoliceCarGameObject : public cCarGameObject // type colony becomes cPoliceCarGameObject
	{
		friend cCarGameObject;

		static constexpr milliseconds const
			CAR_CREATE_INTERVAL = milliseconds(1500),
			CAR_IDLE_MAX = milliseconds(3500),
			CAR_PURSUIT_CHECK = milliseconds(5000);

		static constexpr uint32_t const
			MAX_CARS = 2;

		static constexpr float const
			MIN_SPEED = 50.0f,
			PURSUIT_SPEED = MIN_SPEED * 1.5f;

		static constexpr uint32_t const
			MASK_COLOR_BLUE = 0xff0000,		//bgra
			MASK_COLOR_RED = 0x0000ff;		//bgra

		static constexpr fp_seconds const
			LIGHT_SWITCH_INTERVAL = duration_cast<fp_seconds>(milliseconds(75));
		static constexpr double const
			INV_LIGHT_SWITCH_INTERVAL = 1.0 / LIGHT_SWITCH_INTERVAL.count();
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::PoliceCarGameObject);
		}

		void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		static void UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	protected:
		void setInitialState(cCarGameObject::state&& initialState);
	private:
		static void CreateCar();
	public:
		cPoliceCarGameObject(cPoliceCarGameObject&& src) noexcept;
		cPoliceCarGameObject& operator=(cPoliceCarGameObject&& src) noexcept;
	private:
		struct {

			tTime			checked_last;

			fp_seconds		tLastLights;
			int32_t			stateLights,
							laststateLights;
			uint32_t		colorBlueLight,
							colorRedLight;
			bool			bLightsOn;

		} _this = {};

	public:
		cPoliceCarGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cPoliceCarGameObject& __restrict left, cPoliceCarGameObject& __restrict right) noexcept
	{
		cPoliceCarGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

} // end namespace world


