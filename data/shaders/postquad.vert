#version 450 
#extension GL_GOOGLE_include_directive : enable

#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)
#include "shareduniform.glsl"
#endif

writeonly layout(location = 0) out streamOut
{
	vec2		uv;
#if defined (SMAA_PASS_2) || defined(RESOLVE)
	flat float	slice;
#endif

#ifdef OVERLAY
	flat float time_delta;
	flat float time;
	flat uint odd_frame;
#endif
} Out;
#define texcoord uv // alias

void main() 
{
	const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
	Out.uv = uv;

#if defined (SMAA_PASS_2) || defined(RESOLVE)	// final neighbourhood blending subpass
	Out.slice = float(u._uframe & 63U); // +blue noise over time
#endif

#ifdef OVERLAY
	Out.time_delta = time_delta();
	Out.time = time();
	Out.odd_frame = u._uframe & 1U;
#endif
}