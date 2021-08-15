#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "cAISkyMover.h"

// forward decl
namespace Volumetric
{
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	}
}
namespace world
{
	class cCopterGameObject;
}

namespace world
{
	class cCopterBodyGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cCopterBodyGameObject>
	{
	public:
		bool const			isLightsOn() const { return(_this.bLightsOn); }

	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::CopterBodyGameObject);
		}

		bool const __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		// typedef Volumetric::voxB::voxelState const(* const voxel_event_function)(void* const _this, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState);
		static Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

		void SetOwnerCopter(cCopterGameObject* const& owner);

	public:
		cCopterBodyGameObject(cCopterBodyGameObject&& src) noexcept;
		cCopterBodyGameObject& operator=(cCopterBodyGameObject&& src) noexcept;
	private:
		static constexpr fp_seconds const 
			LIGHT_SWITCH_INTERVAL = duration_cast<fp_seconds>(milliseconds(200));
		static constexpr float const
			INV_LIGHT_SWITCH_INTERVAL = 1.0f / LIGHT_SWITCH_INTERVAL.count();

		static constexpr float
			MAX_SPEED = 30.0f,
			MIN_SPEED = 15.0f;
		
		static constexpr uint32_t const
			MASK_COLOR_BLUE = 0x853211,		//bgra
			MASK_COLOR_RED = 0x4c10d0;		//bgra

		struct {											
			cAISkyMover		ai;
			
			fp_seconds		tLastLights;
			int32_t			stateLights,
							laststateLights;
			uint32_t		colorBlueLight,
							colorRedLight;
			bool			bLightsOn;

			cCopterGameObject* owner_copter;

		} _this = {};
	public:
		cCopterBodyGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cCopterBodyGameObject();
	};

	STATIC_INLINE_PURE void swap(cCopterBodyGameObject& __restrict left, cCopterBodyGameObject& __restrict right) noexcept
	{
		cCopterBodyGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


