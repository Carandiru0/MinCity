#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{

	class cYXIGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cYXIGameObject>
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

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		float const& getMass() const { return(_body.mass); }

		void __vectorcall applyThrust(FXMVECTOR xmThrust);
		void __vectorcall applyAngularThrust(FXMVECTOR xmThrust);

	public:
		cYXIGameObject(cYXIGameObject&& src) noexcept;
		cYXIGameObject& operator=(cYXIGameObject&& src) noexcept;
	private:
		struct {
			XMFLOAT3A	force, thrust, velocity;
			XMFLOAT3A	angular_force, angular_thrust, angular_velocity;
			float		mass;

		} _body;
	public:
		cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cYXIGameObject& __restrict left, cYXIGameObject& __restrict right) noexcept
	{
		cYXIGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


