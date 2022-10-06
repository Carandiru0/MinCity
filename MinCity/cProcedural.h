#pragma once
#include <Utility/class_helper.h>
#include <Noise/supernoise.hpp>

#include <Imaging/Imaging/Imaging.h>
#include <Imaging/Imaging/RBFilter_AVX2.h>
// required forward declarations //
struct ImagingMemoryInstance;
//                                               +-------------------------------------+
//                                               | normalized [0.0f ... 1.0f]          | 
//                                               | u,           v,           in        |
typedef const uint32_t (* const NoiseRenderPassthruFunc_t)(float const, float const, float const, supernoise::interpolator::functor const&);
typedef const uint32_t (* const NoiseRenderFunc_t)(float const, float const, supernoise::interpolator::functor const&);

namespace world
{
	const enum eNOISE_TYPE
	{
		VALUE_NOISE,
		PERLIN_NOISE,
		SIMPLEX_NOISE
	};

	class no_vtable cProcedural : no_copy
	{
	public:
		ImagingMemoryInstance* const __restrict GenerateNoiseImageMixed(uint32_t const size, supernoise::interpolator::functor const& interp);// for permutation of value in red channel, perlin in green channel, simplex in blue channel
		ImagingMemoryInstance* const __restrict GenerateNoiseImage(NoiseRenderPassthruFunc_t noiseRenderfunc, uint32_t const size, supernoise::interpolator::functor const& interp, ImagingMemoryInstance const* const pPassthru);// for custom noise [required - single channel/grayscale], (non-optional) passthru image must be of equal dimensions and single channel. callback recieves current "in" grayscale pixel value normalized in the [0.0f ... 1.0f] range. 
		ImagingMemoryInstance* const __restrict GenerateNoiseImage(NoiseRenderFunc_t noiseRenderfunc, uint32_t const size, supernoise::interpolator::functor const& interp);// for custom noise [required - single channel/grayscale] 
		ImagingMemoryInstance* const __restrict GenerateNoiseImage(uint32_t const noiseType, uint32_t const size, supernoise::interpolator::functor const& interp); // for permutation of value, perlin, or simplex noise
																																						// Input image must be a pow of 2 in dimensions for b-filter
		template<uint32_t const edge_detection = EDGE_COLOR_USE_MAXIMUM, uint32_t const thread_count = RBF_MAX_THREADS>
		ImagingMemoryInstance* const __restrict BilateralFilter(ImagingMemoryInstance* const imageSrc, float const spatial = 0.06f, float const range = 0.045f);

		ImagingMemoryInstance* const __restrict Colorize_TestPattern(ImagingMemoryInstance* const imageSrc);

	private:
		 
	public:
		cProcedural();
		~cProcedural();
	};

	template<uint32_t const edge_detection, uint32_t const thread_count>
	ImagingMemoryInstance* const __restrict  cProcedural::BilateralFilter(ImagingMemoryInstance* const imageSrc, float const spatial, float const range)
	{
		CRBFilterAVX2<edge_detection, thread_count>	bilateral;

		uint32_t const width(imageSrc->xsize), height(imageSrc->ysize);
		if (bilateral.initialize(width, height))
		{
			bilateral.setSigma(spatial, range);

			// TODO: input image and output image must be multiple of 32 pixels  see optimal pitch function
			Imaging tmpFiltered = ImagingNew(MODE_BGRX, width, height);  //must be bgrx input to RB filter (*4 channels)
			if (bilateral.filter(tmpFiltered->block, imageSrc->block, width, height, tmpFiltered->linesize)) {
				return(tmpFiltered);
			}
			else {
				fmt::print("image recursively bilaterally filter FAIL\n");
				return(nullptr);
			}
		}
		else {
			fmt::print("image recursively bilaterally init FAIL\n");
			return(nullptr);
		}

		return(nullptr);
	}

} // end ns world


