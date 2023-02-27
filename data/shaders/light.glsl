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

// for input is world distance units (not normalized) *ONLY* :

// final inverse square law equation used:
// a = 1.0 / sqrt(d*d + 1.0)  [compute shader]   - light color is pre-multiplied by this equation
// b = 1.0 / sqrt(d*d + 1.0)  [fragment shader]  - the light color is multiplied again by this equation
// final attenuation is then b*a*color
// see : https://www.desmos.com/calculator/qlxe8vxuzh
float getAttenuation(in float light_distance)  // this is half of the equation, when light volume is sampled, the other half is calculated. They than combine to make the full equation as above. This seems to be the most accurate for distance.
{
	return( min(1.0f, 1.0f / sqrt(fma(light_distance,light_distance,SINGULARITY_MAX))) );
}



// for input is normalized distance 0.0f ... 1.0f *ONLY* :

// final inverse square law equation used:
// a = 1.0 / sqrt(d*d + 1.0)  [compute shader]   - light color is pre-multiplied by this equation
// b = 1.0 / sqrt(d*d + 1.0)  [fragment shader]  - the light color is multiplied again by this equation
// final attenuation is then b*a*color
// see : https://www.desmos.com/calculator/qlxe8vxuzh
float getAttenuation(in const float normalized_light_distance, in const float volume_length) // this is half of the equation, when light volume is generated, the other half is calculated. They than combine to make the full equation as above. This seems to be the most accurate for distance.
{
	//return( 1.0f - clamp(normalized_light_distance, 0.0f, 1.0f) );
	return( getAttenuation(clamp(normalized_light_distance, 0.0f, 1.0f) * volume_length) );

	// denormalization and scaling to world coordinates (actual world distance)
	//const float d = normalized_light_distance * volume_length * SCALE_FRAGMENT;
	//return( min(1.1f, 1.0f / (fma(d,d,SINGULARITY_MAX))) );                      /// allow slightly higher than 1.0f light hdr values
	//return( 1.0f ); //min(1.0f, 1.0f / sqrt(fma(d,d,SINGULARITY_MAX))) );
}				// light distance (using the lighting.glsl functions the light distance is stored in 16bit snorm 3d volume texture and converted to a friendly normalized unorm float), allow for more precision by using 1.1f as upper limit for attenuation to allow some of the hdr brights thru. 

#endif // _LIGHT_GLSL


