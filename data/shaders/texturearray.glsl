#ifndef TEXTUREARRAY_
#define TEXTUREARRAY_

// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
// after modifications are made here
// update cVulkan->cpp (CreateSharedVoxelResources) and cVoxelWorld.cpp (UpdateDescriptorSet_VoxelCommon)
// which adds the textures, and samplers to the "one descriptor set"

// add base textures....
#define TEX_NOISE 0 				// the 0 index is always reserved for special mix of noise texture, linear sampling, repeat, is set
									// .r = value noise   .g = perlin noise   .b = simplex noise
		#define _value r  
		#define _perlin g  
		#define _simplex b  

// ### here:
#define TEX_BLUE_NOISE 1
#define TEX_TERRAIN 2
#define TEX_TERRAIN2 3
#define TEX_GRID 4
#define TEX_BLACKBODY 5

// ### here:
#ifdef __cplusplus

#define SAMPLER_DEFAULT (MinCity::Vulkan->getLinearSampler<eSamplerAddressing::CLAMP>())

#define TEX_NOISE_SAMPLER (MinCity::Vulkan->getLinearSampler<eSamplerAddressing::MIRRORED_REPEAT>())
#define TEX_BLUE_NOISE_SAMPLER (MinCity::Vulkan->getNearestSampler<eSamplerAddressing::REPEAT>())
#define TEX_TERRAIN_SAMPLER (MinCity::Vulkan->getLinearSampler<eSamplerAddressing::REPEAT>())
#define TEX_TERRAIN2_SAMPLER (MinCity::Vulkan->getAnisotropicSampler<eSamplerAddressing::REPEAT>())
#define TEX_GRID_SAMPLER (MinCity::Vulkan->getAnisotropicSampler<eSamplerAddressing::REPEAT>())
#define TEX_BLACKBODY_SAMPLER SAMPLER_DEFAULT

#endif

// update this            _____
#define NUM_BASE_TEXTURES ( 5 )
//                        -----

// don't touch this
#define TEXTURE_ARRAY_LENGTH ( NUM_BASE_TEXTURES + 1 )

// include where required
#ifndef __cplusplus

layout (binding = 6) uniform sampler2DArray _texArray[TEXTURE_ARRAY_LENGTH];


#endif



#endif // TEXTUREARRAY_


