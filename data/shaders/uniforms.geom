#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

#include "shareduniform.glsl"
#include "transform.glsl"

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */


layout(points) in;
layout(triangle_strip, max_vertices = 12) out;			// using gs instancing is far slower - don't use

#define FRAGMENT_OUT
#include "voxel_fragment.glsl"

#if defined(HEIGHT) // terrain
layout(location = 0) in streamIn
{
	readonly flat vec3	right, forward, up;
	readonly flat uint   adjacency;
	readonly flat vec2	world_uv;
#ifndef BASIC
	readonly flat float   ambient;
	readonly flat float	 color;
	readonly flat float   occlusion;
	readonly flat float   emission;
#endif
} In[];
#elif defined(ROAD) // road
layout(location = 0) in streamIn
{
	readonly flat vec3	right, forward, up;
	readonly flat vec4   corners;
	readonly flat vec2	world_uv;
#ifndef BASIC
	readonly flat float   ambient;
	readonly flat float   occlusion;
	readonly flat float   emission;
	readonly flat vec4    extra;
#endif
} In[];
#else  // voxels only
layout(location = 0) in streamIn
{
	readonly flat vec3	right, forward, up;
	readonly flat uint	adjacency;
#ifdef BASIC
	readonly flat vec2	world_uv;
#endif
#ifndef BASIC
	readonly flat float   ambient;
	readonly flat float	 color;
	readonly flat float   occlusion;
	readonly flat float   emission;
	readonly flat vec4    extra;
	readonly flat float	 passthru;
#endif
} In[];
#endif

#if !defined(BASIC)
// corresponding to volume dimensions
const vec3 TransformToIndexScale = vec3(2.0f, -2.0f, 2.0f);
layout (constant_id = 0) const float TransformToIndexBias_X = 0.0f;
layout (constant_id = 1) const float TransformToIndexBias_Y = 0.0f;
layout (constant_id = 2) const float TransformToIndexBias_Z = 0.0f;
#define TransformToIndexBias vec3(TransformToIndexBias_X, TransformToIndexBias_Y, TransformToIndexBias_Z)
layout (constant_id = 3) const float InvToIndex_X = 0.0f;
layout (constant_id = 4) const float InvToIndex_Y = 0.0f;
layout (constant_id = 5) const float InvToIndex_Z = 0.0f;
#define InvToIndex vec3(InvToIndex_X, InvToIndex_Y, InvToIndex_Z)

#if (defined(HEIGHT) || defined(ROAD)) // terrain, roads

layout (constant_id = 6) const float HALF_TEXEL_OFFSET_U = 0.0f;
#endif
#if defined(HEIGHT)
#define HALF_TEXEL_OFFSET_TEXTURE HALF_TEXEL_OFFSET_U.xx
#endif
#if defined(ROAD)
layout (constant_id = 7) const float HALF_TEXEL_OFFSET_V = 0.0f;
#define HALF_TEXEL_OFFSET_TEXTURE vec2(HALF_TEXEL_OFFSET_U, HALF_TEXEL_OFFSET_V)
#endif

#endif

#ifdef ROAD
layout (constant_id = 8) const float ROAD_WIDTH = 0.0f;
#endif

#ifndef ROAD
const uint BIT_ADJ_ABOVE = (1<<0),				
		   BIT_ADJ_BACK = (1<<1),				
		   BIT_ADJ_FRONT = (1<<2),				
		   BIT_ADJ_RIGHT = (1<<3),				
		   BIT_ADJ_LEFT = (1<<4);
#endif

#if defined(ROAD)
// road only
#if !defined(IsNotAdjacent)
#define GEO_FLATTEN [[flatten]]
#define IsNotAdjacent(adjacent) ( true )  //adjacency flag not used (yet) for roads
#endif

#else
// vxl & terrain vxl
#if !defined(IsNotAdjacent)
#define GEO_FLATTEN [[dont_flatten]]
#define IsNotAdjacent(adjacent) ( 0 == (In[0].adjacency & adjacent) )
#endif

#endif

#if !defined(IsVisible)
#define IsVisible(normal) ( dot(normal, u._eyeDir.xyz) >= 0.0f )
#endif

vec3 PerQuad(in vec3 normal)
{
	normal = normalize(normal);

#if !defined(BASIC)
	// final output must be xzy and transformed to eye/view space for fragment shader
	Out.N.xzy = transformNormalToViewSpace(mat3(u._view), normal);  // mat3 of view only required to transform a normal / direction vector
#endif

	return normal;	// normal at this point is still xyz, but used internally for calculating offset / corner of worldPos
					// this is fixed later for fragment output of xzy
}

void EmitVxlVertex(in const vec3 center, in const vec3 tangent)
{
	vec3 worldPos = center + tangent;

#if defined(HEIGHT)
	worldPos.y = min(worldPos.y, 0.0f);	// bugfix: clip to zero plane for ground so it doesn't extend downwards incorrectly
#endif
#if !defined(BASIC)
	// main uvw coord for light, common to terrain, road & normal voxels
	Out.uv.xzy = fma(TransformToIndexScale, worldPos, TransformToIndexBias) * InvToIndex;

	// final output must be xzy and transformed to eye/view space for fragment shader
	// the worldPos is transformed to view space, in view space eye is always at origin 0,0,0
	// so no need for eyePos

	Out.V.xzy = transformNormalToViewSpace(mat3(u._view), normalize(u._eyePos.xyz - worldPos));	 // transforming a direction does not have any position information from view matrix, so the fractional offset it contains is not inadvertently added here.
#endif																		   // becoming the vec3(0,0,0) - worldPos  view direction vector
	
	gl_Position = u._viewproj * vec4(worldPos, 1.0f); // this remains xyz, is not output to fragment shader anyways
	EmitVertex();
}

