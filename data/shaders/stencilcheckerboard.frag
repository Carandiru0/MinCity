#version 450

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

layout(location = 0) in streamIn /*not used, required for validation*/
{
	readonly vec2		uv;
} In;

#ifdef EVEN
#define OP !
#else
#define OP 
#endif

#define QUAD_SZ 2U

void main() {
	
	const uvec2 uFragCoord = uvec2(floor(gl_FragCoord.xy));

	if ( OP bool( (uFragCoord.x & QUAD_SZ) - (uFragCoord.y & QUAD_SZ) ) )
		discard;	
}




