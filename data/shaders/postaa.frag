#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable

#ifdef HDR
	#define output_rgba rgba16
	#define output_r r16
#else 
    #define output_rgba rgba8
	#define output_r r8
#endif

#include "screendimensions.glsl"
#ifdef HDR
layout (constant_id = 4) const float MaximumNits = 1000.0f;
#endif

#define subgroup_quad_enabled
#define fragment_shader
#include "common.glsl"

#if defined (SMAA_PASS_2) || defined(OVERLAY)
#define FINAL // indicating stage is final / last one
#endif

layout(location = 0) out vec4 outColor;
#ifdef FINAL
layout(location = 1) out vec4 outLastFrame;
#endif

layout(location = 0) in streamIn
{
	readonly vec2		uv;
#if defined (SMAA_PASS_2) || defined(OVERLAY)
	readonly flat float	slice;
#endif

#ifdef OVERLAY
#endif
} In;
#define texcoord uv // alias

#ifndef FINAL
// Post Descriptor Set //
layout (binding = 1) uniform sampler2D colorMap;
layout (binding = 2) uniform sampler2DArray noiseMap;	// bluenoise RG channels, 64 slices.

layout (binding = 3, output_rgba) writeonly restrict uniform image2D outTemporal;
layout (binding = 4) uniform sampler2D temporalColorMap;

layout (input_attachment_index = 0, set = 0, binding = 5) uniform subpassInput lastColorMap;

layout (binding = 6) uniform sampler2D blurMap[2];
layout (binding = 7, output_rgba) writeonly restrict uniform image2D outBlur[2];

layout (binding = 8) uniform sampler2D anamorphicMap[2];
layout (binding = 9, output_r) writeonly restrict uniform image2D outAnamorphic[2];

#else
// Final Descriptor Set //
layout (binding = 1) uniform sampler2D colorMap;
layout (binding = 2) uniform sampler2DArray noiseMap;	// bluenoise RG channels, 64 slices.

layout (binding = 3) uniform sampler2D temporalColorMap;

layout (binding = 4) uniform sampler2D blurMap[2];

layout (binding = 5) uniform sampler2D anamorphicMap[2];

layout (binding = 6) uniform sampler3D lutMap;

layout (input_attachment_index = 0, set = 0, binding = 7) uniform subpassInput inputGUI;

#endif

#if defined(TMP_PASS)
// Shadertoy - https://www.shadertoy.com/view/lt3SWj
// Temporal AA based on Epic Games' implementation:
// https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf
// 
// Originally written by yvt for https://www.shadertoy.com/view/4tcXD2
// Feel free to use this in your shader!
//
// colorMap: input image
// iChannel1: output image from the last frame
//
// Version history:
//   12/13/2016: first version
//   12/14/2016: removed unnecessary gamma correction inside
//               encodePalYuv and decodePalYuv

// YUV-RGB conversion routine from Hyper3D
#define off InvScreenResDimensions

