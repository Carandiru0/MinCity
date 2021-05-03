/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#ifndef TORNADO_H
#define TORNADO_H

#include "volumetricradialgrid.h"
#include <vector>

typedef struct sTornadoInstance : public Volumetric::RadialGridInstance
{
	static constexpr float const TORNADO_AMPLITUDE_MAX = 9.0f;
	
public:
	__vectorcall sTornadoInstance( FXMVECTOR const WorldCoordOrigin, float const Radius );
	
	__forceinline __declspec(noalias) virtual bool const __vectorcall op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const final;
public:
	static std::vector<Volumetric::xRow, tbb::scalable_allocator<Volumetric::xRow>> TornadoRows;

} TornadoInstance;

bool const isTornadoVisible(TornadoInstance const * const __restrict Instance );
bool const UpdateTornado( tTime const tNow, TornadoInstance* const __restrict Instance );
void RenderTornado(TornadoInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic);

#endif


