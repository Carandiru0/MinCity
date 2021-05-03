#ifndef _TRANSFORM_GLSL
#define _TRANSFORM_GLSL

// Helper Matrix->Vector functions  *** note should NOT be used in <fragment> shader stage *** (because u.view is in xyz format width, height, depth 
// normals must be normalized                           									//	and fragment shaders should be in xzy format width, depth, height)

vec3 transformNormalToViewSpace(in const mat3 view, in const vec3 normal)
{
	return( normalize(view * normal) );
}

vec3 transformPositionToViewSpace(in const mat4 view, in const vec3 position)
{
	return( (view * vec4(position, 1.0f)).xyz );
}

#endif // _TRANSFORM_GLSL
