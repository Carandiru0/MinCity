#version 450
#extension GL_GOOGLE_include_directive : enable
#include "shareduniform.glsl"

layout(location = 0) in precise vec4 inPos;

writeonly layout(location = 0) out streamOut
{
	 // must all be xzy
	noperspective vec3	rd;
	noperspective vec3	eyePos;
	flat vec3			eyeDir;
	flat float			slice;

} Out;

// "World Visible Volume"			 // xyz
layout (constant_id = 0) const float WorldDimensions = 0.0f;

void main() {
 			
	const float aa_dimensions = mix(WorldDimensions + 1.0f, WorldDimensions - 1.0f, bool(frame() & 1u)); // this achives temporal antialiasing for the raymarch, especially when reconstruction of the checkerboard resolves
		                                                                                    // *slighlty blurs* which is ok (desired) for the volumetric and reflection outputs

	// this aligned to the origin (0,0,0) - this is properly aligned with depth do not change0
	precise vec3 volume_translation = aa_dimensions.xxx * -0.5f;

	precise const vec3 position = fma(inPos.xyz, aa_dimensions.xxx, volume_translation);

	gl_Position = u._proj * u._view * u._world * vec4(0.5f * position, 1.0f);

	// Compute eye position and ray directions in the unit cube space
	precise vec3 eyePos = u._eyePos.xyz + inPos.xyz; // this perfectly aligns the center of the volume *do not change*
	Out.rd.xzy = normalize(inPos.xyz - eyePos); // should be renormalized in fragment shader
	Out.eyePos.xzy = eyePos;

	// fragment shaders always sample with height being z component
	precise vec3 eyeDir = u._eyeDir.xyz; // isometric camera direction
	eyeDir.y = -eyeDir.y;  // must invert y axis here!! otherwise rotation of camera reveals incorrect depth
	Out.eyeDir.xzy = normalize(eyeDir); // Out.eyeDir is flat, normalized and good to use in fragment shaders
	// **************************** // hybrid rendering alignment, ray marching & rasterization are closely aligned. **DO NOT CHANGE** DEEPLY INVESTIGATED, DO NOT CHANGE!
	Out.slice = frame_to_slice(0.0f); // +blue noise over time (every two frames are matched as raymarch is done one frame checkerboard odd, one frame checkerboard even - the blue noise slice *must* be the same one for both frames (to not create white noise badd!!)
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