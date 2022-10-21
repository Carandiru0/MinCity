#pragma once

#include "cAttachableGameObject.h"

namespace world
{
	class cLightConeGameObject : public cAttachableGameObject, public type_colony<cLightConeGameObject>
	{
		static constexpr uint32_t
			COLOR_LIGHT_VOXEL = 0x0000ff00;  // abgr

	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		cLightConeGameObject(cLightConeGameObject&& src) noexcept;
		cLightConeGameObject& operator=(cLightConeGameObject&& src) noexcept;

	private:
		struct {
			static constexpr fp_seconds const interval = duration_cast<fp_seconds>(milliseconds(3618>>1));
			fp_seconds						  accumulator;

		} _activity_lights;
	public:
		cLightConeGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_);
	};

	STATIC_INLINE_PURE void swap(cLightConeGameObject& __restrict left, cLightConeGameObject& __restrict right) noexcept
	{
		cLightConeGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns

#undef GAMEOBJECT_T
