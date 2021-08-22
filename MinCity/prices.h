#pragma once
#ifndef PRICES_H
#define PRICES_H

#include "betterenums.h"

BETTER_ENUM(ePrices, int64_t const,
	ZERO = 0,
	RESIDENTIAL = 100,
	COMMERCIAL = 200,
	INDUSTRIAL = 400
);


#endif // PRICES_H

