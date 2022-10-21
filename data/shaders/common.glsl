#ifndef _COMMON_GLSL
#define _COMMON_GLSL

#define PI (3.14159265358979323846)
#define GOLDEN_RATIO (1.61803398874989484820)
#define GOLDEN_RATIO_ZERO (0.61803398874989484820)
#define GOLDEN_ANGLE (2.399963229728653)
#define LUMA vec3(0.2126f, 0.7152f, 0.0722f)

#define BLUE_NOISE_UV_SCALER (1.0f/128.0f)
#define BLUE_NOISE_DITHER_SCALAR (17.0f/255.0f)

#define TEXTURE_4K_SIZE 4096.0f

// USAGE :
// define subgroup_quad_enabled, subgroup_xxx_enabled if required common function needs it, enable said extension in the root shader file,
// then include header file, eg.)
// #define subgroup_quad_enabled		
// #include "common.glsl"

#define select(iffalse, iftrue, condition) mix(iffalse, iftrue, condition)

/**
 * ~FAST Conditional move:
 */
#define movc(cond, variable, value) { if (cond) variable = value; }

float sq(in const float x) { return(x * x); }
vec3  sq(in const vec3 xyz) { return(xyz * xyz); }

// https://www.shadertoy.com/view/WlfXRN
vec3 inferno(in const float t) {

    const vec3 c0 = vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    const vec3 c1 = vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    const vec3 c2 = vec3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    const vec3 c3 = vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    const vec3 c4 = vec3(77.162935699427, -33.40235894210092, -81.80730925738993);
    const vec3 c5 = vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    const vec3 c6 = vec3(25.13112622477341, -12.24266895238567, -23.07032500287172);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));

}

float saturation(in const vec3 rgb) {
    float mini = min(rgb.r,min(rgb.g,rgb.b));
    float maxi = max(rgb.r,max(rgb.g,rgb.b));
    
    return (maxi - mini) / maxi;
}

float whiteness(in const vec3 rgb) {
    
	return(rgb.x * rgb.y * rgb.z);
}

// https://www.shadertoy.com/view/MtjBWz - thanks iq
vec3 rndC(in vec3 voxel) // good function, works with any texture sampling that uses linear interpolation
{
    voxel = voxel + 0.5f;
    const vec3 ivoxel = floor( voxel );
    const vec3 fvoxel = fract( voxel );
    
	voxel = ivoxel + fvoxel*fvoxel*(3.0f-2.0f*fvoxel); 
 
	return(voxel - 0.5f);  // returns in same unit as input, voxels
}
vec2 rndC(in vec2 pixel) // good function, works with any texture sampling that uses linear interpolation
{
    pixel = pixel + 0.5f;
    const vec2 ipixel = floor( pixel );
    const vec2 fpixel = fract( pixel );
    
	pixel = ipixel + fpixel*fpixel*(3.0f-2.0f*fpixel); 
 
	return(pixel - 0.5f);  // returns in same unit as input, pixels
}
float rndC(in float pixel) // good function, works with any texture sampling that uses linear interpolation
{
    pixel = pixel + 0.5f;
    const float ipixel = floor( pixel );
    const float fpixel = fract( pixel );
    
	pixel = ipixel + fpixel*fpixel*(3.0f-2.0f*fpixel); 
 
	return(pixel - 0.5f);  // returns in same unit as input, pixels
}

#ifdef fragment_shader
// for better rendering of "pixel art" stuff
// https://www.shadertoy.com/view/MllBWf
vec2 subpixel_filter(in vec2 pixel) // good function, works with any texture sampling that uses linear interpolation
{
    pixel = pixel + 0.5f;
    const vec2 ipixel = floor( pixel );
    const vec2 fpixel = fract( pixel );
    const vec2 aapixel = fwidth( pixel ) * 0.75f; // filter width = 1.5 pixels

	// tweaking fractional value before texture sampling
	pixel = ipixel + smoothstep( vec2(0.5)-aapixel, vec2(0.5)+aapixel, fpixel);
 
	return(pixel - 0.5f);  // returns in same unit as input, pixels
}
#endif

