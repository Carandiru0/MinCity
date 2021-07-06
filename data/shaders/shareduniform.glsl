#ifndef _SHAREDUNIFORM_GLSL
#define _SHAREDUNIFORM_GLSL

layout (binding = 0) restrict readonly uniform SharedUniform {
  
  mat4		_viewProj;
  mat4		_view;
  mat4      _inv_view;
  mat4      _proj;
  vec4		_eyePos;
  vec4		_eyeDir;	// direction to origin(0,0,0) fixes rendering artifacts of raymarch

  vec4		_aligned_data0;  // components #define below
  vec4		_aligned_data1;  // components #define below

  uint		_uframe;	// must be last due to alignment 
} u;

// aligned data 0
vec2 fract_offset() { return(u._aligned_data0.xy); }
float time_delta() { return(u._aligned_data0.z); }
float time() { return(u._aligned_data0.w); }


// aligned data 1


#endif
