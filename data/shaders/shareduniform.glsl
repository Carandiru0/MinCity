#ifndef _SHAREDUNIFORM_GLSL
#define _SHAREDUNIFORM_GLSL

layout (binding = 0) restrict readonly uniform SharedUniform {
  
  precise mat4      _proj;
  precise mat4		_view; 

  precise vec4		_eyePos;
  precise vec4		_eyeDir;	 // direction to origin(0,0,0) fixes rendering artifacts of raymarch

  precise vec4		_aligned_data0;  // components #define below

  uint		_uframe;	// must be last due to alignment 
} u;

// aligned data 0
precise vec2  fractional_offset() { return(u._aligned_data0.xy); }
precise float time_delta() { return(u._aligned_data0.z); }
precise float time() { return(u._aligned_data0.w); }


// aligned data 1

// for consistent bluenoise slice selection across frames
float frame_to_slice() // achieves pairing of frames and covers distribution of frames uniformly (tested)
{
	const uint frame = u._uframe;
	const uint frame_n = (frame & 63u);

	float slice = float(bool(frame_n & 1u) ? frame_n - 1 : frame_n);
	slice += float(frame) / 64.0f;
	slice = round(slice);

	return( float((uint(slice) & 63u)) );
}

#endif