// https://www.shadertoy.com/view/Xt23zV - Dave Hoskins 
// Linear Step - give it a range [edge0, edge1] and a fraction between [0...1]
// returns the normalized [0...1] equivalent of whatever range [edge0, edge1] is, linearly.
// (like smoothstep, except it's purely linear
float linearstep(in const float edge0, in const float edge1, in const float x)
{
    return( clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f) );
}

float inverse_linearstep(in const float edge0, in const float edge1, in const float d)
{
	//       (x - edge0)
	// d = ---------------
	//     (edge1 - edge0)
	//
	// x = d * (edge1 - edge0) + edge0
	//
	// herbie optimized -> fma(d, edge1 - edge0, edge0)
    return( clamp(fma(d, edge1 - edge0, edge0), 0.0f, 1.0f) );
}

// Ken Perlin suggests an improved version of the smoothstep() function, 
// which has zero 1st- and 2nd-order derivatives at x = 0 and x = 1.
float smootherstep(in const float edge0, in const float edge1, in float x) 
{
  x = (x - edge0)/(edge1 - edge0);
  return( x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f) ); // quintic equation
}

// smoothly remap value in range a...b  to  0...1
// ie.) range has a minimum of 0.0 and a maximum of 0.1666f, remap to 0...1 smoothly for any value in this range
// smoothremap(0.0f, 0.1666f, t)
float smoothremap(in const float lower_bound, in const float upper_bound, in const float value)
{
	return( smoothstep(lower_bound, upper_bound, value) );
}

// iq - https://www.iquilezles.org/www/articles/functions/functions.htm
// desmos - https://www.desmos.com/calculator/gopau2jnlb
float cubicPulse( in float x, in const float c, in const float w )
{
    x = abs(x - c);
	x = max(0.0f, x - w); // removed branch
    x /= w;
    return( x*x*(3.0f-2.0f*x) );
}
// iq - https://www.iquilezles.org/www/articles/functions/functions.htm
// desmos - https://www.desmos.com/calculator/gopau2jnlb
// this parabola has a smoothstep like interpolation on both sides
float parabola( in const float x, in const float k )
{
    return( pow( 4.0f*x*(1.0f-x), k ) );
}
float parabola( in const float x ) // optimized version for k = 2
{
	const float y = 4.0f*x*(1.0f-x);
    return( y*y );
}
// triangle wave
float triangle_wave(in const float t) 
{
	return(abs(fract(t) * 2.0f - 1.0f));
}

// differential blending
// https://www.ea.com/frostbite/news/4k-checkerboard-in-battlefield-1-and-mass-effect-andromeda
// slide 114 - by Graham!
//usage: mix(a, b, colordiffblend(a,b))
float colordiffblend(in const vec3 a, in const vec3 b)
{
	return (1.0f / (1.0f + length(a - b)));
}


