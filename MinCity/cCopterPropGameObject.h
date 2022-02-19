#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

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

	class cCopterPropGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cCopterPropGameObject>
	{
	public:
		bool const			isLightsOn() const { return(_this.bLightsOn); }

		void				setLightsOn(bool const bOn) { _this.bLightsOn = bOn; }
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::CopterPropGameObject);
		}

		void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, FXMVECTOR xmLocation, float const fElevation, v2_rotation_t const& azimuth);

		static void __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, void const* const __restrict _this, uint32_t const vxl_index);
		void __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, uint32_t const vxl_index) const;

		void SetOwnerCopter(cCopterGameObject* const& owner);
	public:
		cCopterPropGameObject(cCopterPropGameObject&& src) noexcept;
		cCopterPropGameObject& operator=(cCopterPropGameObject&& src) noexcept;
	private:
		static constexpr fp_seconds const
			LIGHT_SWITCH_INTERVAL = duration_cast<fp_seconds>(milliseconds(200)); // white lights on prop

		static constexpr uint32_t const			//bgra
			MASK_LIGHT_ONE = 0x4c10d0,
			MASK_LIGHT_TWO = 0x66c186,
			MASK_LIGHT_THREE = 0x853211,
			MASK_LIGHT_FOUR = 0xffffff,
			MASK_COLOR_WHITE = 0xffffff,
			NUM_LIGHTS = 4;

		struct {											
			v2_rotation_t		angle;
			fp_seconds			tLastLights;
			uint32_t			colorLights[NUM_LIGHTS];
			uint32_t			idleLightOnIndex;
			bool				bLightsOn;

			cCopterGameObject* owner_copter;
		} _this = {};
	public:
		cCopterPropGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cCopterPropGameObject();
	};

	STATIC_INLINE_PURE void swap(cCopterPropGameObject& __restrict left, cCopterPropGameObject& __restrict right) noexcept
	{
		cCopterPropGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


