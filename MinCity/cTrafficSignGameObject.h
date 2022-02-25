#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "ImageAnimation.h"
#include "eTrafficLightState.h"

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
	class cTrafficControlGameObject;
} // end ns

namespace world
{
	class cTrafficSignGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cTrafficSignGameObject>
	{
		static constexpr uint32_t const
			MASK_COLOR_SIGNAL_TURN = 0x3df79f,	//bgra
			MASK_COLOR_SIGNAL = 0x3b19ff;		//bgra

		static constexpr uint32_t const
			THRU = 0,
			TURN = 1;

	public:
		static constexpr uint32_t const // bgra
			COLOR_GREEN = 0x00ff00,
			COLOR_YELLOW = 0x33caff,
			COLOR_RED = 0x0000ff,
			COLOR_BLACK = 0x000001; // required since "0" is recognized as no pending color

	public:
		// overrides //
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TrafficSignGameObject);
		}
		// ALL derivatives of this class must call base function first in overriden methods, and check its return value
		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		uint32_t const getState() const { return(_state); }
		uint32_t const getColorSignalTurn() const { 
			if (_next_color_signal[TURN])
				return(_next_color_signal[TURN]); 
			return(_color_signal[TURN]);
		}
		uint32_t const getColorSignal() const { 
			if (_next_color_signal[THRU])
				return(_next_color_signal[THRU]);
			return(_color_signal[THRU]);
		}

		void setController(world::cTrafficControlGameObject* const& control);

		void setColorSignalTurn(uint32_t const color_signal_turn_) { 

			uint32_t const current_color(getColorSignalTurn());
			if (current_color != color_signal_turn_) { // only on changes

				if (cTrafficSignGameObject::COLOR_GREEN == color_signal_turn_) {
					_state = eTrafficLightState::GREEN_TURNING_ENABLED;
				}
				// this indicates turning is off on 2 consecutive BLACKS, only after turning signal enabled alreadyy. Compatible with fade out to black on blinking
				else if ((cTrafficSignGameObject::COLOR_YELLOW == color_signal_turn_ || cTrafficSignGameObject::COLOR_BLACK == color_signal_turn_) && eTrafficLightState::GREEN_TURNING_ENABLED == _state) {
					_state = eTrafficLightState::GREEN_TURNING_DISABLED; // safe to set will ease lock on turning state below setColorSignal
				}

				_next_color_signal[TURN] = color_signal_turn_;
				_last_color_signal[TURN] = _color_signal[TURN];
				_accumulator[TURN] = zero_time_duration;
			}
		}
		void setColorSignal(uint32_t const color_signal_) { 

			uint32_t const current_color(getColorSignal());

			if (current_color != color_signal_) { // only on changes

				switch (color_signal_)
				{
				case cTrafficSignGameObject::COLOR_GREEN:
					if (eTrafficLightState::GREEN_TURNING_ENABLED != _state) { // keep turning
						_state = eTrafficLightState::GREEN_TURNING_DISABLED;
					}
					break;
				case cTrafficSignGameObject::COLOR_YELLOW:
					_state = eTrafficLightState::YELLOW_CLEAR;
					break;
				case cTrafficSignGameObject::COLOR_RED:
					_state = eTrafficLightState::RED_STOP;
					break;
				// *no* default: all other colors ignored, state remains the same eg.) fade to black
				}

				_next_color_signal[THRU] = color_signal_;
				_last_color_signal[THRU] = _color_signal[THRU];
				_accumulator[THRU] = zero_time_duration;
			}
		}

		void updateLightColor(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		cTrafficSignGameObject(cTrafficSignGameObject&& src) noexcept;
		cTrafficSignGameObject& operator=(cTrafficSignGameObject&& src) noexcept;
	private:
		ImageAnimation*											_videoscreen;
		cTrafficControlGameObject*								_control;

		fp_seconds	_accumulator[2];

		uint32_t	_state;
		uint32_t	_color_signal[2],
					_last_color_signal[2],
					_next_color_signal[2];
	public:
		cTrafficSignGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cTrafficSignGameObject();
	};

	STATIC_INLINE_PURE void swap(cTrafficSignGameObject& __restrict left, cTrafficSignGameObject& __restrict right) noexcept
	{
		cTrafficSignGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


