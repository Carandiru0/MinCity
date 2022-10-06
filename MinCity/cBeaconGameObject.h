#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{
	class cBeaconGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cBeaconGameObject>
	{
		static constexpr uint32_t const
			COLOR_LIGHT = 0x00ffffff;

		static constexpr float const
			INTERVAL_MIN = time_to_float(duration_cast<fp_seconds>(milliseconds(500))),
			INTERVAL_MAX = time_to_float(duration_cast<fp_seconds>(milliseconds(3300)));

	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}
		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		cBeaconGameObject(cBeaconGameObject&& src) noexcept;
		cBeaconGameObject& operator=(cBeaconGameObject&& src) noexcept;

	private:
		struct {
			fp_seconds                        interval;
			fp_seconds						  accumulator;

		} _activity_light;

	public:
		cBeaconGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cBeaconGameObject& __restrict left, cBeaconGameObject& __restrict right) noexcept
	{
		cBeaconGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


