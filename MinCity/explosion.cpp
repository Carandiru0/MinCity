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
#include "explosion.h"
#include "volumetricradialgrid.h"
#include <Noise/supernoise.hpp>

#include "MinCity.h"
#include "cVoxelWorld.h"

#include <vector>

static constexpr milliseconds const LIFETIME_ANIMATION = milliseconds(6000);
static float constexpr BASE_PATTERN_SCALE = 0.4132f;

inline vector<Volumetric::xRow>	ExplosionInstance::ExplosionRows;  // vector memory is persistance across instances
																																																										 // only one shockwave instance is active at a time
																																																										 // this improves performance and stability
																																																										 // by avoiding reallocation of vector
																																																										 // for any new or deleted shockwave instance
__vectorcall sExplosionInstance::sExplosionInstance( FXMVECTOR const WorldCoordOrigin, float const Radius)
		: sRadialGridInstance(WorldCoordOrigin, Radius, LIFETIME_ANIMATION, ExplosionRows),
	PatternModSeedScaled((PsuedoRandomFloat() * 2.0f - 1.0f)* EXPLOSION_NOISE_PATTERN_SCALAR)
{
    setScale(Radius * Iso::VOX_SIZE);

}
STATIC_INLINE_PURE float const sdSphere(float const r, float const s)	// signed distance function for a sphere
{
	// v2_length(p) - s
	return(r - s);
}

STATIC_INLINE_PURE float const __vectorcall getSpiralNoise3D(FXMVECTOR scale, FXMVECTOR displacement, float t_)
{
	XMVECTOR const parameters = SFM::__fma(scale, displacement, _mm_set1_ps(0.5f*t_));

	XMFLOAT3A vParameters;
	XMStoreFloat3A(&vParameters, parameters);

	return(supernoise::getSpiralNoise3D(vParameters.x, vParameters.y, vParameters.z, t_));
}

// op does not r/w any global memory voxel_op_fnDispersion
__forceinline __declspec(noalias) bool const __vectorcall sExplosionInstance::op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const
{
	float const fObjectMaxRadius(getRadius() * 0.01f + t_ * 0.1f);
	float const fRadius(XMVectorGetX(XMVector2Length(vDisplacement)));

	float fDistance = sdSphere(fRadius, fObjectMaxRadius);

	//if (fDistance >= 0.0f)
	//	return(false);

	float const spheres_distance = /*(fObjectMaxRadius - fRadius * 0.25f) */
		getSpiralNoise3D(_mm_set1_ps(BASE_PATTERN_SCALE + PatternModSeedScaled),
			XMVectorSetZ(vDisplacement, fRadius),
			t_);

 	out.setColor(cMinCity::VoxelWorld->blackbody(1.0f - 0.333f * SFM::abs(spheres_distance - fDistance)));
	//out.setColor(uvec4_v(uvec4_t{ 0,0,0xFF,0 }));

	bool const in_a_sphere = (spheres_distance < fDistance);
	float depth = fRadius / fObjectMaxRadius;
	out.lit = in_a_sphere;
	out.column_sz = 6;// SFM::round_to_u32(depth);
	//out.transparency = 0.1f;

	fDistance += fDistance * spheres_distance * -0.25f;

	out.distance = fDistance;
	return(true);
}
 
bool const isExplosionVisible(ExplosionInstance const* const __restrict Instance )
{
	// approximation, floored location while radius is ceiled, overcompensated, to use interger based visibility test
	return( !Volumetric::isRadialGridNotVisible(Instance->getLocation(), Instance->getRadius())  );
}

bool const UpdateExplosion( tTime const tNow, ExplosionInstance* const __restrict Instance )
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

void RenderExplosion( ExplosionInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic )
{	
	Volumetric::renderRadialGrid<
						    Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_DOWN /*| Volumetric::eRadialGridRenderOptions::FOLLOW_GROUND_HEIGHT*/>
							(Instance, voxelDynamic); 
}

