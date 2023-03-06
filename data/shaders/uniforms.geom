#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#include "shareduniform.glsl"
#include "transform.glsl"
#include "common.glsl"

// Attention:
//
// "Vulkan Rendering is different, concerning the Y Axis. Instead of starting at the bottom of the screen and working up (+Y), we have to start at the top of the screen and work down (-Y)
//  This affects everything, everything is upside down - from uv's to normals. This creates a lot of confusion, and is something of a pain in the ass to work with. Even the standard cartesian coordinate system has Y Up as Positive.
//  Unfortuanetly Y Up is NEGATIVE." - Jason Tully
//
// "If Vulkan didn't negate the Y Axis, it would be the perfect 3D api. Whoever decided to do this has very little understanding of math."
//
//

layout(points) in;					// maximum 3 faces/quads visible of any cube/voxel
layout(triangle_strip, max_vertices = 12) out;			// using gs instancing is far slower - don't use

#define FRAGMENT_OUT
#include "voxel_fragment.glsl"

#if defined(HEIGHT) // terrain
layout(location = 0) in streamIn
{
	readonly flat vec3	right, forward, up;
	readonly flat uint  adjacency;
	readonly flat vec4	world_uvw;
#ifndef BASIC
	readonly flat float   ambient;
	readonly flat float	  color;
	readonly flat float   emission;
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
	readonly flat float	  color;
	readonly flat vec4    material;
	readonly flat vec4    extra;
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

#if defined(HEIGHT) // terrain

layout (constant_id = 6) const float HALF_TEXEL_OFFSET_U = 0.0f;
layout (constant_id = 7) const float HALF_TEXEL_OFFSET_V = 0.0f;
#define HALF_TEXEL_OFFSET_TEXTURE vec2(HALF_TEXEL_OFFSET_U, HALF_TEXEL_OFFSET_V)

#include "terrain.glsl"

#endif

#endif

const uint BIT_ADJ_BELOW = (1<<0),
		   BIT_ADJ_ABOVE = (1<<1),
		   BIT_ADJ_BACK  = (1<<2),				
		   BIT_ADJ_FRONT = (1<<3),				
		   BIT_ADJ_RIGHT = (1<<4),				
		   BIT_ADJ_LEFT  = (1<<5);

// vxl & terrain vxl
#if !defined(IsNotAdjacent)
#define GEO_FLATTEN [[dont_flatten]]
#define IsNotAdjacent(adjacent) ( 0 == (In[0].adjacency & adjacent) )
#endif


#if !defined(IsVisible)
#define IsVisible(normal) ( dot(normal, u._eyeDir.xyz) >= 0.0f )
#endif

void PerVoxel()
{
#ifndef BASIC
	
#ifdef _color
	Out._color = In[0].color;
#endif

#if !defined(HEIGHT) // voxels only
	Out.material = In[0].material; 
#else
	Out._ambient = In[0].ambient;
#ifdef _emission
	Out._emission = In[0].emission;
#endif
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
#if defined(HEIGHT)
    Out.voxelIndex = In[0].world_uvw.xy; // normalized world grid uv coords
#else
	Out.voxelIndex = In[0].world_uv.xy; // normalized world grid uv coords
#endif
#endif  // basic
}

void PerQuad(in const vec3 normal)
{
	Out.N.xzy = normal; // require world space normal for normal map output. (negating whole normal matches up with computed normal in volumetric shader)
}

void EmitVxlVertex(in vec3 worldPos, in const vec3 normal)
{
	// *bugfix - on amd we can get away with only setting these once per voxel, once per quad
	// on nvidia, it's strict to the spec that all output values must be set on every vertex emission -like they are reset every EmitVertex()
	// must re-initialize all output values for next vertex
	PerVoxel();
	PerQuad(normal);  
	
#if defined(HEIGHT) 
	worldPos.y = min(worldPos.y, In[0].world_uvw.w);	// bugfix: clip to zero plane for ground so it doesn't extend downwards incorrectly (default), or *new* calculated minimum height from neighbours (conditional on nonzero normalized heightstep)
#endif
	gl_Position = u._viewproj * vec4(worldPos, 1.0f); // this remains xyz, is not output to fragment shader anyways
	
#if !defined(BASIC)
	
	// main uvw coord for light, common to terrain, normal voxels
															// *bugfix - half-voxel offset required to sample center of voxel
	Out.uv.xzy = fma(TransformToIndexScale, worldPos, TransformToIndexBias) * InvToIndex;

	// final output must be xzy and transformed to eye/view space for fragment shader
	// the worldPos is transformed to view space, in view space eye is always at origin 0,0,0
	// so no need for eyePos
														  // *bugfix - V  is the "incident" vector, pointing towards this vertex from the camera eye point, it is not the same as the vector eyeDir, which is from eye to look at point (the origin 0,0,0)
	Out.V.xzy = /*transformNormalToViewSpace(mat3(u._view),*/ normalize(u._eyePos.xyz - worldPos);//);	 // transforming a direction does not have any position information from view matrix, so the fractional offset it contains is not inadvertently added here.
																		   // becoming the vec3(0,0,0) - worldPos  view direction vector
#endif

	EmitVertex();
}

void BeginQuad(in const vec3 center, in const vec3 right, in const vec3 up, in const vec3 normal
#if !defined(BASIC) && defined(HEIGHT)
	, in const vec2 min_max_height
#endif
)
{	
#if !defined(BASIC)

#if defined(HEIGHT) 
	const vec2 texel_texture = HALF_TEXEL_OFFSET_TEXTURE; // careful....
	const vec2 uv_center_texture = fma( normal.xz, texel_texture, In[0].world_uvw.xy);  
#endif

#endif // not basic
	
	// 2+<--------+1
	//  |\\       |
	//  | \\\\    |
	//  |     \\\ |
	//  |        \|
	// 4+<--------+3
	
{ // right - up vertex
	vec3 tangent = right - up;

#if !defined(BASIC)

#if defined(HEIGHT)
	Out.world_uvw.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture);
	Out.world_uvw.z = min_max_height.y;
#endif

#endif // not basic

	EmitVxlVertex(center + tangent, normal);

}

{ // -right - up vertex
	vec3 tangent = -right - up;

#if !defined(BASIC)
	 
#if defined(HEIGHT)
	Out.world_uvw.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture); 
	Out.world_uvw.z = min_max_height.y;
#endif

#endif // not basic

	EmitVxlVertex(center + tangent, normal);

}