//smooth version of step - https://www.ronja-tutorials.com/2019/11/29/fwidth.html
// produces a harder cut off in conparison with smoothstep, if smoothstep can't be used use this
// usage:
// float aa = aaStep(0.25f, gradient); // harder cut off
// -or-
// float aa = smoothstep(0.24f, 1.0f, gradient); // smoother cut off
//
// gradient *= aa;  // applying cutoff (hardly)
// -or-
// gradient = (gradient - aa * 0.5f) * aa;  // applying cutoff (smoothly)
//
// **** to remember:
// float aa = smoothstep(0.0f, gradient, gradient);  ** pretty much equals gradient** so the line below this does nothing
//  gradient *= aa;
// -however-
// float aa = smoothstep(0.0f, 1.0f, gradient);	**actually smooths and remaps the gradient to the smoothstep function
//  gradient *= aa; // apply smoothstep to original grasdient
// -and-
// *** if the value of second parameter is smaller than first parameter into smoothstep
// there will be an undesired "inverse" cutoff ***
// if the value for the second parameter is larger than 1.0f, the result is no longer normalized in the range 0..1.0
// ie.) smoothstep(0.0f, 1.5f, gradient) requires : smoothstep(0.0f, 1.5f, gradient) / (1.5f*1.5f)  to normalize again
// I dunno about you but negative values are way outside the scope of smoothstep
// #### use smoothstep rather than aaStep, only use aaStep in special cases where a smoother, but harder cutoff is needed (in comparison to atep())
// or if "linearerity" is critically important and smoothstep is introducing unsightly waves of discontuity ####
#ifdef fragment_shader
float aaStep(in float compValue, in float gradient){
  const float halfChange = fwidth(gradient) * 0.5f;

  //base the range of the inverse lerp on the change over one pixel
  float lowerEdge = compValue - halfChange;
  float upperEdge = compValue + halfChange;
  //do the inverse interpolation
  return( clamp((gradient - lowerEdge) / (upperEdge - lowerEdge), 0.0f, 1.0f) );
}
vec2 aaStep(in const vec2 compValue, in const vec2 gradient){
  const vec2 halfChange = fwidth(gradient) * 0.5f;

  //base the range of the inverse lerp on the change over one pixel
  const vec2 lowerEdge = compValue - halfChange;
  const vec2 upperEdge = compValue + halfChange;
  //do the inverse interpolation
  return( clamp((gradient - lowerEdge) / (upperEdge - lowerEdge), vec2(0.0f), vec2(1.0f)) );
}
#endif

// polynomial smooth mins:
// BEST - C2 (2nd Order) Derivative Continuity - cubic smin:
float smin( in const float a, in const float b, in const float k )
{
    const float h = max( k-abs(a-b), 0.0f )/k;
    return min( a, b ) - h*h*h*k*(1.0f/6.0f);
}
// GOOD - C1 (1st Order) Derivative Continuity - quadratic smin:
//float smin(in const float a, in const float b, in const float k)
//{
//    const float h = clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
//    return mix(b, a, h) - k * h * (1.0f - h);
//}
// YUV-RGB conversion routine from Hyper3D
vec3 encodePalYuv(in const vec3 rgb)
{
    return vec3(
        dot(rgb, vec3(0.299f, 0.587f, 0.114f)),
        dot(rgb, vec3(-0.14713f, -0.28886f, 0.436f)),
        dot(rgb, vec3(0.615f, -0.51499f, -0.10001f))
    );
}

vec3 decodePalYuv(in const vec3 yuv)
{
    return vec3(
        dot(yuv, vec3(1.0f, 0.0f, 1.13983f)),
        dot(yuv, vec3(1.0f, -0.39465f, -0.58060f)),
        dot(yuv, vec3(1.0f, 2.03211f, 0.0f))
    );
}

// see superfastmath for reference implementation
vec3 RGBToOKLAB(in const vec3 rgb)
{
	const mat3 kCONEtoLMS = mat3(                
         0.4122214708f,  0.2119034982f,  0.0883024619f,
         0.5363325363f,  0.6806995451f,  0.2817188376f,
         0.0514459929f,  0.1073969566f,  0.6299787005f);

	return( pow(kCONEtoLMS*rgb, vec3(1.0f/3.0f)) );
}

vec3 OKLABToRGB(in const vec3 oklab)
{
	const mat3 kLMStoCONE = mat3(
         4.0767416621f, -1.2684380046f, -0.0041960863f,
        -3.3077115913f,  2.6097574011f, -0.7034186147f,
         0.2309699292f, -0.3413193965f,  1.7076147010f);

	return(kLMStoCONE*(oklab*oklab*oklab));
}

vec3 unpackColor(in float fetched) {

	// SHADER sees     A B G R			(RGBA in reverse) 
	return( unpackUnorm4x8(uint(fetched)).rgb );
}
float packColor(in const vec3 pushed) {
	
	// SHADER sees     A B G R			(RGBA in reverse) 
	return(float(packUnorm4x8(vec4(pushed,0.0f))));
}

