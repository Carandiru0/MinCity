#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable
#define subgroup_quad_enabled
#define fragment_shader

layout(early_fragment_tests) in;  

#if !defined(BLEND)

#include "screendimensions.glsl"

#if !defined(RESOLVE)
#define BINDING 6
#define READWRITE
#include "sharedbuffer.glsl"
#endif

#endif

#include "common.glsl"

#if defined(BLEND)
layout(location = 0) out vec4 outColor;
layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputVolumetricColor; // full resolution

layout(location = 0) in streamIn /*not used, required for validation*/
{
	readonly vec2		uv;
} In;

void main() {
	
	// straight out - is already pre-multiplied in the proper place, the upsample shader *do not change*, results in a lot of noise dissapearing!
	outColor = subpassLoad(inputVolumetricColor);
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
layout (binding = 3) uniform sampler2D fullMap[2]; // full resolution checkered volumetric light  &  bounce light (reflection)

float fetch_bluenoise_scaled(in const vec2 uv, in const float slice)  // important for correct reconstruction result that blue noise is scaled by " * 0.5f + 0.5f "
{
	return( textureLod(noiseMap, vec3(uv * ScreenResDimensions * BLUE_NOISE_UV_SCALER, slice), 0).r * 0.5f + 0.5f); // better to use textureLod, point/nearest sampling *bugfix - really needs tooo be scaled * 0.5f + 0.5f, otherwise checkerboard reconstruction partially fails *do not change*
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
/* reconstruct is sharper
// full optimal gaussian offsets & kernel for denoise
const vec2 offset[25] = vec2[25](
	vec2(-1,-1),
    vec2(-0.5,-1),
    vec2(0,-1),
    vec2(0.5,-1),
    vec2(1,-1),
    
    vec2(-1,-0.5),
    vec2(-0.5,-0.5),
    vec2(0,-0.5),
    vec2(0.5,-0.5),
    vec2(1,-0.5),
    
    vec2(-1,0),
    vec2(-0.5,0),
    vec2(0,0),
    vec2(0.5,0),
    vec2(1,0),
    
    vec2(-1,0.5),
    vec2(-0.5,0.5),
    vec2(0,0.5),
    vec2(0.5,0.5),
    vec2(1,0.5),
    
    vec2(-1,1),
    vec2(-0.5,1),
    vec2(0,1),
    vec2(0.5,1),
    vec2(1,1)
);
const float kernel[25] = float[25](
	1.0f/256.0f,
    1.0f/64.0f,
    3.0f/128.0f,
    1.0f/64.0f,
    1.0f/256.0f,
    
    1.0f/64.0f,
    1.0f/16.0f,
    3.0f/32.0f,
    1.0f/16.0f,
    1.0f/64.0f,
    
    3.0f/128.0f,
	3.0f/32.0f,
    9.0f/64.0f,
    3.0f/32.0f,
    3.0f/128.0f,
    
    1.0f/64.0f,
    1.0f/16.0f,
    3.0f/32.0f,
    1.0f/16.0f,
    1.0f/64.0f,
    
    1.0f/256.0f,
    1.0f/64.0f,
    3.0f/128.0f,
    1.0f/64.0f,
    1.0f/256.0f
);
// temporal path tracing denoiser (gaussian) algorithm - https://www.shadertoy.com/view/ldKBRV
vec4 denoise_sample(restrict sampler2D map_last_frame, restrict sampler2D map_current_frame, in const vec2 uv, in const float scaled_bluenoise) // awesome function
{
	vec4 f0 = textureLod(map_last_frame, uv, 0.0f); // last frame, full resolution
	vec4 f1 = reconstruct(map_current_frame, uv, scaled_bluenoise); // current frame, quarter resolution, reconstruction of alternating checkerboarded frames

	vec4 sf0 = vec4(0), sf1 = vec4(0);
	float wf = 0.0f;

	for (uint i = 0u; i < 25u; ++i) {
		
		vec2 guv;
		float w0, w1;

		guv = (uv * ScreenResDimensions + offset[i]) * InvScreenResDimensions;
		// current frame
		const vec4 s1 = reconstruct(map_current_frame, guv, scaled_bluenoise); // better sampling for reconstruction
		{
			const vec4 t = f1 - s1;
			const float d = dot(t,t);
			w1 = exp(-d);
		}

		// screen resolution needs to be doubled for previous frame sample, as it is a full resolution textures
		guv = (uv * ScreenResDimensions * 2.0f + offset[i]) * InvScreenResDimensions * 0.5f;
		// previous frame
		const vec4 s0 = textureLod(map_last_frame, guv, 0.0f);
		{
			const vec4 t = f0 - s0;
			const float d = dot(t,t);
			w0 = exp(-d);
		}
			
		const float w = w0 * w1;
		const float wphi = w * kernel[i];
		sf0 += s0 * wphi;
		sf1 += s1 * wphi;
		wf += wphi;
	}

	const vec4 t = sf1/wf - f0;
	const float d = dot(t,t);
	const float p = exp(-d); 

	return mix(sf0/wf, sf1/wf, p);
}*/

void main() {
	
	// resolve 0 = set start
	// resolve 1 = unset start
	// resolve 2 = set start
	// resolve 3 = unset start
	const float scaled_bluenoise = fetch_bluenoise_scaled(In.uv, In.slice);
	
	// final verdict - denoise sample results in blobbyness, reconstruct has more detail (better)
	outVolumetric = reconstruct(checkeredMap[VOLUME], In.uv, scaled_bluenoise);   //denoise_sample(fullMap[VOLUME], checkeredMap[VOLUME], In.uv, scaled_bluenoise);
	outReflection = reconstruct(checkeredMap[REFLECT], In.uv, scaled_bluenoise);  //denoise_sample(fullMap[REFLECT], checkeredMap[REFLECT], In.uv, scaled_bluenoise);
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

			// *bugfix - jittering uv results in larger more blocky voxels being masked into the volumetrics, looks like shit - lower resolution

			// accumulate alpha weighted by corresponding depth weight
			alphaSumV = dot(textureGather(volumetricMap, In.uv, 3), depthWeights); // use texture gather for alpha for best result here
			alphaSumV *= inv_weight;	// average final alpha

			const vec4 alphaSamplesR = textureGather(reflectionMap, In.uv, 3);
			alphaSumR_Blur = dot(1.0f - alphaSamplesR, depthWeights);
			alphaSumR_Blur *= inv_weight;	// average final alpha
			alphaSumR_Fade = dot(alphaSamplesR, depthWeights);
			alphaSumR_Fade *= inv_weight;	// average final alpha
		}
	}

	const vec4 vdFd = vec4(dFdxFine(In.uv), dFdyFine(In.uv));

	vec3 volume_color;
	{ // Volumetrics
		// ANTI-ALIASING - based on difference in temporal depth, and spatial difference in opacity
		volume_color = supersample(volumetricMap, In.uv, vdFd);

		expandAA(volumetricMap, volume_color, In.uv);

		volume_color *= alphaSumV; // pre-multiply w/ bilateral alpha
		outVolumetric = vec4(volume_color, alphaSumV); // output is pre-multiplied, doing it here rather than the "blend stage" hides a lot of noise! *do not change*
	}
	
	vec3 bounce_color;
	{ // Reflection
		// Reflection map extra AA filtering and blur //      
		bounce_color = supersample(reflectionMap, In.uv, vdFd);

		// greater distance between source of reflection & reflected surface = greater blur
		bounce_color = poissonBlur(reflectionMap, bounce_color.rgb, In.uv, POISSON_RADIUS * (alphaSumR_Blur + INV_HALF_POISSON_RADIUS)); // min radius 0.5f to max radius POISSON_RADIUS + 0.5f
		expandAA(reflectionMap, bounce_color, In.uv);

		bounce_color *= alphaSumR_Fade; // pre-multiply w/ bilateral alpha -- value is half-as-bright vs original (darker-reflection) 
		outReflection = vec4(bounce_color, alphaSumR_Fade);
	}

	if (subgroupElect())
	{
		vec3 ambient_color = volume_color + bounce_color;
		ambient_color = ambient_color + subgroupQuadSwapHorizontal(ambient_color);
		ambient_color = ambient_color + subgroupQuadSwapVertical(ambient_color);

		atomicAdd(b.average_reflection_count, 2U); // bounce color + volume color (the average calc is in the voxel vertex shader, and yes it accounts for the quad 
		b.average_reflection_color.rgb += ambient_color; // ok to add out of order, vertex shader uniforms.vert is after which uses the count and the total sum.
	}

	// NO ALIASING !!!
}

#endif




