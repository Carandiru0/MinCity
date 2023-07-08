#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_EXT_shader_8bit_storage : enable

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

layout(early_fragment_tests) in;

#include "screendimensions.glsl"

#define FRAGMENT_IN
#include "voxel_fragment.glsl"

layout(location = 0) out vec4 outColor;
#ifdef MOUSE
layout(location = 1) out vec2 outMouse;
#endif

const uint MAX_LAYERS = 255;

#define READWRITE
#include "sharedbuffer.glsl"

layout(std430, set=0, binding=4) restrict buffer subgroupMaximum { // reset every frame (CPU clear buffer upload to GPU)
  uint8_t subgroup_layer_count_max[];
};

// voxels, accumulating to alpha only
void main() {
  
    outColor.a = 1.0f / 255.0f; // (additive blending is enabled)
#ifdef MOUSE
    outMouse = vec2(0); // required for mouse occlusion query to discern transparency
#endif
    // read last completed maximum transparency layer "depth" and output to color attachment
    // the current blend state causes this is add to the current value in the alpha channel of the destination color attachment
    // this is basically a counter that increments but is in the range of [0.0 ... 1.0]
    // the alpha channel is 8 bits for the main color attachment, so a maximum of 255 transparency layers can be used
    // the range is adaptive from [0.0 ... n], where n is the last completed maximum count of transparency layers used
    // so the more "hits" on the same pixel drawn by the clear mask of a transparent object, the more "weight" is represented.
    // this weight is then used in fragment shaders for transparency and is multiplied to the alpha to give a distributed transparency
    // the resulting transparency is then equally distributed for that pixel, giving a good close approximation to order independent transparency

    // update maximum transparency layer "depth"
    const uint pixel_index = uint(floor(fma(floor(gl_FragCoord.y), ScreenResDimensions.x, floor(gl_FragCoord.x))));   // *bugfix - out of bounds access (gpu-assisted validation error), must floor gl_FragCoord to get correct index

    // for any pixel contained in subgroup, get current value of the pixel count, store the new maximum to all pixels belonging to the subgroup
    // after all pixels are completed in succession the buffer will contain subgroup "blocks" with repective maximum for each subgroup.
    uint new_count_max = min(MAX_LAYERS, uint(subgroup_layer_count_max[pixel_index]) + 1U); // here: new_image_layer_count_max is never equal to zero
    subgroup_layer_count_max[pixel_index] = uint8_t(new_count_max); // inherently atomic, each pixel invocation is distinct
    new_count_max = subgroupMax(new_count_max);

    if (subgroupElect()) { // a single invocation performs the atomic max (representing the maximum for the whole image)
        // store to new maximum representing entire image, after the whole image is completed, all subgroup maximums will be factored into a single value
        atomicMax(b.new_image_layer_count_max, new_count_max);
        // atomicMax returns value *before* comparison. 1st parameter is inout to variable in ssbo, or shared. 2nd parameter is variable to compare with, and if new maximum replaces value stored in parameter 1.
        // this differs from subgroupMax operation, where the return value is the maximum of all shader invocations in subgroup
    }
}


