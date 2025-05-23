/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

// DEPRECATED - OBSOLETE - USE VDB's w/ custom shader //
/*
#ifndef RAIN_H
#define RAIN_H

#include "volumetricradialgrid.h"
#include <Imaging/Imaging/Imaging.h>
#include <vector>

typedef struct sRainInstance : public Volumetric::RadialGridInstance
{
public:
    enum eRainImageType
    {
        DISTANCE = 0,

        SPEED,

        COUNT
    };
public:


public:
	__vectorcall sRainInstance(FXMVECTOR const WorldCoordOrigin, float const Radius);
    ~sRainInstance();

    __forceinline __declspec(noalias) virtual bool const __vectorcall op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const final;

public:
	static vector<Volumetric::xRow> RainRows;
    static Imaging     _imgRain[eRainImageType::COUNT];

private:

} RainInstance;

bool const UpdateRain(tTime const tNow, RainInstance* const __restrict Instance);
void RenderRain(RainInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic);

#endif

*/
