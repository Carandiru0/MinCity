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
#include "tornado.h"
#include "volumetricradialgrid.h"

#include <vector>

static constexpr milliseconds const LIFETIME_ANIMATION = milliseconds(12000);

inline vector<Volumetric::xRow>	TornadoInstance::TornadoRows;  // vector memory is persistance across instances
																																																										 // only one shockwave instance is active at a time
																																																										 // this improves performance and stability
																																																										 // by avoiding reallocation of vector
																																																										 // for any new or deleted shockwave instance
__vectorcall sTornadoInstance::sTornadoInstance( FXMVECTOR const WorldCoordOrigin, float const Radius)
		: sRadialGridInstance(WorldCoordOrigin, Radius, LIFETIME_ANIMATION, TornadoRows)
{
    setScale((float)TornadoInstance::TORNADO_AMPLITUDE_MAX);

}
		
STATIC_INLINE_PURE float const __vectorcall singlewave(float const x, float const t)
{
	float const X( x - t * t );
	return( -SFM::__cos(X) * SFM::__exp(-X * X));

	// return( -SFM::__cos(X) * (SFM::__exp(-X * (X)) - x*0.01f)); // repeating wave with gradually increasing amplitude
}

// op does not r/w any global memory voxel_op_fnDispersion
__forceinline __declspec(noalias) bool const __vectorcall sTornadoInstance::op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const
{
    out.distance = singlewave(_mm_cvtss_f32(XMVector2Length(XMVectorScale(vDisplacement, getRadius()))), t_);
    out.emissive = true;
	out.setColor(uvec4_v(0xFF, 0x00, 0x00));
	out.lit = true;

	return(true);
}

bool const isTornadoVisible(TornadoInstance const* const __restrict Instance )
{
	// approximation, floored location while radius is ceiled, overcompensated, to use interger based visibility test
	return( !Volumetric::isRadialGridNotVisible(Instance->getLocation(), Instance->getRadius())  );
}

bool const UpdateTornado( tTime const tNow, TornadoInstance* const __restrict Instance )
{
	// Update base first (required)
	std::optional<tTime const> const tLast(Instance->Update(tNow));

	if ( std::nullopt != tLast )	// still alive?
	{
		fp_seconds const tDelta(tNow - *tLast);

		//Instance->getRotation() += tDelta.count() * 0.90005f;   // todo weird....
        Instance->getRotation() = Volumetric::RadialGridInstance::DEFAULT_ROTATION;
#ifdef DEBUG_RANGE
		static uint32_t tLastDebug;	// place holder testing
		if (tNow - tLastDebug > 250) {

			if ( Instance->RangeMax > -FLT_MAX && Instance->RangeMin < FLT_MAX ) {
			//	DebugMessage("%.03f to %.03f", Instance->RangeMin, Instance->RangeMax);
			}
			tLastDebug = tNow;
		}
#endif
		
		return(true);
	}
	
	return(false); // dead instance
}

void RenderTornado( TornadoInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic )
{	
	Volumetric::renderRadialGrid<
							Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_DOWN | Volumetric::eRadialGridRenderOptions::FOLLOW_GROUND_HEIGHT>
							(Instance, voxelDynamic); 
}
