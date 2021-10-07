#ifndef VOXEL_FRAGMENT_
#define VOXEL_FRAGMENT_

//#ifndef BASIC

#ifdef FRAGMENT_OUT
writeonly layout(location = 0) out streamOut   // in/out to pixel shader (all members must be vec4)
{
	vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	vec4 N;
	vec4 V;	
	flat vec4 extra;	
	flat float ambient;
#if defined(HEIGHT) || defined(ROAD)
	vec2 world_uv;
#ifdef ROAD
	vec3 road_uv;
#endif
#endif
} Out;
#else
readonly layout(location = 0) in streamIn   // in/out to pixel shader (all members must be vec4)
{
	vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	vec4 N;
	vec4 V;	
	flat vec4 extra;	
	flat float ambient;
#if defined(T2D) || defined(ROAD)
	vec2 world_uv;
#ifdef ROAD
	vec3 road_uv;
#endif
#endif
} In;
#endif



#define i_extra_00 uv.w			// *                          -special case careful this is also reserved entire uv vector (uv.xyzw) pass thru to fragment shader
#define i_extra_0 N.w			// *						  -occlusion
#define i_extra_1 V.w			// *						  -emission
#define f_extra_0 extra.x		// *						  - packed color
#define f_extra_1 extra.y		// *						  - transparency
#define f_extra_2 extra.z		// *                          - pass thru inverse weight maximum (transparency weighting max for this frame)
#define f_extra_3 extra.w		// *						  - time

// i_ = interpolated by fragment shader
// f_ = flat, not interpolated ""   ""

// defaults //
#define _passthru i_extra_00		// could be packed color(voxel model), distance(volumetric radial grid voxel), etc.
#define _occlusion i_extra_0
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

#undef _occlusion
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

