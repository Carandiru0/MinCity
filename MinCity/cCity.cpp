#include "pch.h"
#include "globals.h"
#include "cCity.h"
#include <Random/superrandom.hpp>

static constexpr fp_seconds const		EPSILON = fp_seconds(fixed_delta_duration);
static constexpr milliseconds const		UPDATE_INTERVAL = milliseconds(250);
static constexpr fp_seconds const		MAX_POP_DELTA_LIFE = duration_cast<fp_seconds>(minutes(5));
static constexpr fp_seconds const		MAX_CASH_DELTA_LIFE = duration_cast<fp_seconds>(seconds(30));

cCity::cCity(std::string_view const name)
	: _name(name), _info{}, _population_committed(0)
{

}

void cCity::modifyPopulationBy(int32_t delta)
{
	fp_seconds const tLife( PsuedoRandomFloat() * MAX_POP_DELTA_LIFE + EPSILON); // always not zero

	if (delta < 0) {
		// if the change will put the committed population below zero at some point in time
		int64_t committed((int64_t)_population_committed);
		committed -= (int64_t)delta;
		if (committed < 0) {
			// clamp the delta value to a value where the committed population would at most equal 0
			delta = delta + (int32_t)committed;
		}
	}

	// never add changes of zero... no change
	if (0 == delta)
		return;

	_population_changes.emplace_back(deltaGrowth(tLife, now(), delta));
}
void cCity::modifyCashBy(int32_t const delta)
{
	fp_seconds const tLife( PsuedoRandomFloat() * MAX_POP_DELTA_LIFE + EPSILON); // always not zero

	_cash_changes.emplace_back(deltaGrowth(tLife, now(), delta));
}


void cCity::Update(tTime const tNow)
{
	// bad idea ?

	using iter_delta = std::vector<deltaGrowth>::const_iterator;

	constinit static tTime tLast(zero_time_point);

	if (tNow - tLast >= nanoseconds(UPDATE_INTERVAL)) {

		// population count //
		// calculate current population based off a committed value that accumulates the changes that have expired
		// and the new changes pending
		// // new changes add to committed population once expired and get removed from vector
		// // new changes that are not expired calculate there growth to the delta value they have with linear interpolation
		// // this is added to current population
	
		int64_t iDelta(0);

		for (iter_delta i = _population_changes.cbegin(); i != _population_changes.cend() ; ++i) {

			deltaGrowth const& growth(*i);

			fp_seconds const tAge(tNow - growth.tCreated);

			float const fAge(tAge / growth.tLife);

			if (fAge < 1.0f) { // not expired

				// any single change is limited to int32_t in magnitude
				// however this does not limit the accumulated delta which is int64_t (unlikely exception)
				iDelta += (int64_t)SFM::round_to_i32(SFM::lerp(0.0f, growth.delta, SFM::saturate(fAge))); // accumulate changes in population
			}
			else { // expired

				int64_t committed((int64_t)_population_committed);
				committed += (int64_t)growth.delta;
				// ensure committed never goes below zero //
				committed = std::max(0LL, committed);
				// now safetly set back to the member that's unsigned
				_population_committed = committed;

				_population_changes.erase(i); // last!  ***** crashes here
			}
		}
		// add the accumulated change to committed population (not setting committed population //
		int64_t population((int64_t)_population_committed);
		population += iDelta;
		// current population
		_info.population = std::max(0LL, population);

		// cash count //
		// calculate cuurent cash - this has no seperate committed value - itself is just the current cash that has accumulated
		// from the posted changes. the posted changes do not linearly interpolate like population, rather they add their value when they expire
		// and are then removed from the vector
		iDelta = 0;

		for (iter_delta i = _cash_changes.cbegin(); i != _cash_changes.cend() ; ++i) {

			deltaGrowth const& growth(*i);

			fp_seconds const tAge(tNow - growth.tCreated);

			// expired ?
			if (tAge >= growth.tLife) {

				// any single change is limited to int32_t
				// however the total for cash is not limited being int64_t
				// add directly to cash member, can be negative
				_info.cash += (int64_t)growth.delta;
				_cash_changes.erase(i); // last!
			}
		}

		tLast = tNow;
	}
	
}