// Converts a color from linear to sRGB (branchless)
vec3 toSRGB(vec3 linearRGB)
{
    const bvec3 cutoff = lessThan(linearRGB, vec3(0.0031308f));
    return mix(1.055f*pow(linearRGB, vec3(1.0f/2.4f)) - 0.055f, linearRGB * 12.92f, cutoff);
}

// Converts a color from sRGB to linear (branchless)
vec3 toLinear(vec3 sRGB)
{
    const bvec3 cutoff = lessThan(sRGB, vec3(0.04045f));
    return mix(pow((sRGB + vec3(0.055f)) / 1.055f, vec3(2.4f)), sRGB / 12.92f, cutoff);
}

vec2 compressNormal(in const vec3 normal)  // expects in normal to be normalized
{
	// lossless compression!
	// compress direction storing:  convert normalized (-1 ... 1) to (0 ... 1)
	//								only x(width) and y(depth), when decompressing z is calculated:
	//																								1.0f = x + y + z
	//																								convert (0 ... 1) to (-1 ... 1)
	//																								normalize()
	return (normal * 0.5f + 0.5f).xy;
}
vec3 decompressNormal(in const vec2 compressed_normal)
{
	// compress direction storing:  convert normalized (-1 ... 1) to (0 ... 1)
	//								only x(width) and y(depth), when decompressing z is calculated:
	//																								1.0f = x + y + z
	//																								convert (0 ... 1) to (-1 ... 1)
	//																								normalize()
	// solve for z, 1=x+y+z : z=1-x-y
	vec3 decompressed_normal;
	decompressed_normal.z = (1.0f - compressed_normal.x - compressed_normal.y);
	decompressed_normal.xy = compressed_normal.xy;

	decompressed_normal = decompressed_normal * 2.0f - 1.0f;
	
	return(normalize(decompressed_normal));
}

// rotation quaternions only
vec4 qmul(in const vec4 q0, in const vec4 q1)
{
    vec4 qq;
    
    qq.xyz = cross(q0.xyz, q1.xyz) + q0.w*q1.xyz + q1.w*q0.xyz;
    qq.w = q0.w*q1.w - dot(q0.xyz, q1.xyz);
    return(qq);
}
vec4 qinv(in const vec4 q0)
{
	return(vec4(-q0.xyz,q0.w));
}

vec3 v3_rotate( in const vec3 p, in const vec4 q) // preferred - rotate a 3d vector by quaternion
{ 
  return(qmul(qinv(q), qmul(vec4(p, 0.0f), q)).xyz);
}

vec3 rotate( in const vec3 p, in const vec2 cossin )
{
	#define c_ x
	#define s_ y

	return( vec3(fma(p.x, cossin.c_, p.z * cossin.s_), p.y, fma(p.x, -cossin.s_, p.z * cossin.c_)) );

	#undef c_
	#undef s_
}
vec3 rotate( in const vec3 p, in const float angle )
{
	#define c_ x
	#define s_ y
	const vec2 cossin = vec2(cos(angle), sin(angle));
	return( vec3(fma(p.x, cossin.c_, p.z * cossin.s_), p.y, fma(p.x, -cossin.s_, p.z * cossin.c_)) );

	#undef c_
	#undef s_
}
vec2 rotate( in const vec2 p, in const vec2 cossin )
{
	#define c_ x
	#define s_ y

	return( vec2(fma(p.x, cossin.c_, p.y * cossin.s_), fma(p.x, -cossin.s_, p.y * cossin.c_)) );

	#undef c_
	#undef s_
}
vec2 rotate( in const vec2 p, in const float angle )
{
	#define c_ x
	#define s_ y
	const vec2 cossin = vec2(cos(angle), sin(angle));
	return( vec2(fma(p.x, cossin.c_, p.y * cossin.s_), fma(p.x, -cossin.s_, p.y * cossin.c_)) );

	#undef c_
	#undef s_
}


