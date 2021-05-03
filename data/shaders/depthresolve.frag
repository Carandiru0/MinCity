#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable

layout(location = 0) out float outFullDepth;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS inputFullDepth;
layout (binding = 1, r32f) writeonly restrict uniform image2D outHalfDepth;

// Downscale Aspect Ratio aware scale of original framebuffer
layout (constant_id = 0) const float DownScaleWidth = 1.0f;
layout (constant_id = 1) const float DownScaleHeight = 1.0f;
#define DownScale vec2(DownScaleWidth, DownScaleHeight)

void main() {
	
	// multisampled depth resolve
	float depth = 0.25f * (subpassLoad(inputFullDepth, 0).r + subpassLoad(inputFullDepth, 1).r + subpassLoad(inputFullDepth, 2).r + subpassLoad(inputFullDepth, 3).r);

	outFullDepth = depth;

	depth = min(depth, subgroupQuadSwapHorizontal(depth));  // <--- this is proper - nice clean edges intersecting volumetrics and rest of scene (correct)
	depth = min(depth, subgroupQuadSwapVertical(depth));

	//depth += subgroupQuadSwapHorizontal(depth);   // <--- incorrect
	//depth += subgroupQuadSwapVertical(depth);
	//depth *= 0.25f;

	//if (gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0)) { // if downscale == 0.5f, doing this is ok, but not always the case in higher resolutions
      imageStore(outHalfDepth, ivec2(gl_FragCoord.xy * DownScale), depth.xxxx);
    //}
	
}




