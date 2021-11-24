#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

#ifdef GIF_MODE
namespace world
{

	class cRockStageGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cRockStageGameObject>
	{
		static constexpr uint32_t const  // bgr
			GLASS_COLOR = 0xffffff,
			BULB_COLOR = 0x551099;

		static constexpr uint32_t const  // bgr
			MASK_GLASS_COLOR = 0xffffff, 
			MASK_BULB_COLOR = 0xc500f6;
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::RockStageGameObject);
		}

		static Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cRockStageGameObject(cRockStageGameObject&& src) noexcept;
		cRockStageGameObject& operator=(cRockStageGameObject&& src) noexcept;
	private:
		uint32_t 
			_glass_color,
			_bulb_color,
			_mini_light_color;

		float _accumulator,
			  _direction;

		fp_seconds _accumulator_strobe;

		bool const _odd_mini_light;
	public:
		cRockStageGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cRockStageGameObject& __restrict left, cRockStageGameObject& __restrict right) noexcept
	{
		cRockStageGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns

#endif
