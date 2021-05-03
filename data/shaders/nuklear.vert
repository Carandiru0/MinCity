#version 450
#extension GL_GOOGLE_include_directive : enable
#include "common.glsl"

out gl_PerVertex {
    vec4 gl_Position;
};

layout(binding = 0) restrict readonly uniform UniformBufferObject {
    mat4 projection;
} ubo;

layout(location = 0) in vec4 position_uv;
layout(location = 1) in uvec4 color;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec2 fragGrey;

const float inv255 = 1.0f/255.0f;

#define shade r
#define alpha g
#define shadealpha rg

void main() {
    gl_Position = ubo.projection * vec4(position_uv.xy, 0.0f, 1.0f);

	fragUv = position_uv.zw;

	// normalize color input and convert to greyscale + alpha
	vec4 color_normalized = vec4(color) * inv255;
	vec2 grey = vec2(dot(color_normalized.rgb, LUMA), color_normalized.a);

	// pre-multiply alpha, shade.g (alpha) passes thru
	grey.shade = grey.shade * grey.alpha;

	fragGrey = grey;
}

