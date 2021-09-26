#version 450
#extension GL_GOOGLE_include_directive : enable
#include "shareduniform.glsl"

layout(location = 0) in vec4 inPos;

writeonly layout(location = 0) out streamOut
{
	noperspective vec3	rd;
	flat vec3			eyePos;
	flat vec3			eyeDir;
	flat vec3			fractional_offset;
} Out;


layout (constant_id = 0) const float VolumeDimensions_X = 0.0f;
layout (constant_id = 1) const float VolumeDimensions_Y = 0.0f;
layout (constant_id = 2) const float VolumeDimensions_Z = 0.0f;
#define VolumeDimensions vec3(VolumeDimensions_X, VolumeDimensions_Y, VolumeDimensions_Z)

void main() {
 			
	// Compute eye position and ray directions in the unit cube space
	// fragment shaders always sample with height being z component

	const vec3 eyePos = u._eyePos.xyz;
	Out.rd.xzy = normalize(inPos.xyz - eyePos);
	Out.eyePos.xzy = eyePos;

	vec3 eyeDir = u._eyeDir.xyz;
	eyeDir.y = -eyeDir.y;  // must invert y axis here!! otherwise rotation of camera reveals incorrect depth
	eyeDir = normalize(eyeDir);  
	Out.eyeDir.xzy = eyeDir; // Out.eyeDir is flat, normalized and good to use normalized in fragment shader

	Out.fractional_offset = vec3(fractional_offset(), 0.0f) / VolumeDimensions.xzy;

	// volume needs to begin at ground level - this is properly aligned with depth do not change
	const vec3 VolumeScale = VolumeDimensions * 0.5f;

	// **************************** // hybrid rendering alignment, ray marching & rasterization are closely aligned. **DO NOT CHANGE** DEEPLY INVESTIGATED, DO NOT CHANGE!
	const vec3 volume_translation = mix(vec3(-0.5f), vec3(0.0f), eyeDir * 0.5f + 0.5f) - vec3(VolumeScale.x * 0.5f, VolumeScale.y, VolumeScale.z * 0.5f);
	
	// inverted y translation, also put at groundlevel
	gl_Position = u._viewproj * vec4(fma(inPos.xyz, VolumeScale, volume_translation), 1.0f);	
}

/*
void main() {
 
	// volume needs to begin at ground level
	const vec3 VolumeScale = VolumeDimensions * 0.25f;
	
	vec3 volume_translation = 0.5f - (VolumeScale * 0.5f);
	volume_translation.y = VolumeDimensions.y * -0.25f; // inverted y translation fix
	//volume_translation = rotate(volume_translation);

	//const vec3 position = inPos.xyz;
	//gl_Position = u.viewProj * vec4(rotate(position.xyz * VolumeScale, _time) + volume_translation + vec3(pc.packed._fractoffset_x, 0.0f, pc.packed._fractoffset_z), 1.0f);

	vec3 position = inPos.xyz * VolumeScale + volume_translation;
	//position.y = -position.y + VolumeScale.y * 0.5f;
	gl_Position = u.viewProj * vec4(position, 1.0f);

	// Compute eye position and ray directions in the unit cube space ( 242.0f? is bang on)
	const vec3 transformed_eyePos = (u.eyePos - volume_translation) * (vec3(VolumeScale.x, VolumeDimensions.y * 1.85f, VolumeScale.z));

	//fragment shaders always sample with height being z component
	Out.ray_dir = normalize((inPos.xyz) - (transformed_eyePos)).xzy;
	Out.eyePos = (transformed_eyePos).xzy;
	Out.eyeDir = u._eyeDir;
}*/