/* non aliased sampling 
https://www.shadertoy.com/view/ldsSRX

vec2 saturate(vec2 x)
{
	return clamp(x, 0.0, 1.0);   
}

vec2 magnify(vec2 uv)
{
    uv *= iChannelResolution[0].xy; 
    return (saturate(fract(uv) / saturate(fwidth(uv))) + floor(uv) - 0.5f) / iChannelResolution[0].xy;
}

usage :
textureGrad(iChannel0, magnify(uv), dFdx(uv), dFdy(uv));
*/
#if defined(ScreenResDimensions) && defined(InvScreenResDimensions)
vec2 magnify(in vec2 uv, in const vec2 dimensions)
{
    uv *= dimensions; 
    return (clamp(fract(uv) / clamp(fwidth(uv), 0.0f, 1.0f), 0.0f, 1.0f) + floor(uv) - 0.5f) / dimensions;
}
vec2 magnify(in vec2 uv)
{
    uv *= ScreenResDimensions; 
    return (clamp(fract(uv) / clamp(fwidth(uv), 0.0f, 1.0f), 0.0f, 1.0f) + floor(uv) - 0.5f) * InvScreenResDimensions;
}
vec3 magnify(in vec3 voxel)
{ 
    return (clamp(fract(voxel) / clamp(fwidth(voxel), 0.0f, 1.0f), 0.0f, 1.0f) + floor(voxel) - 0.5f);
}
#endif

#if defined(fragment_shader) // fragment shaders only
vec3 supersample(restrict in const sampler2D colorTex, in const vec2 uv, in const vec4 vdFd)	//  ** rotated grid 2x2 supersampling (RGSS / 4Rook)
{
	// rotated grid uv offsets
	const vec2 uvOffsets = vec2(0.125f, 0.375f);

	// supersampled using 2x2 rotated grid
	vec3 color = textureLod(colorTex, uv + uvOffsets.x * vdFd.xy + uvOffsets.y * vdFd.zw, 0.0f).rgb;
	color += textureLod(colorTex, uv + uvOffsets.x * vdFd.xy - uvOffsets.y * vdFd.zw, 0.0f).rgb;
	color += textureLod(colorTex, uv + uvOffsets.y * vdFd.xy - uvOffsets.x * vdFd.zw, 0.0f).rgb;
	color += textureLod(colorTex, uv + uvOffsets.y * vdFd.xy + uvOffsets.x * vdFd.zw, 0.0f).rgb;

	return(color * 0.25f);
}
vec3 supersample(restrict in const sampler2D colorTex, in const vec2 uv)	//  ** rotated grid 2x2 supersampling (RGSS / 4Rook)
{
	// per pixel partial derivatives
	return(supersample(colorTex, uv, vec4(dFdx(uv), dFdy(uv))));
}

#endif // frag

// single pass, single direction fast gaussian 13 tap blur - https://github.com/Jam3/glsl-fast-gaussian-blur
vec3 blur(restrict in const sampler2D image, in const vec2 uv, in const vec2 inv_resolution, in vec2 direction) {
  
  vec3 color = textureLod(image, uv, 0).rgb * 0.1964825501511404;
  direction = direction * inv_resolution; // bake distance into direction for offset
  {
	  const vec2 off1 = 1.411764705882353 * direction;
	  color += textureLod(image, uv + off1, 0).rgb * 0.2969069646728344;
	  color += textureLod(image, uv - off1, 0).rgb * 0.2969069646728344;
  }
  {
	  const vec2 off2 = 3.2941176470588234 * direction;
	  color += textureLod(image, uv + off2, 0).rgb * 0.09447039785044732;
	  color += textureLod(image, uv - off2, 0).rgb * 0.09447039785044732;
  }
  {
	  const vec2 off3 = 5.176470588235294 * direction;
	  color += textureLod(image, uv + off3, 0).rgb * 0.010381362401148057;
	  color += textureLod(image, uv - off3, 0).rgb * 0.010381362401148057;
  }
  return(color);
}

