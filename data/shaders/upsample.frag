#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable
#define subgroup_quad_enabled
#define fragment_shader

#if !defined(BLEND)

#include "screendimensions.glsl"

#if !defined(RESOLVE)
#define BINDING 6
#define READWRITE
#include "sharedbuffer.glsl"
#endif

#endif

#include "common.glsl"

layout(early_fragment_tests) in;  

#if defined(BLEND)
layout(location = 0) out vec4 outColor;
layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputVolumetricColor; // full resolution

layout(location = 0) in streamIn /*not used, required for validation*/
{
	readonly vec2		uv;
} In;

void main() {
	
	const vec4 colorVolumetric = subpassLoad(inputVolumetricColor);

	outColor.rgb = colorVolumetric.rgb * colorVolumetric.a;	// pre-multiply for bilateral alpha
	outColor.a = colorVolumetric.a;
}

#elif defined(RESOLVE)

layout(location = 0) in streamIn
{
	readonly vec2		uv;
	readonly flat float	slice;
} In;

#define VOLUME 0
#define REFLECT 1

layout(location = 0) out vec4 outVolumetric;
layout(location = 1) out vec4 outReflection;

layout (binding = 1) uniform sampler2DArray noiseMap;
layout (binding = 2) uniform sampler2D checkeredMap[2]; // half resolution checkered volumetric light  &  bounce light (reflection)

float fetch_bluenoise_scaled(in const vec2 uv)  // important for correct reconstruction result that blue noise is scaled by " * 0.5f + 0.5f "
{
	return( textureLod(noiseMap, vec3(uv * ScreenResDimensions * BLUE_NOISE_UV_SCALER, In.slice), 0).r * 0.5f + 0.5f); // better to use textureLod, point/nearest sampling
	// textureLod all float, repeat done by hardware sampler (point repeat)
}
vec4 reconstruct( in const restrict sampler2D checkeredPixels, in const vec2 uv, in const float scaled_bluenoise )
{
	// rotated grid uv offsets
    const vec2 uvOffsets = (vec2(0.125f, 0.375f) + scaled_bluenoise) * InvScreenResDimensions;

    // 2x2 rotated grid
	vec4 color;
    color  = textureLod(checkeredPixels, uv + uvOffsets * vec2(-1,-1), 0.0f);
    color += textureLod(checkeredPixels, uv + uvOffsets * vec2(-1, 1), 0.0f);
    color += textureLod(checkeredPixels, uv + uvOffsets * vec2( 1,-1), 0.0f);
    color += textureLod(checkeredPixels, uv + uvOffsets * vec2( 1, 1), 0.0f);

	color *= 0.25f; // temporal blending enabled = 0.25, disabled = 0.5f

	return(color);
}

void main() {
	
	// resolve 0 = set start
	// resolve 1 = unset start
	// resolve 2 = set start
	// resolve 3 = unset start
	const float scaled_bluenoise = fetch_bluenoise_scaled(In.uv);

	outVolumetric = reconstruct(checkeredMap[VOLUME], In.uv, scaled_bluenoise);
	outReflection = reconstruct(checkeredMap[REFLECT], In.uv, scaled_bluenoise);
}

#else // not resolve

layout(location = 0) in streamIn
{
	readonly vec2 uv;
	readonly flat float	slice;
} In;

layout(location = 0) out vec4 outVolumetric;
layout(location = 1) out vec4 outReflection;

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput inputFullDepth; // full resolution depth map
layout (binding = 2) uniform sampler2D HalfDepthMap;	// half resolution depth map
layout (binding = 3) uniform sampler2DArray noiseMap; // bluenoise
layout (binding = 4) uniform sampler2D volumetricMap; // half resolution volumetric light
layout (binding = 5) uniform sampler2D reflectionMap; // half resolution bounce light (reflection) source

const float POISSON_RADIUS = 9.0f;
const float INV_HALF_POISSON_RADIUS = 0.5f / POISSON_RADIUS;

const int TAPS = 12;
const float INV_FTAPS = 1.0f / float(TAPS + 1); // + 1 because input is also in average
const vec2 kTaps[TAPS] = {	vec2(-0.326212,-0.40581),vec2(-0.840144,-0.07358),
							vec2(-0.695914,0.457137),vec2(-0.203345,0.620716),
							vec2(0.96234,-0.194983),vec2(0.473434,-0.480026),
							vec2(0.519456,0.767022),vec2(0.185461,-0.893124),
							vec2(0.507431,0.064425),vec2(0.89642,0.412458),
							vec2(-0.32194,-0.932615),vec2(-0.791559,-0.59771) 
					     };

vec3 poissonBlur( in const restrict sampler2D samp, in vec3 color, in const vec2 center_uv, in const float radius ) // pass in center sample 
{
	for ( int tap = 0 ; tap < TAPS ; ++tap ) 
	{
		color += textureLod(samp, center_uv + InvScreenResDimensions * kTaps[tap] * radius, 0).rgb;
	}

	color *= INV_FTAPS; 

	return color;
}