{ // right + up vertex
	vec3 tangent = right + up;

#if !defined(BASIC)
	
#if defined(HEIGHT)
	Out.world_uvw.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture); 
	Out.world_uvw.z = min_max_height.x;
#endif

#endif // not basic

	EmitVxlVertex(center + tangent, normal);

}

{ // -right + up vertex
	vec3 tangent = -right + up;

#if !defined(BASIC)
	
#if defined(HEIGHT)
	Out.world_uvw.xy = fma( normalize(tangent.xz), texel_texture, uv_center_texture);
	Out.world_uvw.z = min_max_height.x;
#endif

#endif // not basic

	EmitVxlVertex(center + tangent, normal);

	EndPrimitive(); // *bugfix: if endprimitive is not used per quad - there are cases where the voxel trianglestrip is not drawing a "face" of the cube
					//          which is very hard to isolate as its rare, < 2% of all voxels drawn for exammple with ground. 
					//			by emitting a unique triangle strip quad, 3 times individually for the voxel's faces this problem is avoided
					//			the optimal vertex count emission per primitive from a geometry shader is actually 4.
					//			so this works out really well!
}

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

	// DOT NOT CHANGE ORDER OF QUADS OR VERTICES - ORDER IS IMPORTANT //

	// UP
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_ABOVE) ) {
#define _normal -up
		const vec3 normal = normalize(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, forward, normal
#if !defined(BASIC) && defined(HEIGHT)
			, vec2(-In[0].world_uvw.z)
#endif
			);
		}
#undef _normal
	}

#ifndef HEIGHT // not terrain - No bottoms on Terrain
	// DOWN
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_BELOW) ) {
#define _normal up
		const vec3 normal = normalize(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, -forward, normal);
		}
#undef _normal
	}
#endif // #### not terrain

	// BACK
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_BACK) ) {
#define _normal forward
		const vec3 normal = normalize(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, up, normal
#if !defined(BASIC) && defined(HEIGHT)
			, vec2(0.0f, -In[0].world_uvw.z)
#endif
			);
		}
#undef _normal
	}

	// FRONT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_FRONT) ) {
#define _normal -forward
		const vec3 normal = normalize(_normal);

		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, right, -up, normal
#if !defined(BASIC) && defined(HEIGHT)
			, vec2(0.0f, -In[0].world_uvw.z)
#endif
			);
		}
#undef _normal
	}

	// LEFT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_LEFT) ) {
#define _normal -right              
		const vec3 normal = normalize(_normal);
		
		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, forward, up, normal
#if !defined(BASIC) && defined(HEIGHT)
			, vec2(0.0f, -In[0].world_uvw.z)
#endif
			);
		}
#undef _normal
	}

	// RIGHT
	GEO_FLATTEN if ( IsNotAdjacent(BIT_ADJ_RIGHT) ) {
#define _normal right
		const vec3 normal = normalize(_normal);
		
		[[dont_flatten]] if ( IsVisible(normal) ) {
			BeginQuad(center + _normal, forward, -up, normal
#if !defined(BASIC) && defined(HEIGHT)
			, vec2(0.0f, -In[0].world_uvw.z)
#endif
			);
		}
#undef _normal
	} 


// visible faces of a voxel is always 3 at any point of time, so only half the number of vertices in a cube actually need to be emitted from geometry shade, as a single primitive.

// maximum 3 quads generated for any given input point.
// 4 vertices/quad  - >  12 vertices/voxel
}