#ifdef subgroup_quad_enabled
// special AA method that is very fast, requires:  #extension GL_KHR_shader_subgroup_quad: enable
// requires 1st, centered color sample to be passed in
void expandAA( restrict in const sampler2D colorTex, inout vec3 color, in const vec2 uv )	// sample stride: 1
{
	const vec3 reference_color = color;
	color += subgroupQuadSwapHorizontal(color);
	color += subgroupQuadSwapVertical(color);

	if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0) )		// TL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0)).rgb;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 1) ) // TR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0)).rgb;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 2) ) // BL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0)).rgb;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 3) ) // BR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0)).rgb;
	}	

	color = mix(reference_color, color * (1.0f/6.0f), 0.5f);
}
void expandAA( restrict in const sampler2D colorTex, inout vec4 color, in const vec2 uv )	// blends alpha aswell version sample stride: 1
{
	const vec4 reference_color = color;
	color += subgroupQuadSwapHorizontal(color);
	color += subgroupQuadSwapVertical(color);
	// 4 samples...

	if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0) )		// TL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1));
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0));
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 1) ) // TR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0));
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 2) ) // BL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1));
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0));
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 3) ) // BR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0));
	}	
	// 2 more samples...

	color = mix(reference_color, color * (1.0f/6.0f), 0.5f);
}
void expandBlurAA( restrict in const sampler2D colorTex, inout vec3 color, in const vec2 uv, in const float blur_strength )	// sample stride: 2
{
	const vec3 reference_color = color;
	color += subgroupQuadSwapHorizontal(color);
	color += subgroupQuadSwapVertical(color);

	if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0) )		// TL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -2)).rgb * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-2,  0)).rgb * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 1) ) // TR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -2)).rgb * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 2,  0)).rgb * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 2) ) // BL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  2)).rgb * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-2,  0)).rgb * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 3) ) // BR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0)).rgb;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  2)).rgb * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 2,  0)).rgb * blur_strength;
	}	

	color = mix(reference_color, color * 1.0f/(6.0f + blur_strength * 2.0f), 0.5f);
}
void expandBlurAA( restrict in const sampler2D colorTex, inout vec4 color, in const vec2 uv, in const float blur_strength )	// blends alpha aswell version sample stride: 2
{
	const vec4 reference_color = color;
	color += subgroupQuadSwapHorizontal(color);
	color += subgroupQuadSwapVertical(color);
	// 4 samples,,,

	if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0) )		// TL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1));
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -2)) * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-2,  0)) * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 1) ) // TR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -1));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0, -2)) * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 2,  0)) * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 2) ) // BL
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1));
		color += textureLodOffset(colorTex, uv, 0, ivec2(-1,  0));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  2)) * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2(-2,  0)) * blur_strength;
	}
	else if ( gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 3) ) // BR
	{
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  1));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 1,  0));
		color += textureLodOffset(colorTex, uv, 0, ivec2( 0,  2)) * blur_strength;
		color += textureLodOffset(colorTex, uv, 0, ivec2( 2,  0)) * blur_strength;
	}	
	// 4 more samples, 2 are weighted

	color = mix(reference_color, color * 1.0f/(6.0f + blur_strength * 2.0f), 0.5f);
}

#endif

// diagonally skewed pixel bleeding, pass in center sample or else
// InvScreenResDimensions must be a global constant defined before including this header file
#if defined(InvScreenResDimensions)
vec3 bleed(restrict in const sampler2D colorTex, in const vec3 color, in const vec2 uv, in const float strength)
{
	const vec4 uv_offset = vec4(uv + (0.5f * InvScreenResDimensions), uv - (0.5f * InvScreenResDimensions));
    return( mix( color, max(color,	max(max( textureLodOffset(colorTex, uv_offset.xy, 0, ivec2( 1,  1)).rgb, 
											 textureLodOffset(colorTex, uv_offset.xy, 0, ivec2( 1, -1)).rgb), 
										max( textureLodOffset(colorTex, uv_offset.zw, 0, ivec2( -1, 1)).rgb, 
											 textureLodOffset(colorTex, uv_offset.zw, 0, ivec2( -1,-1)).rgb) ) ), strength) );
}
#endif