/*
// non-bilateral blur:
vec4 poissonBlur( in const restrict sampler2D samp, in vec4 color, in const vec2 center_uv, in const float radius ) // pass in center sample 
{
	int iTap = TAPS - 1;

	do {

		color += textureLod(samp, center_uv + kTaps[iTap] * InvScreenResDimensions * radius, 0);

	} while(--iTap >= 0);

	color *= INV_FTAPS;

	return color;
}
*/

// duplicated because of different noiseMap binding index between reconstruction and resolve shaders, simpler
float fetch_bluenoise_scaled(in const vec2 uv)  // important for correct reconstruction result that blue noise is scaled by " * 0.5f + 0.5f "
{
	return( textureLod(noiseMap, vec3(uv * ScreenResDimensions * BLUE_NOISE_UV_SCALER, In.slice), 0).r * 0.5f + 0.5f); // better to use textureLod, point/nearest sampling
	// textureLod all float, repeat done by hardware sampler (point repeat)
}

void main() {
	
	float alphaSumV, alphaSumR_Blur, alphaSumR_Fade; // bilateral result
	
	{
		vec4 depthWeights;
		{
			const float fullDepth = subpassLoad(inputFullDepth).r;
			const vec4 halfDepth = textureGather(HalfDepthMap, In.uv);  // 0 = r = depth

			depthWeights = 1.0f / (abs(halfDepth - fullDepth) + 1e-5f);  // epsilon corrects nans or infinity resulting in artifacts
		}

	
		{
			const float inv_weight = 1.0f / (depthWeights.x + depthWeights.y + depthWeights.z + depthWeights.w);

			// sampling alpha channel with jittered (blue noise) offset - 4 samples in textureGather - close to super sampling convolution
			// this eliminates the disconuitity of the edges not matching geometry (difference between volumetric opacity and actual opacity derived from depth buffer)
			// good quality removing the artifact with negilble visible noise
			// this results in superior bilateral filtering
			const vec2 jittered_uv = (In.uv * ScreenResDimensions + fetch_bluenoise_scaled(In.uv)) * InvScreenResDimensions;

			// accumulate alpha weighted by corresponding depth weight
			alphaSumV = dot(textureGather(volumetricMap, jittered_uv, 3), depthWeights); // use texture gather for alpha for best result here
			alphaSumV *= inv_weight;	// average final alpha

			const vec4 alphaSamplesR = textureGather(reflectionMap, jittered_uv, 3);
			alphaSumR_Blur = dot(alphaSamplesR, depthWeights);
			alphaSumR_Blur *= inv_weight;	// average final alpha
			alphaSumR_Fade = dot(1.0f - alphaSamplesR, depthWeights);
			alphaSumR_Fade *= inv_weight;	// average final alpha
		}
	}

	const vec4 vdFd = vec4(dFdx(In.uv), dFdy(In.uv));

	{ // Volumetrics
		// ANTI-ALIASING - based on difference in temporal depth, and spatial difference in opacity
		vec4 volume_color = vec4(supersample(volumetricMap, In.uv, vdFd), alphaSumV);

		expandBlurAA(volumetricMap, volume_color.rgb, In.uv, 1.0f - volume_color.a);  // softening of bilateral edges
		outVolumetric = volume_color; // output alpha is setup for pre-multiplication which happens at blending pass in upsample.frag
	}

	{ // Reflection
		// Reflection map extra AA filtering and blur //      
		vec3 bounce_color = supersample(reflectionMap, In.uv, vdFd);

		// greater distance between source of reflection & reflected surface = greater blur
		bounce_color = poissonBlur(reflectionMap, bounce_color.rgb, In.uv, POISSON_RADIUS * (alphaSumR_Blur + INV_HALF_POISSON_RADIUS)); // min radius 0.5f to max radius POISSON_RADIUS + 0.5f
	
		// no aliasing
		expandBlurAA(reflectionMap, bounce_color.rgb, In.uv, 1.0f - alphaSumR_Blur);  // softening of bilateral edges
		outReflection = vec4(bounce_color, alphaSumR_Fade); // output alpha is setup for pre-multiplication which happens at reflection sampling in lighting.glsl

		if (subgroupElect())
		{
			bounce_color = bounce_color + subgroupQuadSwapHorizontal(bounce_color);
			bounce_color = bounce_color + subgroupQuadSwapVertical(bounce_color);

			atomicAdd(b.average_reflection_count, 1U);
			b.average_reflection_color.rgb += bounce_color; // ok to add out of order, vertex shader uniforms.vert is after which uses the count and the total sum.
		}
	}

	// NO ALIASING !!!
}

#endif