vec4 temporal_aa(in const vec2 uv)
{
	const vec4 lastColor = subpassLoad(lastColorMap);
    
    vec3 antialiased = lastColor.xyz;
    float mixRate = min(lastColor.w, 0.5);
    
    vec3 in0 = textureLod(colorMap, uv, 0).xyz;

    antialiased = mix(antialiased * antialiased, in0 * in0, mixRate);
    antialiased = sqrt(antialiased);
    
	// interleave for hiding latency
	vec3 in1 = textureLod(colorMap, uv + vec2(+off.x, 0.0), 0).xyz;
	antialiased = encodePalYuv(antialiased);
	in0 = encodePalYuv(in0);
    vec3 in2 = textureLod(colorMap, uv + vec2(-off.x, 0.0), 0).xyz;
	in1 = encodePalYuv(in1);
    vec3 in3 = textureLod(colorMap, uv + vec2(0.0, +off.y), 0).xyz;
	in2 = encodePalYuv(in2);
    vec3 in4 = textureLod(colorMap, uv + vec2(0.0, -off.y), 0).xyz;
	in3 = encodePalYuv(in3);
    vec3 in5 = textureLod(colorMap, uv + vec2(+off.x, +off.y), 0).xyz;
	in4 = encodePalYuv(in4);
    vec3 in6 = textureLod(colorMap, uv + vec2(-off.x, +off.y), 0).xyz;
	in5 = encodePalYuv(in5);
    vec3 in7 = textureLod(colorMap, uv + vec2(+off.x, -off.y), 0).xyz;
	in6 = encodePalYuv(in6);
    vec3 in8 = textureLod(colorMap, uv + vec2(-off.x, -off.y), 0).xyz;
    in7 = encodePalYuv(in7);
    in8 = encodePalYuv(in8);

    vec3 minColor = min(min(min(in0, in1), min(in2, in3)), in4);
    vec3 maxColor = max(max(max(in0, in1), max(in2, in3)), in4);
    minColor = mix(minColor,
       min(min(min(in5, in6), min(in7, in8)), minColor), 0.5f);
    maxColor = mix(maxColor,
       max(max(max(in5, in6), max(in7, in8)), maxColor), 0.5f);
    
   	const vec3 preclamping = antialiased;
    antialiased = clamp(antialiased, minColor, maxColor);
    
    mixRate = 1.0 / (1.0 / mixRate + 1.0f);
    
    const vec3 diff = antialiased - preclamping;
    const float clampAmount = dot(diff, diff);
    
    mixRate += clampAmount * 4.0f;
    mixRate = clamp(mixRate, 0.05f, 0.5f);
    
    antialiased = decodePalYuv(antialiased);
        
    return vec4(antialiased, mixRate);
}

void anamorphicMask(in const vec3 color)
{
	const float luminance = dot(color, LUMA);

	float mask = 1.5f * luminance*luminance*color.g;
	
	mask = smoothstep(0.0f, 1.0f, mask);

	mask = pow(mask, 3.0);
	mask = pow(mask, 24.0) * 14.0;

	imageStore(outAnamorphic[0], ivec2(In.uv * ScreenResDimensions), mask.xxxx);
}
#endif

#ifdef SMAA_PASS_0
void anamorphicReduction(in const vec2 uv)
{
	float color = 0;
	const float s = ScreenResDimensions.y / 450.0;

	// Horizontal reduction for anamorphic flare.
    for (int x = 0; x < 8; x++)
    {
        color += 0.25 * textureLod(anamorphicMap[0],
            vec2(128.0, 1.0) * uv + (0.5 * s * vec2(float(x) - 3.5f, 0)) * InvScreenResDimensions, 0).r;
    }

	imageStore(outAnamorphic[1], ivec2(In.uv * ScreenResDimensions), color.xxxx);
}
#endif

#ifdef SMAA_PASS_2
vec3 anamorphicFlare(in const vec2 uv)
{
	const float flare = textureLod(anamorphicMap[1], uv / vec2(128, 1), 0).r;

	vec3 color = flare * vec3(1.055, 0.0, 6.0) * 8e-3;

	// Compress dynamic range.
    color.rgb *= 6.0;
	color.rgb = 1.5 * color.rgb / (1.0 + color.rgb);

   return color;
}
#endif

//#ifdef OVERLAY
//const vec3 gui_punk = vec3( 937.254e-3f, 23.594e-3f, 411.764e-3f );
//const vec3 gui_bleed = vec3( 349.019e-3f, 200e-3f, 635.294e-3f );

//const vec3 gui_green = vec3(266.666e-3f, 913.725e-3f, 537.254e-3f); // 0x0089E944  - abgr
//const vec3 gui_bleed = vec3(619.607e-3f, 1.0f, 792.156e-3f);		// 0x00CAFF9E  - abgr
	
//const vec3 gui_bleed = vec3(1.0f, 545.098e-3f, 152.941e-3f);
//const vec3 gui_nixie = vec3(960.784e-3f, 1.0f, 94.1176e-3f);
//#endif

#ifdef SMAA_PASS_2

#define LUT_SIZE_STANDARD 65
const float LUT_SCALE = (LUT_SIZE_STANDARD - 1.0f) / LUT_SIZE_STANDARD;
const float LUT_OFFSET = 1.0f / (2.0f * LUT_SIZE_STANDARD);

#endif


