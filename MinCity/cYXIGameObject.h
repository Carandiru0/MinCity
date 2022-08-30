#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{

	class cYXIGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cYXIGameObject>
	{
		static constexpr uint32_t const  // bgr
			MASK_THRUSTER_CORE = 0xff0000,
			MASK_THRUSTER_GRADIENT1 = 0xf6401f,
			MASK_THRUSTER_GRADIENT2 = 0xbe7846,
			MASK_THRUSTER_GRADIENT3 = 0xb08651;

		static constexpr fp_seconds const
			THRUSTER_COOL_DOWN = duration_cast<fp_seconds>(milliseconds(3333));

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
			XMFLOAT3A	thrust;
			float		tOn;	// linear 0.0f ... 1.0f of how "on" the thruster is
			fp_seconds	cooldown;

			void __vectorcall On(FXMVECTOR xmThrust)
			{
				XMStoreFloat3A(&thrust, xmThrust);
				tOn = 1.0f;
				cooldown = THRUSTER_COOL_DOWN;
			}

			void Off()
			{
				XMStoreFloat3A(&thrust, XMVectorZero());
				tOn = 0.0f;
				cooldown = zero_time_duration;
			}

		} _thruster;

		bool	_mainthruster;

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


