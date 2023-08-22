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


 // *remember* to [rebuild] whole solution if any of these values are changed.

 #define MAGIC_SCALAR            (1.0f/16.0f)     // for decoding light color from compute only

 #define VOLUMETRIC_INTENSITY    (4.0f)         // *bugfix: important that intensities are applied to light color only
 #define DIRECT_INTENSITY        (1.0f)       

 #define ATTENUATION_SCALAR      (0.5f)           // *bugfix: should be as close to 1.0 as possible, so that attenuation is not "extending" the range of light by a using a tweaked value. [Tweaking can cause the voronoi edges to show up]

// main public functions:

// for input is world distance units (not normalized) *ONLY* :

// final inverse square law equation used:
// a = 1.0 / (d*d + 1.0)  
float getAttenuation(in const float light_distance) 
{
	return( 1.0f / fma(light_distance,light_distance,1.0f) );
}

#endif // _LIGHT_GLSL


