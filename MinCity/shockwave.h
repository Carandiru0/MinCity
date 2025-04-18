/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#ifndef SHOCKWAVE_H
#define SHOCKWAVE_H

#include "volumetricradialgrid.h"
#include <vector>

typedef struct sShockwaveInstance : public Volumetric::RadialGridInstance
{
	static constexpr float const SHOCKWAVE_AMPLITUDE_MAX = 9.0f;
	
public:
	__vectorcall sShockwaveInstance( FXMVECTOR const WorldCoordOrigin, float const Radius );
	
	__forceinline __declspec(noalias) virtual bool const __vectorcall op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const final;
public:
	static vector<Volumetric::xRow> ShockwaveRows;

} ShockwaveInstance;

bool const isShockwaveVisible( ShockwaveInstance const * const __restrict Instance );
bool const UpdateShockwave( tTime const tNow, ShockwaveInstance* const __restrict Instance );
void RenderShockwave( ShockwaveInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic);

#endif


