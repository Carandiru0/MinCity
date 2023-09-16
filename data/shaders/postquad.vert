#version 450 
#extension GL_GOOGLE_include_directive : enable

#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)
#include "shareduniform.glsl"
#endif

#if !defined (DEPTH)
writeonly layout(location = 0) out streamOut
{
	vec2		uv;
#if defined (SMAA_PASS_2) || defined(RESOLVE) || defined(OVERLAY)
	flat float	slice;
#endif

} Out;
#define texcoord uv // alias
#endif

void main() 
{
	const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);

#if !defined (DEPTH)
	Out.uv = uv;

#if defined(RESOLVE) || defined(OVERLAY)	// final neighbourhood blending subpass or volumetric resolve
	Out.slice = frame_to_slice(0.0f); // +blue noise over time (every two frames are matched as raymarch is done one frame checkerboard odd, one frame checkerboard even - the blue noise slice *must* be the same one for both frames (to not create white noise badd!!)
#endif

#if defined (SMAA_PASS_2)
	Out.slice = frame_to_slice(float(bool(frame() & 1u))); // allow capture of frame oddity - allows *only* in the final post AA pass, for unique bluenoise to be used every frame
#endif
#endif

}