#ifdef HDR
// sRGB to bt.2020 wide gamut
vec3 bt709_to_bt2020(in const vec3 color) // 2020
{
	// using 4 digits values, based of double precision values when rounded
	// see: https://stackoverflow.com/questions/46132625/convert-from-linear-rgb-to-xyz
	// then it is using the official values which are only defined to 4 digits
	// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf
	// 
	// [RGB.2020] = inverse(m) * n * [RGB.709]

	// this uses the reduced to matrix "o" 
	// row major //
	const mat3 xyz_to_bt2020 = mat3(	// values are the inverse matrix
		  1.71650f, -0.66662f,   0.01765f,
		 -0.35558f,  1.61644f,  -0.04281f,
		 -0.25337f,  0.01577f,   0.94208f );
	const mat3 bt709_to_xyz = mat3(
		 0.4124564f, 0.2126729f, 0.0193339f,
		 0.3575761f, 0.7151522f, 0.1191920f,
		 0.1804375f, 0.0721750f, 0.9503041f );

	const mat3 bt709_to_xyz_to_bt2020 = xyz_to_bt2020 * bt709_to_xyz;	// accurate
	/*
	const mat3 bt709_to_xyz_to_bt2020 = mat3( // row major //			// inaccurate!
         0.6274f, 0.0691f, 0.0164f,
         0.3293f, 0.9195f, 0.0880f,
         0.0433f, 0.0114f, 0.8956f);
	*/
	return(bt709_to_xyz_to_bt2020 * color);
}

vec3 PQ(in const vec3 color, in const float display_max_nits) // 2084
{
    // Apply ST2084 curve
    const float m1 = 2610.0f / 4096.0f * 0.25f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;

    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
	const float c1 = c3 - c2 + 1.0f;

	// max(vec3(0), color.xyz) * ((display_max_nits / 80.0f) * (80.0f/10000.0f))
    const vec3 y = pow(max(vec3(0), color.xyz) * ((display_max_nits / 80.0f) * (80.0f/10000.0f)), vec3(m1));

    return ( pow((c1 + c2 * y) / (1.0f + c3 * y), vec3(m2)) );
}
#endif

