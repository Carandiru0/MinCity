#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable

layout(location = 0) out float outFullDepth;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS inputFullDepth;
layout (binding = 1, r32f) writeonly restrict uniform image2D outHalfDepth;

layout(location = 0) in streamIn /*not used, required for validation*/
{
	readonly vec2		uv;
} In;

void main() {
	
	// multisampled depth resolve
	const float samp[4] = { subpassLoad(inputFullDepth, 0).r, subpassLoad(inputFullDepth, 1).r, subpassLoad(inputFullDepth, 2).r, subpassLoad(inputFullDepth, 3).r };
	float depth = min(min(min(samp[0], samp[1]), samp[2]), samp[3]);

	outFullDepth = depth;

	depth = min(depth, subgroupQuadSwapHorizontal(depth));  // <--- this is proper - nice clean edges intersecting volumetrics and rest of s111cene (correct)
	depth = min(depth, subgroupQuadSwapVertical(depth));
	depth = min(depth, subgroupQuadSwapDiagonal(depth)); // EXTRA SAMPLE!
	
	//depth += subgroupQuadSwapHorizontal(depth);   // <--- incorrect
	//depth += subgroupQuadSwapVertical(depth);
	//depth *= 0.25f;

	//if (gl_SubgroupInvocationID == subgroupQuadBroadcast(gl_SubgroupInvocationID, 0)) { // downscale is always 50%, doing this is ok
    //  imageStore(outHalfDepth, ivec2(gl_FragCoord.xy) >> 1, depth.xxxx);
    //}
	
	ivec2 pixel = (ivec2(gl_FragCoord.xy) >> 2) * 3; // start - must match getDownResolution() in vku_addon.hpp   *set for 75% downscale, 3 out of 4 pixels*
	pixel += ivec2(ceil(fract(gl_FragCoord.xy / 4.0f) * 4.0f));   // +0, +1, +2, +3 offset from start

	imageStore(outHalfDepth, pixel, depth.xxxx);
}




