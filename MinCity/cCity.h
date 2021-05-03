#pragma once
#include <string_view>
#include <vector>
#include <Utility/class_helper.h>
#include "tTime.h"
#include "CityInfo.h"

typedef struct deltaGrowth
{
	fp_seconds			tLife;
	tTime				tCreated;
	float				delta;

	deltaGrowth(fp_seconds const tLife_, tTime const tCreated_, int32_t const delta_)
		: tLife(tLife_), tCreated(tCreated_), delta((float)delta_)
	{}

} deltaGrowth;

class cCity : no_copy
{
public:
	CityInfo const&			getInfo() const { return(_info); }
	std::string_view const	getName() const { return(_name); }
	uint64_t const			getPopulation() const { return(_info.population); }
	int64_t const			getCash() const { return(_info.cash); }

	void					modifyPopulationBy(int32_t delta);
	void					modifyCashBy(int32_t const delta);

	// main methods
	void Update(tTime const tNow);

private:
	CityInfo					_info;
	std::string_view const		_name;

	uint64_t					_population_committed;

	std::vector<deltaGrowth>	_population_changes,
								_cash_changes;

public:
	cCity(std::string_view const name);
};