// usage: vec2 uv = lensDistort(uv, factor)*Res/min(Res,Res.yx);
//		  uv = uv / Res.x * Res.y;
//        factor  - 0.05
// eg.) anamorphic mode
//vec2 uv = lensDistort(In.uv, -0.05f)*ScreenResDimensions/min(ScreenResDimensions,ScreenResDimensions.yx);
//uv = uv / ScreenResDimensions.x * ScreenResDimensions.y;
vec2 lensDistort(vec2 c, float factor)
{
    // [0;1] -> [-1;1]
    c = (c - 0.5) * 2.0;
    // [-1;1] -> film frame size
    c.y *= 16.0/9.0;
    // distort
    c /= sqrt(1.0 + dot(c, c) * -factor + 2.0 * factor);
    // film frame size -> [-1;1]
    c.y *= 9.0/16.0;
    // [-1;1] -> [0;1]
    c = c * 0.5 + 0.5;
    return c;
}

/* backup of gradient / normal calculation 
 https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch01.html
Compute normal based on density from GPU Gems book, chapter 1 !
based on central differences

float d = 1.0/(float)voxels_per_block;
float3 grad;
grad.x = density_vol.Sample(TrilinearClamp, uvw + float3( d, 0, 0)) -
         density_vol.Sample(TrilinearClamp, uvw + float3(-d, 0, 0));
grad.y = density_vol.Sample(TrilinearClamp, uvw + float3( 0, d, 0)) -
         density_vol.Sample(TrilinearClamp, uvw + float3( 0,-d, 0));
grad.z = density_vol.Sample(TrilinearClamp, uvw + float3( 0, 0, d)) -
         density_vol.Sample(TrilinearClamp, uvw + float3( 0, 0,-d));
output.wsNormal = -normalize(grad);

vec3 computeNormal(in const vec3 uvw)
{
	vec3 gradient;

	// trilinear sampling - more accurate interpolated result then using texelFetch
	gradient.x = fetch_opacity(uvw + (vec3(0.5f, 0.0f, 0.0f) * InvVolumeDimensions)) - fetch_opacity(uvw + (vec3(-0.5f, 0.0f, 0.0f) * InvVolumeDimensions));
	gradient.y = fetch_opacity(uvw + (vec3(0.0f, 0.5f, 0.0f) * InvVolumeDimensions)) - fetch_opacity(uvw + (vec3(0.0f, -0.5f, 0.0f) * InvVolumeDimensions));
	gradient.z = fetch_opacity(uvw + (vec3(0.0f, 0.0f, 0.5f) * InvVolumeDimensions)) - fetch_opacity(uvw + (vec3(0.0f, 0.0f, -0.5f) * InvVolumeDimensions));

	return( normalize(gradient) ); // normal from central differences (gradient) 
}
*/
/* backup of raymarching depth reconstruction

float fetch_depth( in const vec2 uv )
{
	return( textureLod(depthMap, uv, 0).r );
}
float reconstruct_depth(out vec3 depth_pos, in const vec3 rd)
{
	const float depth = fetch_depth(gl_FragCoord.xy / ScreenResDimensions);

	const float viewZDist = (dot(-In.eyeDir, rd));
	depth_pos = rd * depth / viewZDist;
	
	return depth;
}
// usage:
//vec3 depth_position;
//const float depth = reconstruct_depth(depth_position, rd);
//float t_depth = length(depth_position);

// Modify Interval end to be min of either volume end or scene depth
// **** this clips the volume against any geometry in the scene
//t_depth = t_depth * abs(dot(In.eyeDir, rd));

//t_hit.y = min(t_hit.y, (t_depth + t_hit.x));
*/

