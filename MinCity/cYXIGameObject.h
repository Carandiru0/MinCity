#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "cPhysics.h"
#include "cThrusterFireGameObject.h"

// forward dcl
class cUser;

namespace world
{
	class cYXIGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cYXIGameObject>
	{
		static constexpr uint32_t const
			MAIN = 0,
			UP = 1,
			THRUSTER_COUNT = 2;

		static constexpr uint32_t const // better place #todo
			out = 0,
			in = 1;

		static constexpr uint32_t const  // bgr
			COLOR_THRUSTER_CORE = 0xff0000,
			COLOR_THRUSTER_GRADIENT1 = 0xf6401f,
			COLOR_THRUSTER_GRADIENT2 = 0xbe7846,
			COLOR_THRUSTER_GRADIENT3 = 0xb08651,
			COLOR_THRUSTER_GRADIENT_MIN = COLOR_THRUSTER_CORE,
			COLOR_THRUSTER_GRADIENT_MAX = COLOR_THRUSTER_GRADIENT3,
			COLOR_THRUSTER_UP = 0x00ffff, // mask
			COLOR_ACTIVITY_LIGHTS = 0x3df79f; // mask

		static constexpr fp_seconds const
			MAIN_THRUSTER_COOL_DOWN = duration_cast<fp_seconds>(milliseconds(3333)),
			UP_THRUSTER_COOL_DOWN = duration_cast<fp_seconds>(milliseconds(1000));

	public:
		static constexpr float const
			MIN_UP_THRUST = -cPhysics::GRAVITY;
		static constexpr float const     // total thrust available to main thruster is shared with up thruster, with up thruster always taking priority. However the main thruster has a much higher peak thrust.
			MAX_MAIN_THRUST = 4.0f,      // maximum = MAX_MAIN_THRUST , minimum = MAX_MAIN_THRUST - MAX_UP_THRUST (when up thruster is on fully)
			MAX_UP_THRUST = MAX_MAIN_THRUST * 0.95f; // needs to be greater than gravity, 95% maximum up thrust of main thruster energy. higher = less efficient and steals more thrust from main thruster when on.
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		float const						getMass() const { return(_body.mass); }
		XMVECTOR const __vectorcall		getVelocity() const { return(XMLoadFloat3A(&_body.velocity)); }
		XMVECTOR const __vectorcall		getAngularVelocity() const { return(XMLoadFloat3A(&_body.angular_velocity)); }

		void __vectorcall applyForce(FXMVECTOR xmForce);			// F = ma
		void __vectorcall applyAngularForce(FXMVECTOR xmForce);		// T = F x r

		void __vectorcall applyThrust(FXMVECTOR xmThrust);									// F = ma
		void __vectorcall applyAngularThrust(FXMVECTOR xmThrust);							// T = F x r

		void setParent(cUser* const);
		void enableThrusterFire(float const power);
		void updateThrusterFire(float const power);
		void disableThrusterFire();

		bool const isDestroyed() const;
		void destroy();
	private:
		void autoThrust(float const ground_clearance, fp_seconds const tDelta);
		bool const collide(float const ground_clearance, fp_seconds const tDelta);

	public:
		cYXIGameObject(cYXIGameObject&& src) noexcept;
		cYXIGameObject& operator=(cYXIGameObject&& src) noexcept;
	private:

		struct {
			fp_seconds  THRUSTER_COOL_DOWN; // must be first

			XMFLOAT3A	thrust;
			float		tOn;	// linear 0.0f ... 1.0f of how "on" the thruster is
			fp_seconds	cooldown;
			bool		bOn,
						bAuto;

			void __vectorcall On(FXMVECTOR xmThrust)
			{
				XMStoreFloat3A(&thrust, xmThrust);
				tOn = 1.0f;
				cooldown = THRUSTER_COOL_DOWN;
				bOn = true;
				bAuto = false; // always reset so that any user thrust "on" cancels the automatic flag if it is currently flagged as automatic.
			}

			void Off()
			{
				bOn = false;
				bAuto = false;
				XMStoreFloat3A(&thrust, XMVectorZero());
				tOn = 0.0f;
				cooldown = zero_time_duration;
			}

		} _thruster[THRUSTER_COUNT];
	
		struct {
			XMFLOAT3A	force[2], thrust, velocity;
			XMFLOAT3A	angular_force[2], angular_thrust, angular_velocity;
			float		mass;

		} _body;

		struct activity_lights {
			static constexpr fp_seconds const interval = duration_cast<fp_seconds>(milliseconds(2 * 1618));
			fp_seconds						  accumulator;

		} _activity_lights;

		cThrusterFireGameObject* _thrusterFire;
		cUser* _parent;
	public:

		float const                     getThrusterPower(uint32_t const index) { return(_thruster[index].tOn); }

		cYXIGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_);
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


