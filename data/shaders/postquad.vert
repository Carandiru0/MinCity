#version 450 
#extension GL_GOOGLE_include_directive : enable

#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)
#include "shareduniform.glsl"
#endif

writeonly layout(location = 0) out streamOut
{
	vec2		uv;
#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)
	flat float	slice;
#endif
#if defined (SMAA_PASS_2)
	flat float  time;
	flat float  time_delta;
#endif

} Out;
#define texcoord uv // alias

void main() 
{
	const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
	Out.uv = uv;

#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)	// final neighbourhood blending subpass or volumetric resolve
	Out.slice = frame_to_slice(); // +blue noise over time (every two frames are matched as raymarch is done one frame checkerboard odd, one frame checkerboard even - the blue noise slice *must* be the same one for both frames (to not create white noise badd!!)
#endif

#ifdef SMAA_PASS_2
	Out.time = time();
	Out.time_delta = time_delta();
#endif
}