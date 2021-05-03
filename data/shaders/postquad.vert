#version 450 
#extension GL_GOOGLE_include_directive : enable
#include "shareduniform.glsl"

layout(location = 0) out streamOut
{
	vec2		uv;
#if defined (SMAA_PASS_2)
	flat float	jitter;
	flat uint	odd_frame;
#endif

#ifdef OVERLAY
	flat float time_delta;
	flat float time;
	flat uint  odd_frame;
#endif
} Out;
#define texcoord uv // alias

void main() 
{
	Out.uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(Out.uv * 2.0f - 1.0f, 0.0f, 1.0f);

#if defined (SMAA_PASS_2)	// final neighbourhood blending subpass
	const uint odd = u._uframe & 1U;
	Out.jitter = float( (int(odd) << 1) - 1 ); // -1 ... 1
	Out.odd_frame = odd;
#endif

#ifdef OVERLAY
	Out.time_delta = time_delta();
	Out.time = time();
	Out.odd_frame = u._uframe & 1U;
#endif
}