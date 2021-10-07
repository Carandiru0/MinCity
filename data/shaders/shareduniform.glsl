#ifndef _SHAREDUNIFORM_GLSL
#define _SHAREDUNIFORM_GLSL

layout (binding = 0) restrict readonly uniform SharedUniform {
  
  mat4		_viewproj;
  mat4		_view;
  mat4      _inv_view;   // currently not used
  mat4      _proj;
  vec4		_eyePos;
  vec4		_eyeDir;	 // direction to origin(0,0,0) fixes rendering artifacts of raymarch

  vec4		_aligned_data0;  // components #define below
  vec4		_aligned_data1;  // components #define below

  uint		_uframe;	// must be last due to alignment 
} u;

// aligned data 0
vec2 fractional_offset_v2() { return(u._aligned_data0.xy); }
vec3 fractional_offset_v3() { return(vec3(u._aligned_data0.x, 0.0f, u._aligned_data0.y)); }
float time_delta() { return(u._aligned_data0.z); }
float time() { return(u._aligned_data0.w); }


// aligned data 1


#endif
