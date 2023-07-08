#ifndef _SHAREDBUFFER_GLSL
#define _SHAREDBUFFER_GLSL

#ifndef BINDING
#define BINDING 1
#endif

#if defined(WRITEONLY)
layout(std430, set=0, binding=BINDING) writeonly restrict buffer SharedBuffer
#elif defined(READWRITE)
layout(std430, set=0, binding=BINDING) restrict buffer SharedBuffer
#else
layout(std430, set=0, binding=BINDING) readonly restrict buffer SharedBuffer
#endif
{ 
  vec4 average_reflection_color;	// only rgb components matter, .a is unused
  
  uint average_reflection_count;
  uint new_image_layer_count_max;
} b;

#endif
