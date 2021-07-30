/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#ifndef EXPLOSION_H
#define EXPLOSION_H

#include "volumetricradialgrid.h"
#include <vector>

typedef struct sExplosionInstance : public Volumetric::RadialGridInstance
{
	static constexpr float const EXPLOSION_AMPLITUDE_MAX = 64.0f,
								 EXPLOSION_NOISE_PATTERN_SCALAR = 10.5f;
	
public:
	__vectorcall sExplosionInstance( FXMVECTOR const WorldCoordOrigin, float const Radius );
	
	__forceinline __declspec(noalias) virtual bool const __vectorcall op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const final;

private:
	float const PatternModSeedScaled;
public:
	static vector<Volumetric::xRow> ExplosionRows;

} ExplosionInstance;

bool const isExplosionVisible(ExplosionInstance const * const __restrict Instance );
bool const UpdateExplosion( tTime const tNow, ExplosionInstance* const __restrict Instance );
void RenderExplosion(ExplosionInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic);

#endif


