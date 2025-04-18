#ifndef _SHAREDUNIFORM_GLSL
#define _SHAREDUNIFORM_GLSL

layout (binding = 0) restrict readonly uniform SharedUniform {
  
  precise mat4      _proj;
  precise mat4		_view; 
  precise mat4		_world; 

  precise vec4		_eyePos;
  precise vec4		_eyeDir;	 // direction to origin(0,0,0) fixes rendering artifacts of raymarch

  precise vec4		_aligned_data0;  // components #define below

  uint		_uframe;	// must be last due to alignment 
} u;

// aligned data 0
precise vec2  fractional_offset() { return(u._aligned_data0.xy); }
precise float time_delta() { return(u._aligned_data0.z); }
precise float time() { return(u._aligned_data0.w); }
uint          frame() { return(u._uframe); }

// aligned data 1

// for consistent bluenoise slice selection across frames - this is done so that there are NOT two distinct bluenoise between 2 frames, as the pixels are reconstructed in the checkerboard resolve every frame. This keeps the bluenoiuse consistent over time, not degenerating into white noise.
float frame_to_slice(in const float odd) // achieves pairing of frames and covers distribution of frames *uniformly* (tested)
{                                   // pass 1.0f for odd to have a unique slice index every frame, only for final post AA pass
	const uint frame_ = u._uframe;
	const uint frame_n = (frame_ & 63u);

	float slice = float(bool(frame_n & 1u) ? frame_n - 1 : frame_n);
	slice += float(frame_) / 64.0f;
	//slice = round(slice); // *bugfix - this was causing errors, taken out produces perfect distribution
	slice += odd;
	
	return( float((uint(slice) & 63u)) );
}

#endif
