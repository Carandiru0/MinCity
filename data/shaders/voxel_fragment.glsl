#ifndef VOXEL_FRAGMENT_
#define VOXEL_FRAGMENT_

//#ifndef BASIC

#ifdef FRAGMENT_OUT
layout(location = 0) out streamOut   // in/out to pixel shader (all members must be vec4)
{
	vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	vec4 N;
	vec4 V;
	vec4 special;		
	flat vec4 extra;	
	flat vec3 ambient;
#ifdef ROAD
	vec3 uv_local;
#endif

} Out;
#else
layout(location = 0) in streamIn   // in/out to pixel shader (all members must be vec4)
{
	vec4 uv;			// uv:  xyz always reserved for light volume uv relative coords
	vec4 N;
	vec4 V;
	vec4 special;		
	flat vec4 extra;	
	flat vec3 ambient;
#ifdef ROAD
	vec3 uv_local;
#endif
} In;
#endif



#define i_extra_00 uv.w			// *                          -special case careful this is also reserved entire uv vector (uv.xyzw) pass thru to fragment shader
#define i_extra_0 N.w			// *						  -occlusion
#define i_extra_1 V.w			// *						  -emission
#define i_extra_2 special.x		// *						  ------------------------------------------ -x-
#define i_extra_3 special.y		// *						  - uv texture coord (xyz)  or  color (rgb)  -y-
#define i_extra_4 special.z		// *						  --------^-------^------^------^------^---- -z-
#define i_extra_5 special.w		// --------- free ------------
#define f_extra_0 extra.x		// *						  - time
#define f_extra_1 extra.y		// --------- free ------------
#define f_extra_2 extra.z		// *                          - transparency
#define f_extra_3 extra.w		// *						  - pass thru inverse weight maximum (transparency weighting max for this frame)

// i_ = interpolated by fragment shader
// f_ = flat, not interpolated ""   ""

// defaults //
#define _passthru i_extra_00		// could be packed color(voxel model), distance(volumetric radial grid voxel), etc.
#define _occlusion i_extra_0
#define _emission i_extra_1
#define _uv_texture special.xyz
#define _color special.xyz
#undef i_extra_2
#undef i_extra_3
#undef i_extra_4

// specializations
#ifdef TRANS  
#define _transparency f_extra_2
#define _inv_weight_maximum f_extra_3
#define _time f_extra_0

#endif // TRANS

#ifdef ROAD
#undef _occlusion
#undef _transparency

#endif // ROAD


#if defined(HEIGHT) || defined(T2D)
#undef _extra_data

#endif // HEIGHT or T2D


#ifdef BASIC

#if (defined(HEIGHT) || defined(T2D) || defined(ROAD))
#define _voxelIndex extra.xy
#endif

#endif

#endif // VOXEL_FRAGMENT_

