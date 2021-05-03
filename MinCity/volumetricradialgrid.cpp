/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#include "pch.h"
#include "globals.h"
#include "IsoVoxel.h"
#include "VOLUMETRICRADIALGRID.h"

namespace Volumetric
{
	
std::optional<tTime const> const sRadialGridInstance::Update(tTime const tNow)
{			
	if (isInvalidated()) {
		Volumetric::radialgrid_generate(getRadius(), const_cast<std::vector<Volumetric::xRow, tbb::scalable_allocator<Volumetric::xRow>>& __restrict>(InstanceRows));
		resetInvalidated();
	}
	
	return(UpdateLocalTime(tNow));
}
	
	// Stored as relative displacement, not absolute positions
	// Length now optimized out, see xRow struct
#define plot4points(x, y, vecRows) \
{ \
	float const xn(-x); \
  \
	vecRows.emplace_back( xRow(xn, y) ); \
  \
	if (0.0f != y) \
		vecRows.emplace_back( xRow(xn, -y) ); \
} \

NO_INLINE void radialgrid_generate(float const radius, std::vector<xRow, tbb::scalable_allocator<xRow>>& __restrict vecRows)
{
	static constexpr float const step(Iso::MINI_VOX_STEP);
	float  error(-radius),
			   x(radius),
			   y(0.0f);
	
	vecRows.clear(); // important!!!!
	vecRows.reserve((((uint32_t)radius) << 1) * 2 + 1);
	
	while (x >= y)
	{
		float const lastY(y);

		error += y;
		y += step;
		error += y;

		plot4points(x, lastY, vecRows);

		if (error >= 0.0f)
		{
			if (x != lastY)
				plot4points(lastY, x, vecRows);		

			error -= x;
			x -= step;
			error -= x;
		}
	}
	
	// not required ?
	//std::sort(vecRows.begin(), vecRows.end());
	// if needed by reserve above (less fragmentation of heap)
}

} // end namespace