/* backup of useful expanding/growing poisson disc blur
const int TAPS = 12;
const vec2 kTaps[TAPS] = {	vec2(-0.326212,-0.40581),vec2(-0.840144,-0.07358),
							vec2(-0.695914,0.457137),vec2(-0.203345,0.620716),
							vec2(0.96234,-0.194983),vec2(0.473434,-0.480026),
							vec2(0.519456,0.767022),vec2(0.185461,-0.893124),
							vec2(0.507431,0.064425),vec2(0.89642,0.412458),
							vec2(-0.32194,-0.932615),vec2(-0.791559,-0.59771) 
					     };

void poissonBlurReflection( inout vec3 color_reflect, in const vec2 center_uv, in const float radius ) // pass in center sample 
{
	int iTap = TAPS - 1;

	do {

		color_reflect.rgb += textureLod(ambientLightMap, center_uv + kTaps[iTap] * InvDownResDimensions * radius, 0).rgb;

	} while(--iTap >= 0);

	color_reflect *= (1.0f/float(TAPS+1));
}
*/
/* backup of good crt shader technique
// bleedStr must be defined b4 include of common.glsl
const float rgbMaskSub 		= 96.0f / 255.0f;
const float rgbMaskSep		= 1.5f;
const float rgbMaskStr		= 0.15f;
const float rgbMaskPix		= 6.0f;
const float hardScan		= 0.1f;
const float scanPix			= 9.0f;

vec3 crt( restrict in const sampler2D colorBleedTex, in const vec3 color, in const vec2 uv )
{
	//
	const vec2 modulo = floor(mod(uv * vec2(1280,720), vec2(rgbMaskPix, scanPix)));

	const float luminance = dot(color, LUMA);
	const float maskLuma = (rgbMaskSub + (0.5f - rgbMaskSub) * -(luminance * 2.0f - 1.0f));
	const float maskStrength = (rgbMaskStr - (1.0f - rgbMaskStr) * 0.5f * luminance);

	// apply rgb mask
	// vertical //
	vec3 color_crt = mix(color,
				color - mix(vec3(0, maskLuma * rgbMaskSep * 0.5f, maskLuma * rgbMaskSep),
						mix(vec3(maskLuma * rgbMaskSep * 0.5f, 0, maskLuma * rgbMaskSep * 0.5f),
							vec3(maskLuma * rgbMaskSep, maskLuma * rgbMaskSep * 0.5f, 0),
							modulo.x * 0.5f),
						modulo.x * 0.5f),
					maskStrength);
	// horizontal //
	color_crt = mix(color_crt, mix(color,
				color - mix(vec3(0, maskLuma * rgbMaskSep * 0.5f, maskLuma * rgbMaskSep),
						mix(vec3(maskLuma * rgbMaskSep * 0.5f, 0, maskLuma * rgbMaskSep * 0.5f),
							vec3(maskLuma * rgbMaskSep, maskLuma * rgbMaskSep * 0.5f, 0),
							modulo.y * 0.5f),
						modulo.y * 0.5f),
					maskStrength), 0.5f);

	// apply adaptive scanlines with color bleeding them
	//color_crt = mix(color_crt, color_crt * hardScan, modulo.y * (hardScan / scanPix));

	// bleed into adjacent pixels
	color_crt = mix(color_crt, max(color, bleed(colorBleedTex, color, uv)), bleedStr);

	return color_crt;
}
}*/
/* backup of awesome scanline algo
	ambient = mix(vec3(0), ambient, aaStep( SCANLINE_INTERLEAVE * 0.5f, mod(gl_FragCoord.y, SCANLINE_INTERLEAVE - InvScreenResDimensions.y * 0.5f) ) );
	ambient += subgroupQuadSwapDiagonal(ambient);
	ambient *= 0.5f;
*/
 
#endif

