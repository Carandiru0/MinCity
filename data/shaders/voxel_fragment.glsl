#ifndef VOXEL_FRAGMENT_
#define VOXEL_FRAGMENT_


#ifdef FRAGMENT_OUT
writeonly layout(location = 0) out streamOut   // in/out to pixel shader (all members must be vec4)
{
	vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	vec4 N;
	vec4 V;	
	flat vec4 extra;	
#ifndef BASIC
#if defined(HEIGHT) || defined(ROAD)
	vec2 world_uv;
#ifdef ROAD
	vec3 road_uv;
#endif
#else // voxels only
	flat vec4 material;
#endif
#endif
} Out;
#else
layout(location = 0) in streamIn   // in/out to pixel shader (all members must be vec4)
{
	readonly vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	readonly vec4 N;
	readonly vec4 V;
	readonly flat vec4 extra;	
#ifndef BASIC
#if defined(T2D) || defined(ROAD)
	readonly vec2 world_uv;
#ifdef ROAD
	readonly vec3 road_uv;
#endif
#else // voxels only
	readonly flat vec4 material;
#endif
#endif
} In;
#endif



#define i_extra_00 uv.w			// *                          - free (unused)
#define i_extra_0 N.w			// *						  - ambient (only used when road or terrain, normal voxels have material)
#define i_extra_1 V.w			// *						  - emission (only used when road or terrain, normal voxels have material)
#define f_extra_0 extra.x		// *						  - packed color
#define f_extra_1 extra.y		// *						  - transparency
#define f_extra_2 extra.z		// *                          - pass thru inverse weight maximum (transparency weighting max for this frame)
#define f_extra_3 extra.w		// *						  - time

// i_ = interpolated by fragment shader
// f_ = flat, not interpolated ""   ""

// defaults //
// free not used anymore #define _passthru i_extra_00		// could be packed color(voxel model), distance(volumetric radial grid voxel), etc.
#define _ambient i_extra_0
#define _emission i_extra_1
#define _color f_extra_0
#define _time f_extra_3
#undef i_extra_2
#undef i_extra_3
#undef i_extra_4

// specializations
#ifdef TRANS  

#define _transparency f_extra_1
#define _inv_weight_maximum f_extra_2

#endif // TRANS

#ifdef ROAD

#undef _transparency
#undef _color

#endif // ROAD


#if defined(HEIGHT) || defined(T2D)
#undef _extra_data

#endif // HEIGHT or T2D


#ifdef BASIC

#define _voxelIndex extra.xy

#endif

#endif // VOXEL_FRAGMENT_

