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


// 2D Weyl hash 32-bit XOR  - https://www.shadertoy.com/view/4dlcR4 

#define _W0 0x3504f335u   
#define _W1 0x8fc1ecd5u  
#define _W2 0xbb67ae85u
#define _W3 0xf1bbcdcbu

// 741103597u, 1597334677u, 204209821u, 851723965u  // MLCG constants
#define _M0 741103597u    
#define _M1 1597334677u
#define _M2 204209821u
#define _M3 851723965u

#define _FSCALE 256.0f
#define _FNORM (1.0f/16777216.0f/_FSCALE)

// private //
uint base_hash_1D(in uvec2 n)
{
  n.x *= _W0;   // x' = Fx(x)
  n.y *= _W1;   // y' = Fy(y)
  n.x ^= n.y;    // combine
  n.x *= _M0;    // MLCG constant

  return( n.x ^ (n.x >> 16) );
}

uvec2 base_hash_2D(in uvec2 n)
{
  n.x *= _W0;   // x' = Fx(x)
  n.y *= _W1;   // y' = Fy(y)
  n.x ^= n.y;    // combine

  return( (n.x * uvec2(_M0, _M1)) ^ (n.x >> 16) ); // MLCG constant
}

uvec3 base_hash_3D(in uvec3 n)
{
  n.x *= _W0;   // x' = Fx(x)
  n.y *= _W1;   // y' = Fy(y)
  n.z *= _W2;	  // z' = Fz(z)
  n.x ^= n.y;    // combine
  n.x ^= n.z;    // combine

  return( (n.x * uvec3(_M0, _M1, _M2)) ^ (n.x >> 16) ); // MLCG constant
}

uvec4 base_hash_4D(in uvec4 n)
{
  n.x *= _W0;   // x' = Fx(x)
  n.y *= _W1;   // y' = Fy(y)
  n.z *= _W2;	// z' = Fz(z)
  n.w *= _W3;	// w' = Fw(w)
  n.x ^= n.y;    // combine
  n.x ^= n.z;    // combine
  n.x ^= n.w;    // combine

  return( (n.x * uvec4(_M0, _M1, _M2, _M3)) ^ (n.x >> 16) ); // MLCG constant
}

// public //

// ####### hash(out,in)

float hash11(in const float x) 
{
	return base_hash_1D(uvec2(uint(_FSCALE * x))) * _FNORM;
}

float hash12(in const vec2 xy)
{
	return base_hash_1D(uvec2(_FSCALE * xy)) * _FNORM;
}

vec2 hash21(in const float x)
{
	return base_hash_2D(uvec2(uint(_FSCALE * x))) * _FNORM;
}

vec2 hash22(in const vec2 xy)
{
	return base_hash_2D(uvec2(_FSCALE * xy)) * _FNORM;
}

vec3 hash31(in const float x)
{
	return base_hash_3D(uvec3(uint(_FSCALE * x))) * _FNORM;
}

vec3 hash32(in const vec2 x)
{
	return base_hash_3D(uvec3(_FSCALE * x, 0)) * _FNORM;
}

vec3 hash33(in const vec3 x)
{
	return base_hash_3D(uvec3(_FSCALE * x)) * _FNORM;
}

vec4 hash41(in const float x) 
{
	return base_hash_4D(uvec4(uint(_FSCALE * x))) * _FNORM;
}

vec4 hash44(in const vec4 x) 
{
	return base_hash_4D(uvec4(_FSCALE * x)) * _FNORM;
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

