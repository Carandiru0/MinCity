#ifndef _RANDOM_GLSL
#define _RANDOM_GLSL
// hammersly Disk and square distribution functions https://www.trentreed.net/blog/physically-based-shading-and-image-based-lighting/ 

// Hammersley function (return random low-discrepency points)
vec2 Hammersley(in const int i, in const int N)  // i = iDx, N = # of sample/total iteration (used inside of loop to generate points)
{
  return vec2(
    float(i) / float(N),
    float(bitfieldReverse(i)) * 2.3283064365386963e-10f
  );
}
vec2 HammersleyDisk(in const int i, in const int N) {
    const vec2 h = 2.0f * Hammersley(i, N) - 1.0f;
    return( h * sqrt( 1.0f - 0.5f * h*h ).yx );
}

// https://www.shadertoy.com/view/NlGBWc   good quality (very low bias) hash
uint murmur3( in uint u )
{
  u ^= ( u >> 16 ); u *= 0x85EBCA6Bu;
  u ^= ( u >> 13 ); u *= 0xC2B2AE35u;
  u ^= ( u >> 16 );

  return u;
}
uvec3 murmur3( in uvec3 u )
{
  u ^= ( u >> 16 ); u *= 0x85EBCA6Bu;
  u ^= ( u >> 13 ); u *= 0xC2B2AE35u;
  u ^= ( u >> 16 );

  return u;
}

float unorm(in const uint n) { return(fract(float(n) * (1.0 / float(0xffffffffU)))); }
vec3 unorm(in const uvec3 n) { return(fract(vec3(n) * (1.0 / float(0xffffffffU)))); }

float hash11(in float m) 
{
    uint mu = floatBitsToUint(m * GOLDEN_RATIO) | 0x1u;

    return(1.0f - unorm(murmur3(mu)));
}
vec3 hash33(in vec3 m) 
{
    uvec3 mu = floatBitsToUint(m * GOLDEN_RATIO) | 0x1u;

    return(1.0f - unorm(murmur3(mu)));
}

// good random with spatial (3D) distribution
float rand(in const vec3 st) { 
  vec3 r = hash33(st) * GOLDEN_RATIO_ZERO;
  return fract(r.z * 111.111111 * 111.111111 + r.y * 111.111111 + r.x);
}

// noise begins //

// fast detailed 3d noise skewed in z direction and associated fbm, cloud functions //
float hash_noise( in const vec3 x ) {
    const vec3 p = floor(x);
    vec3 f = fract(x);
    f = f*f*(3.0f-2.0f*f);
    const float n = p.x + p.y + p.z*157.0f;
    return mix(mix( hash11(n+  0.0f), hash11(n+  1.0f),f.x),
               mix( hash11(n+157.0f), hash11(n+158.0f),f.y),f.z);
}
float swirl_fbm( in vec3 p ) {
    float f = 0.0f;
    f += 0.5000f*hash_noise( p ); p = p*2.02f;
    f += 0.2500f*hash_noise( p ); p = p*2.03f;
    f += 0.1250f*hash_noise( p ); p = p*2.01f;
    f += 0.0625f*hash_noise( p );
    
    return f * (1.0f/0.9375f);
}
float swirl_clouds(in const vec3 st, in const float time) {	// apply to mix 
	return length( swirl_fbm( st * swirl_fbm( st - 0.01f*time) + vec3(1.7f,1.2f,9.2f)+ 0.25f*time ) );
}



#endif