void BeginQuad(in const vec3 center, in const vec3 right, in const vec3 up, in const vec3 normal) 
{	
#if !defined(BASIC)

#if defined(HEIGHT) 
	const vec2 texel_texture = HALF_TEXEL_OFFSET_TEXTURE; // careful....
	const vec2 uv_center_texture = fma( normal.xz, texel_texture, In[0].world_uv.xy);
#endif
#if defined(ROAD)
	const vec2 texel_road_texture = vec2(0.5f, ROAD_WIDTH * 0.5f);
#endif

#endif // not basic
	
#ifdef ROAD
#ifndef BASIC
	const float rotate = In[0].extra.y;
	const vec2 texel_texture = mix(HALF_TEXEL_OFFSET_TEXTURE.yx, HALF_TEXEL_OFFSET_TEXTURE.xy, rotate); // careful....
	const vec2 uv_center_texture = fma( normal.xz, texel_texture, In[0].world_uv.xy);
#endif
	const vec4 corners = In[0].corners; 
#endif

{ // right - up vertex
	vec3 tangent = right - up;
#ifdef ROAD
	tangent.y += corners.x;
#endif
#if !defined(BASIC)

#if defined(HEIGHT) || defined(ROAD)
	Out.world_uv.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture); 
#endif
#ifdef ROAD
	Out.road_uv.xy = (normalize(mix(tangent.xz, tangent.zx, rotate))) * texel_road_texture + 0.5f;
#endif
#endif // not basic

	EmitVxlVertex(center, tangent);

}

{ // -right - up vertex
	vec3 tangent = -right - up;
#ifdef ROAD
	tangent.y += corners.y;
#endif
#if !defined(BASIC)
	 
#if defined(HEIGHT) || defined(ROAD)
	Out.world_uv.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture); 
#endif
#ifdef ROAD
	Out.road_uv.xy = (normalize(mix(tangent.xz, tangent.zx, rotate))) * texel_road_texture + 0.5f;
#endif
#endif // not basic

	EmitVxlVertex(center, tangent);

}

{ // right + up vertex
	vec3 tangent = right + up;
#ifdef ROAD
	tangent.y += corners.z;
#endif
#if !defined(BASIC)
	
#if defined(HEIGHT) || defined(ROAD)
	Out.world_uv.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture); 
#endif
#ifdef ROAD 
	Out.road_uv.xy = (normalize(mix(tangent.xz, tangent.zx, rotate))) * texel_road_texture + 0.5f;
#endif
#endif // not basic

	EmitVxlVertex(center, tangent);

}

{ // -right + up vertex
	vec3 tangent = -right + up;
#ifdef ROAD
	tangent.y += corners.w;
#endif
#if !defined(BASIC)
	
#if defined(HEIGHT) || defined(ROAD)
	Out.world_uv.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture);
#endif
#ifdef ROAD 
	Out.road_uv.xy = (normalize(mix(tangent.xz, tangent.zx, rotate))) * texel_road_texture + 0.5f;
#endif
#endif // not basic

	EmitVxlVertex(center, tangent);
}

	EndPrimitive();

} // end BeginQuad


// GEOMETRY - - - - In = xyz world space
//					all calculation in this shader remain in xyz world space
//			--------Out = xzy view space
void main() {
	
	const vec3 center = gl_in[0].gl_Position.xyz;
	const vec3 right = In[0].right;
    const vec3 up = In[0].up;
	const vec3 forward = In[0].forward;

	// ** Per voxel ops *** //
#ifndef BASIC
	Out.ambient = In[0].ambient;
#ifdef _color
	Out._color = In[0].color;
#endif
#ifdef _occlusion
	Out._occlusion = In[0].occlusion;
#endif
#ifdef _emission
	Out._emission = In[0].emission;
#endif

#ifdef ROAD
	Out.road_uv.z = In[0].extra.x; // road tile index in .z, which is good for indexing texture array nicely as special.xy already contains uv coords
#endif
	
#if !(defined(HEIGHT) || defined(ROAD))
	Out._passthru = In[0].passthru; // passthru extra data ****************************************************************************
#endif

#ifdef _time
	Out._time = time();
#endif

#ifdef TRANS

#ifdef _transparency
	Out._transparency = In[0].extra.z; // transparency
#endif
#ifdef _weight_maximum
	Out._inv_weight_maximum = In[0].extra.w; // reserved (currently used for passing maximum value for transparency weight normalization) 
#endif

#endif

#else // basic

	Out._voxelIndex = In[0].world_uv.xy; // normalized world grid uv coords

#endif  // basic
	
#ifndef ROAD // only top quad is required for roads
	// RIGHT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_RIGHT) ) {
#define _normal right
		const vec3 normal = PerQuad(_normal);
		
		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, up, forward, normal);
		}
#undef _normal
	} 
	// LEFT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_LEFT) ) {
#define _normal -right              
		const vec3 normal = PerQuad(_normal);
		
		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, forward, up, normal); 
		}
#undef _normal
	} // else

	// FRONT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_FRONT) ) {
#define _normal -forward
		const vec3 normal = PerQuad(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, up, right, normal);
		}
#undef _normal
	} 
	// BACK
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_BACK) ) {
#define _normal forward
		const vec3 normal = PerQuad(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, up, normal);
		}
#undef _normal
	} // else

	//// not rendering (bottom)

#endif // #### not road

	// UP
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_ABOVE) ) {
#define _normal -up
		const vec3 normal = PerQuad(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, forward, normal);			
		}
#undef _normal
	}

// endprimitive is inherent at end of shader, so it does not need to be explicitly called
// it is only used when multiple primitives are emitted by geometry shader
}

