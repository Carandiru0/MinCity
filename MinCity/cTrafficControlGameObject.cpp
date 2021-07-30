#include "pch.h"
#include "cTrafficControlGameObject.h"
#include "voxelModelInstance.h"
#include "cTrafficSignGameObject.h"
#include "eTrafficLightState.h"
#include "cVoxelWorld.h"

namespace world
{
	static constexpr fp_seconds const BLINK_INTERVAL = duration_cast<fp_seconds>(milliseconds(400));

	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cTrafficControlGameObject::remove(static_cast<cTrafficControlGameObject const* const>(_this));
		}
	}

	// state>			  0						1
	//	 0		GREEN_TURNING_ENABLED		RED_STOP
	//	 1		GREEN_TURNING_DISABLED		RED_STOP
	///	 2		GREEN_TURNING_DISABLED		RED_STOP
	//	 3		YELLOW_CLEAR				RED_STOP
	//	 n		goto side 1					goto side 0
	static constexpr uint32_t const STATE_COUNT = 8;		// must be power of 2
	static constexpr uint32_t const STATE_ORDER[STATE_COUNT] = {
		eTrafficLightState::GREEN_TURNING_ENABLED,		// 0
		eTrafficLightState::GREEN_TURNING_DISABLED,		// 1
		eTrafficLightState::GREEN_TURNING_DISABLED,		// 2
		eTrafficLightState::YELLOW_CLEAR,				// 3

		eTrafficLightState::RED_STOP,					// 4
		eTrafficLightState::RED_STOP,					// 5
		eTrafficLightState::RED_STOP,					// 6
		eTrafficLightState::RED_STOP					// 7
	};

	static constexpr fp_seconds const INTERVALS[STATE_COUNT] = {
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(2)),
		// interval must mirror or timing will be out of sync for xing
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(5)),
		duration_cast<fp_seconds>(seconds(2))
	};

	cTrafficControlGameObject::cTrafficControlGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _accumulator{}, _interval{},
		_signs{}, _sign_count(0), _counter{}, _state{}, _turning_side{ false, false }, _blink_accumulator{}
	{
		instance_->setOwnerGameObject<cTrafficControlGameObject>(this, &OnRelease);

		_sign_pair.reserve(4);

		// prime state
		_counter[0] = 0;
		_counter[1] = 4;

		_state[0] = STATE_ORDER[_counter[0]];
		_state[1] = STATE_ORDER[_counter[1]];
	}

	cTrafficControlGameObject::cTrafficControlGameObject(cTrafficControlGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTrafficControlGameObject>(this, &OnRelease);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTrafficControlGameObject>(nullptr, nullptr);
		}

		_accumulator = std::move(src._accumulator);
		_interval = std::move(src._interval);
		_blink_accumulator = std::move(src._blink_accumulator);

		for (uint32_t i = 0; i < 2; ++i) {
			_counter[i] = std::move(src._counter[i]);
			_state[i] = std::move(src._state[i]);
			_turning_side[i] = std::move(_turning_side[i]);
		}

		for (uint32_t i = 0; i < 4; ++i) {
			_signs[i] = std::move(src._signs[i]);
			src._signs[i] = nullptr;
		}
		_sign_count = std::move(src._sign_count);
		_sign_pair = std::move(src._sign_pair);
	}
	cTrafficControlGameObject& cTrafficControlGameObject::operator=(cTrafficControlGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTrafficControlGameObject>(this, &OnRelease);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTrafficControlGameObject>(nullptr, nullptr);
		}

		_accumulator = std::move(src._accumulator);
		_interval = std::move(src._interval);
		_blink_accumulator = std::move(src._blink_accumulator);

		for (uint32_t i = 0; i < 2; ++i) {
			_counter[i] = std::move(src._counter[i]);
			_state[i] = std::move(src._state[i]);
			_turning_side[i] = std::move(_turning_side[i]);
		}

		for (uint32_t i = 0; i < 4; ++i) {
			_signs[i] = std::move(src._signs[i]);
			src._signs[i] = nullptr;
		}
		_sign_count = std::move(src._sign_count);
		_sign_pair = std::move(src._sign_pair);
		
		return(*this);
	}

	size_t const cTrafficControlGameObject::exportData(vector<uint8_t>& out) const
	{
		struct {
			uint32_t	hash[4];
			uint32_t    sign_count;
		} data{};

		uint32_t count(0);

		for (uint32_t sign = 0; sign < _sign_count; ++sign) {
			if (_signs[sign]) {

				if (_signs[sign]->getModelInstance()) {
					data.hash[sign] = _signs[sign]->getModelInstance()->getHash();
					++count;
				}
			}
		}

		data.sign_count = count;

		size_t const bytes(sizeof(data));
		out.reserve(bytes); out.resize(bytes);
		memcpy(out.data(), &data, bytes);

		return(bytes);	}

	void cTrafficControlGameObject::importData(uint8_t const* const in, size_t const size)
	{
		struct {
			uint32_t	hash[4];
			uint32_t    sign_count;
		} data{};

		size_t const bytes(sizeof(data));
		if (size == bytes) { // validated?
			memcpy(&data, in, bytes);

			for (uint32_t sign = 0; sign < data.sign_count; ++sign) {

				auto * const pSignModelInstance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(data.hash[sign]);
				if (pSignModelInstance) {

					cTrafficSignGameObject* const pSign = pSignModelInstance->getOwnerGameObject<cTrafficSignGameObject>();

					if (pSign) {
						Add(pSign);
					}
				}
			}
		}
	}

	void cTrafficControlGameObject::deduce_sign_pairs()
	{
		point2D_t voxelIndex[4];

		for (uint32_t i = 0; i < 4; ++i) {
			if (_signs[i]) {
				voxelIndex[i] = _signs[i]->getModelInstance()->getVoxelIndex();
			}
		}

		// Reset sign pairs set
		_sign_pair.clear();
		
 		for (uint32_t j = 0; j < 4; ++j) {

			if (_signs[j]) {

				for (uint32_t i = 0; i < 4; ++i) {

					// if they don't share a common axis at all, the signs oppose each other and are a pair.
					if (_signs[i]) {
						if (voxelIndex[j].x != voxelIndex[i].x && voxelIndex[j].y != voxelIndex[i].y) {

							bool bUnique(true);

							for (set_sign_pair::const_iterator iter = _sign_pair.cbegin(); iter != _sign_pair.cend(); ++iter) {

								if (_signs[j] == iter->sign[0] || _signs[j] == iter->sign[1]) {
									bUnique = false;
									break;
								}

								if (_signs[i] == iter->sign[0] || _signs[i] == iter->sign[1]) {
									bUnique = false;
									break;
								}
							}

							if (bUnique) {
								_sign_pair.emplace_back(_signs[j], _signs[i]);
							}
						}
					}
				}
			}
		}

		// if sign_pair contains only one element (one pair of signs) it's a 3 way intersection
		// and we have to add the singular traffic sign with a null partner
 		if (_sign_pair.size() < 2) {

			cTrafficSignSet const& sign_pair = *_sign_pair.cbegin();
			for (uint32_t i = 0; i < 4; ++i) {

				if (_signs[i]) {
					if (sign_pair.sign[0] != _signs[i] && sign_pair.sign[1] != _signs[i]) {
						// lone traffic sign found
						_sign_pair.emplace_back(_signs[i], nullptr);
						break;
					}
				}
			}
		}
	}

	void cTrafficControlGameObject::Add(cTrafficSignGameObject* const& sign)
	{
		if (_sign_count < 4) {

			sign->setController(this);
			_signs[_sign_count] = sign;
			++_sign_count;

			if (_sign_count > 2) {
				deduce_sign_pairs();
			}
		}
	}

	void cTrafficControlGameObject::Remove(cTrafficSignGameObject* const& sign)
	{
		for (uint32_t i = 0; i < 4; ++i) {

			if (sign == _signs[i]) {
				_signs[i] = nullptr;
				--_sign_count;

				if (_sign_count > 2) {
					deduce_sign_pairs();
				}
				break;
			}
		}
	}

	void cTrafficControlGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		{ // don't update if road being drawn
			point2D_t const voxelIndex = getModelInstance()->getVoxelIndex();

			Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));
			if (pVoxel) {
				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isPending(oVoxel))
					return;
			}
		}

		if ((_accumulator += tDelta) >= _interval) {

			_accumulator -= _interval;

			for (uint32_t i = 0; i < 2; ++i) {
				
				_counter[i] = (_counter[i] + 1) & (STATE_COUNT - 1);  // wraps around
				_state[i] = STATE_ORDER[_counter[i]];
			}

			_interval = INTERVALS[_counter[0]]; // same "change" interval appplies to both sides of xing
			_blink_accumulator = zero_time_duration;

			uint32_t mutated_state[2]{ _state[0], _state[1] };

			// 3way xing specialization
			if (_sign_count < 4) {
				for (uint32_t i = 0; i < 2; ++i) {

					if (eTrafficLightState::GREEN_TURNING_ENABLED == mutated_state[i]) {

						if (_sign_pair[i].sign[0] && _sign_pair[i].sign[1]) { // no blinking on these sides of xing
							mutated_state[i] = eTrafficLightState::GREEN_TURNING_DISABLED;
						}
					}
					else if (eTrafficLightState::GREEN_TURNING_DISABLED == mutated_state[i]) { // always turning on this side of xing

						if ((_sign_pair[i].sign[0] && !_sign_pair[i].sign[1]) ||
							(!_sign_pair[i].sign[0] && _sign_pair[i].sign[1])) {
							mutated_state[i] = eTrafficLightState::GREEN_TURNING_ENABLED;
						}
					}
				}
			}

			uint32_t sign_pair_signal_turn_color[2]{},
					 sign_pair_signal_color[2]{};

			for (uint32_t i = 0; i < 2; ++i) {
				switch (mutated_state[i]) {
				case eTrafficLightState::GREEN_TURNING_ENABLED:
					sign_pair_signal_turn_color[i] = cTrafficSignGameObject::COLOR_GREEN;
					sign_pair_signal_color[i] = cTrafficSignGameObject::COLOR_GREEN;
					break;
				case eTrafficLightState::GREEN_TURNING_DISABLED:
					sign_pair_signal_turn_color[i] = cTrafficSignGameObject::COLOR_BLACK;
					sign_pair_signal_color[i] = cTrafficSignGameObject::COLOR_GREEN;
					break;
				case eTrafficLightState::YELLOW_CLEAR:
					sign_pair_signal_turn_color[i] = cTrafficSignGameObject::COLOR_YELLOW;
					sign_pair_signal_color[i] = cTrafficSignGameObject::COLOR_YELLOW;
					break;
				case eTrafficLightState::RED_STOP:
					sign_pair_signal_turn_color[i] = cTrafficSignGameObject::COLOR_RED;
					sign_pair_signal_color[i] = cTrafficSignGameObject::COLOR_RED;
					break;
				}
			}

			// apply output, light color, to traffic light pairs
			for (uint32_t i = 0; i < 2; ++i) {

				if (4 == _sign_count && eTrafficLightState::GREEN_TURNING_ENABLED == mutated_state[i])
				{
					// for alternating side of xing that gets the turning signal option
					cTrafficSignGameObject* const& signA(_sign_pair[i].sign[0]);
					cTrafficSignGameObject* const& signB(_sign_pair[i].sign[1]);
					if (_turning_side[i]) {

						if (signA) {
							signA->setColorSignalTurn(sign_pair_signal_turn_color[i]);
							signA->setColorSignal(sign_pair_signal_color[i]);
						}
						if (signB) {
							signB->setColorSignalTurn(cTrafficSignGameObject::COLOR_RED);
							signB->setColorSignal(cTrafficSignGameObject::COLOR_RED);
						}
					}
					else {

						if (signA) {
							signA->setColorSignalTurn(cTrafficSignGameObject::COLOR_RED);
							signA->setColorSignal(cTrafficSignGameObject::COLOR_RED);
						}
						if (signB) {
							signB->setColorSignalTurn(sign_pair_signal_turn_color[i]);
							signB->setColorSignal(sign_pair_signal_color[i]);
						}
					}

					_turning_side[i] = !_turning_side[i];
				}
				else {

					for (uint32_t j = 0; j < 2; ++j) {

						cTrafficSignGameObject* const& sign(_sign_pair[i].sign[j]);

						if (sign) {

							sign->setColorSignalTurn(sign_pair_signal_turn_color[i]);
							sign->setColorSignal(sign_pair_signal_color[i]);

						}
					}
				}
			}
		}

		// blinking turn signal, only one sign has a turning signal active at any given time
		for (uint32_t i = 0; i < 4; ++i) {

			if (_signs[i]) {
				if (eTrafficLightState::GREEN_TURNING_ENABLED == _signs[i]->getState()) {

					if ((_blink_accumulator += tDelta) >= BLINK_INTERVAL) {

						_blink_accumulator -= BLINK_INTERVAL;

						// bugfix: yellow at the end of the turning state, prevents last minute collisions (only required on 4way xing, as 3way xing just switches to full yellow)
						uint32_t blinkColor(cTrafficSignGameObject::COLOR_GREEN);
						if (4 == _sign_count && ((_interval - _accumulator) < (5 * BLINK_INTERVAL))) {
							blinkColor = cTrafficSignGameObject::COLOR_YELLOW;
						}
						
						if (blinkColor != _signs[i]->getColorSignalTurn()) {
							_signs[i]->setColorSignalTurn(blinkColor);
							break; 
						}
						else {
							_signs[i]->setColorSignalTurn(cTrafficSignGameObject::COLOR_BLACK + 1); // bugfix: +1 to avoid disabling turning on blink
							break;
						}
					}
				}

				_signs[i]->updateLightColor(tNow, tDelta);
			}
		}

	}


} // end ns world

