#ifndef _LIGHT_GLSL
#define _LIGHT_GLSL

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

// main public functions:
#define SINGULARITY_MAX (0.61803398874989484820 * 0.001f) // specify singularity constant 1 / (zero) - should be less than or equal to 1.0f but never zero!)

// final inverse square law equation used:
// a = 1.0 / sqrt(d*d + 1.0)  [compute shader]   - light color is pre-multiplied by this equation
// b = 1.0 / sqrt(d*d + 1.0)  [fragment shader]  - the light color is multiplied again by this equation
// final attenuation is then b*a*color
// see : https://www.desmos.com/calculator/qlxe8vxuzh
float getAttenuation(in const float light_distance, in const float vox_size)  // this is half of the equation, when light volume is sampled, the other half is calculated. They than combine to make the full equation as above. This seems to be the most accurate for distance.
{
	const float d = light_distance*vox_size;
	return( 0.5f * min(1.0f, 1.0f / sqrt(fma(d,d,SINGULARITY_MAX))) ); // ~= sqrt(fma(light_distance,light_distance,SINGULARITY_MAX))) );
}				// light is stored in 16bit 3d volume texture, allow for more precision by using 2.0 as upper limit 

// final inverse square law equation used:
// a = 1.0 / sqrt(d*d + 1.0)  [compute shader]   - light color is pre-multiplied by this equation
// b = 1.0 / sqrt(d*d + 1.0)  [fragment shader]  - the light color is multiplied again by this equation
// final attenuation is then b*a*color
// see : https://www.desmos.com/calculator/qlxe8vxuzh
float getAttenuation(in const float normalized_light_distance, in const float volume_length, in const float vox_size) // this is half of the equation, when light volume is generated, the other half is calculated. They than combine to make the full equation as above. This seems to be the most accurate for distance.
{
	// denormalization and scaling to world coordinates (actual world distance)
	return( getAttenuation(normalized_light_distance * volume_length, vox_size) );
}				// light is stored in 16bit 3d volume texture, allow for more precision by using 2.0 as upper limit 

#endif // _LIGHT_GLSL


