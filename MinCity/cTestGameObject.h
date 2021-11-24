#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{

	class cTestGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cTestGameObject>
	{
#ifdef GIF_MODE

		static constexpr uint32_t const  // bgr
			GLASS_COLOR = 0xffffff,
			BULB_COLOR = 0x551099;

#endif

		static constexpr uint32_t const  // bgr
			MASK_GLASS_COLOR = 0xffffff, 
			MASK_BULB_COLOR = 0x19ffff;
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

		static Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cTestGameObject(cTestGameObject&& src) noexcept;
		cTestGameObject& operator=(cTestGameObject&& src) noexcept;
	private:
		v2_rotation_t _parent_rotation;

		uint32_t _glass_color,
				 _bulb_color;

		float _accumulator,
			  _direction;
	public:
		cTestGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cTestGameObject& __restrict left, cTestGameObject& __restrict right) noexcept
	{
		cTestGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


