#version 460
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
layout(location = 1) out vec4 fragColor;

const float inv255 = 1.0f/255.0f;

void main() {
    gl_Position = ubo.projection * vec4(position_uv.xy, 0.0f, 1.0f);

	fragUv = position_uv.zw;

	// pre-multiply alpha, (alpha) passes thru
	vec4 color_norm = vec4(color) * inv255;
	color_norm.rgb = color_norm.rgb * color_norm.a;

	fragColor = color_norm;
}