void main() {

#if defined(TMP_PASS)  // temporal resolve subpass + anamorphic mask downsample + blur mask downsample

	{
		const vec4 color = temporal_aa(In.uv); 
		imageStore(outTemporal, ivec2(In.uv * ScreenResDimensions), color);

		anamorphicMask(color.rgb);
	}
	{ // mask for bloom - bugfix: use the msaa sampler instead of temporal ssaa color provides stability when moving camera, otherwise a pulsing intensity for bloom can be seen - very distractinbg
		vec3 color = textureLod(colorMap, In.uv, 0).rgb;
		expandBlurAA(colorMap, color, In.uv, 0.5f);

		const float maxi = max(color.r, max(color.g, color.b));

		color = dot(color, LUMA) * smoothstep(vec3(0.5f, 0.7f, 0.9f), vec3(1), maxi.xxx); // 0.5, 0.7, 0.9 is good for bloom mask - do not change

		imageStore(outBlur[0], ivec2(In.uv * ScreenResDimensions), vec4(color, 1.0f));		
	}
	
	return; // output of pixel shader not used
#elif defined(SMAA_PASS_0)  // blur downsampled horizontally + anamorphic reduction													
	vec3 color = textureLod(blurMap[0], In.uv, 0).rgb;
	expandAA(blurMap[0], color, In.uv); // <----- keeps things a little bit sharper

	imageStore(outBlur[1], ivec2(In.uv * ScreenResDimensions), vec4(color, 1.0f));

	anamorphicReduction(In.uv);
	return; // output of pixel shader not used
#elif defined (SMAA_PASS_1)   // blur downsampled vertically	           
	vec3 color = textureLod(blurMap[1], In.uv, 0).rgb;
	expandAA(blurMap[1], color, In.uv); // <----- keeps things a little bit sharper

	imageStore(outBlur[0], ivec2(In.uv * ScreenResDimensions), vec4(color, 1.0f));

	return; // output of pixel shader not used
#elif defined (SMAA_PASS_2)	// final pass aa + blur upsample + dithering + anamorphic flare	

	vec3 color = textureLod(colorMap, In.uv, 0).rgb;

	const vec4 temporal_color = textureLod(temporalColorMap, In.uv, 0);
	
	color = mix(color, temporal_color.rgb, 0.5f);  // 1st: average TAA result with MSAA result (provides best looking result - SMAA removed as it wasn't as sharp)

	// 3rd: blur the color so far using the temporal map, based on how dark it is
	// using that result, subtract it from the color using absolute differences
	// this results in very smooth antialiasing
	// scale the luminance back by how much it has changed
	// its so bright it bleeds
	const float luminance = dot(color, LUMA);
	{
		const vec3 reference_color = color;
		
		expandBlurAA(temporalColorMap, color, In.uv, (1.0f - luminance));
		color = clamp(reference_color - abs(reference_color - color), 0.0f, 1.0f); // bugfix - fixes random colors in dark screen locations (negative values)
		color = color * luminance / dot(color, LUMA);
	}
	// AA at this point is nearly perfect, the image has an extremely wide range in contrast and just pops out
	// very happy, backup!
	// save last color for temporal usuage - do not want dithering to add random aliasing
	outLastFrame = vec4(color, temporal_color.a); // must preserve temporal alpha channel in output
	// motion vectors ? color = vec3(normalize(vec2(dFdxFine(temporal_color.a), dFdyFine(temporal_color.a))) * 0.5f + 0.5f, 0.0f);

	{ // 1.) LUT & add in the finalized bloom
	
		// ############# Final Post Processing Pass ###################### //
		// *** 3D LUT *** - apply before dithering and anamorphic flares //
		color = textureLod(lutMap, color * LUT_SCALE + LUT_OFFSET, 0).rgb;
		
		// BLOOM // // *BEST APPLIED ONLY HERE* bloom after the 3D lut is applied (expanding output range) and before any HDR transforms.
		const vec3 bloom = textureLod(blurMap[0], In.uv, 0).rgb;
		const float inv_luma = 1.0f - luminance;
		color = color + dot(bloom, vec3(inv_luma, inv_luma * 0.5f, inv_luma * 0.25f)); //*bugfix - this is correct don't change
	}

	const float luma = dot(color, LUMA); // b4 non-linear transform
	// using textureLod here is better than texelFetch - texelFetch makes the noise appear non "blue", more like white noise
	const float noise_dither = textureLod(noiseMap, vec3(In.uv * ScreenResDimensions * BLUE_NOISE_UV_SCALAR, In.slice), 0).r * BLUE_NOISE_DITHER_SCALAR; // *bluenoise RED channel used* //
	{ // ANAMORPHIC FLAREc
	
		// anamorphic flare minus dithering on these parts to maximize the dynamic range of the flare (could be above 1.0f, subtracting the dither result maximizes the usable range of [0.0f ... 1.0f]
		const vec3 flare = anamorphicFlare(In.uv);
		color += color * clamp( flare + flare * noise_dither, 0.0f, 1.0f);
	}
#ifdef HDR
	{ // 2.) HDR
		// ************* //	
		// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf
		color = pow(color, vec3(2.4f)); // linear to srgb gamma 2.4
		color = bt709_to_bt2020(color);	// srgb/bt.709 to bt.2020 wide gamut color
		color = PQ(color, MaximumNits);	// finally encoded with Perceptual Quantitizer HDR ST2084
										// maximum display nits customizable for user display
	}
#endif
	{ // 3.) *** DITHERING - do not change, validated fft and with highpass to be of extreme quality  (Isolated from temporal AA frames to not introduce any noise into temporal AA process. Only presentation sees the final frame dithered by blue noise)

		// makes colors take advantage of high contrast. darker darks and brighter brights only offset by the bluenoise. This dithers the color and removes banding. Also the
		// perceptual difference is that of seeing more color than there really is.
		color = max(vec3(0), mix(color - noise_dither, color + noise_dither, 1.0f - clamp(luminance / luma, 0.0f, 1.0f))); // shade dithering by luminance (only clamping negative values)
	}
	
	outColor = vec4(color, 1.0f);


	//*** GUI Overlay ***//
#elif defined (OVERLAY)	// overlay final (actual) subpass (gui overlay)

	outColor = subpassLoad(inputGUI);
	
#endif
}




