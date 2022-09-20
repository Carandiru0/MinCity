#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{
	// forward decl
	class cYXIGameObject;

	class cYXISphereGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cYXISphereGameObject>
	{
		static constexpr uint32_t const
			THRUSTER_COUNT = 6;

		static constexpr uint32_t const  // bgr
			MASK_X_MIN = 0x00007f,
			MASK_X_MAX = 0x0000ff,
			MASK_Y_MIN = 0x007f00,
			MASK_Y_MAX = 0x00ff00,
			MASK_Z_MIN = 0x7f0000,
			MASK_Z_MAX = 0xff0000,
			COLOR_THRUSTER_GRADIENT_MIN = 0xff0000,
			COLOR_THRUSTER_GRADIENT_MAX = 0xb08651;

		static constexpr uint32_t const // XxYyZz(6bits)
			X_MAX_BIT = (1 << 5),
			X_MIN_BIT = (1 << 4),
			Y_MAX_BIT = (1 << 3),
			Y_MIN_BIT = (1 << 2),
			Z_MAX_BIT = (1 << 1),
			Z_MIN_BIT = (1 << 0);

		static constexpr fp_seconds const
			THRUSTER_COOL_DOWN = duration_cast<fp_seconds>(milliseconds(500));
	public:
		static constexpr float const
			MAX_THRUST = 0.5f;
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		float const						getMass() const { return(_body.mass); }
		XMVECTOR const __vectorcall		getAngularForce() const { return(XMLoadFloat3A(&_body.angular_force)); }



		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		void setParent(cYXIGameObject* const parent, FXMVECTOR const xmOffset) { _parent = parent; XMStoreFloat3A(&_offset, xmOffset); }

		XMVECTOR const __vectorcall applyAngularThrust(FXMVECTOR xmThrust, bool const auto_leveling_enable = false);
	
	private:
		void __vectorcall startThruster(FXMVECTOR xmThrust, bool const auto_leveling_enable);

	public:
		cYXISphereGameObject(cYXISphereGameObject&& src) noexcept;
		cYXISphereGameObject& operator=(cYXISphereGameObject&& src) noexcept;

	private:
		cYXIGameObject *      _parent;
		XMFLOAT3A			  _offset;

		struct {
			XMFLOAT3A	thrust;
			float		tOn;	// linear 0.0f ... 1.0f of how "on" the thruster is
			fp_seconds	cooldown;
			bool		autoleveling; // is this the auto-counter thrust? (auto-leveling thrust)

			void __vectorcall On(FXMVECTOR xmThrust, bool const auto_leveling_enable)
			{
				XMStoreFloat3A(&thrust, xmThrust);
				tOn = 1.0f;
				cooldown = THRUSTER_COOL_DOWN;
				autoleveling = auto_leveling_enable;
			}

			void Off()
			{
				XMStoreFloat3A(&thrust, XMVectorZero());
				tOn = 0.0f;
				cooldown = zero_time_duration;
				autoleveling = false;
			}

			void CounterOff()
			{
				// transition to non-auto levelling state smoothly
				XMVECTOR const xmThrust = SFM::lerp(XMVectorZero(), XMLoadFloat3A(&thrust), tOn);
				XMStoreFloat3A(&thrust, xmThrust);
				autoleveling = false;

			}
		} _thruster[THRUSTER_COUNT];

		uint32_t			  _thrusters;		// active thruster(s) for emission		XxYyZz (6bits)

		struct {
			XMFLOAT3A	angular_force, angular_thrust, angular_velocity;
			float		mass;

		} _body;

	public:
		cYXISphereGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cYXISphereGameObject& __restrict left, cYXISphereGameObject& __restrict right) noexcept
	{
		cYXISphereGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


