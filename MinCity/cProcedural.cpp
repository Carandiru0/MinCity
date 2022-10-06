#include "pch.h"
#include "globals.h"
#include "cProcedural.h"

#include <Random/superrandom.hpp>



static constexpr float const NOISE_SCALAR_HEIGHT = 12.0f;

static uint32_t const RenderValueNoise_ImagePixel(float const u, float const v, supernoise::interpolator::functor const& interp)
{
	// Get perlin noise for this voxel
	float const fNoiseHeight = supernoise::getValueNoise(NOISE_SCALAR_HEIGHT * (u * 0.5f + 0.5f), NOISE_SCALAR_HEIGHT * (v * 0.5f + 0.5f), interp);

	return(SFM::saturate_to_u8(fNoiseHeight * 255.0f));
}
static uint32_t const RenderPerlinNoise_ImagePixel(float const u, float const v, supernoise::interpolator::functor const& interp)
{
	// Get perlin noise for this voxel
	float const fNoiseHeight = supernoise::getPerlinNoise(NOISE_SCALAR_HEIGHT * (u * 0.5f + 0.5f), NOISE_SCALAR_HEIGHT * (v * 0.5f + 0.5f), 0.0f, interp);

	return(SFM::saturate_to_u8(fNoiseHeight * 255.0f));
}
static uint32_t const RenderSimplexNoise_ImagePixel(float const u, float const v, supernoise::interpolator::functor const& interp)
{
	// Get perlin noise for this voxel
	float const fNoiseHeight = supernoise::getSimplexNoise2D(NOISE_SCALAR_HEIGHT * (u * 0.5f + 0.5f), NOISE_SCALAR_HEIGHT * (v * 0.5f + 0.5f));

	return(SFM::saturate_to_u8(fNoiseHeight * 255.0f));
}
namespace world
{
	cProcedural::cProcedural()
	{

	}

	ImagingMemoryInstance* const __restrict cProcedural::GenerateNoiseImageMixed(
		uint32_t const size,
		supernoise::interpolator::functor const& interp)
	{
		ImagingMemoryInstance* const __restrict imageNoise = ImagingNew(eIMAGINGMODE::MODE_BGRX, size, size);

		struct { // avoid lambda heap
			uint8_t* const* const __restrict image;
			int const size;
			supernoise::interpolator::functor const& interp;
		} const p = { imageNoise->image, imageNoise->xsize, interp };


		tbb::parallel_for(int(0), imageNoise->ysize, [&p](int const y) {

			float const inv_size(1.0f / float(p.size));
			float const v((float)y * inv_size);
			int x = p.size - 1;
			uint8_t* __restrict pOut(p.image[y]);
			do {

				float const u((float)x * inv_size); 
				uint32_t 
					noisePixelValueRed(0),
					noisePixelValueGreen(0),
					noisePixelValueBlue(0);

				tbb::parallel_invoke(
					[&] {
						noisePixelValueRed = RenderValueNoise_ImagePixel(u, v, p.interp);
					},
					[&] {
						noisePixelValueGreen = RenderPerlinNoise_ImagePixel(u, v, p.interp);
					},
					[&] {
						noisePixelValueBlue = RenderSimplexNoise_ImagePixel(u, v, p.interp);
					}
				);

				// order is ABGR (RGBA backwards)
				*reinterpret_cast<uint32_t* const>(pOut) = /*(noisePixelValueAlpha << 24) |*/ (noisePixelValueBlue << 16) | (noisePixelValueGreen << 8) | noisePixelValueRed;

				pOut += 4;

			} while (--x >= 0);

			});

		return(imageNoise);
	}

	ImagingMemoryInstance* const __restrict cProcedural::GenerateNoiseImage(NoiseRenderPassthruFunc_t noiseRenderfunc, 
																			uint32_t const size,
																			supernoise::interpolator::functor const& interp,
		                                                                    ImagingMemoryInstance const* const pPassthru)
	{
		static constexpr float const INV_255 = 1.0f / 255.0f;

		ImagingMemoryInstance* const __restrict imageNoise = ImagingNew(eIMAGINGMODE::MODE_L, size, size);

		struct { // avoid lambda heap
			uint8_t const* const* const __restrict image_in;
			uint8_t* const* const __restrict       image_out;
			int const size;
			NoiseRenderPassthruFunc_t const noiseRenderfunc;
			supernoise::interpolator::functor const& interp;
		} const p = { pPassthru->image, imageNoise->image, imageNoise->xsize, noiseRenderfunc, interp };


		tbb::parallel_for(int(0), imageNoise->ysize, [&p](int const y) {

			float const inv_size(1.0f / float(p.size));
			float const v((float)y * inv_size);
			int x = p.size - 1;
			uint8_t const* __restrict pIn(p.image_in[y]);
			uint8_t* __restrict pOut(p.image_out[y]);
			do {

				*pOut = p.noiseRenderfunc((float)x * inv_size, v, INV_255 * ((float)*pIn), p.interp);

				++pOut;
				++pIn;
				
			} while (--x >= 0);

		});

		return(imageNoise);
	}

	ImagingMemoryInstance* const __restrict cProcedural::GenerateNoiseImage(NoiseRenderFunc_t noiseRenderfunc,
		                                                                    uint32_t const size,
		                                                                    supernoise::interpolator::functor const& interp)
	{
		ImagingMemoryInstance* const __restrict imageNoise = ImagingNew(eIMAGINGMODE::MODE_L, size, size);

		struct { // avoid lambda heap
			uint8_t* const* const __restrict       image_out;
			int const size;
			NoiseRenderFunc_t const noiseRenderfunc;
			supernoise::interpolator::functor const& interp;
		} const p = { imageNoise->image, imageNoise->xsize, noiseRenderfunc, interp };


		tbb::parallel_for(int(0), imageNoise->ysize, [&p](int const y) {

			float const inv_size(1.0f / float(p.size));
			float const v((float)y * inv_size);
			int x = p.size - 1;

			uint8_t* __restrict pOut(p.image_out[y]);
			do {

				*pOut = p.noiseRenderfunc((float)x * inv_size, v, p.interp);

				++pOut;

			} while (--x >= 0);

			});

		return(imageNoise);
	}

	ImagingMemoryInstance* const __restrict cProcedural::GenerateNoiseImage(uint32_t const noiseType, uint32_t const size, supernoise::interpolator::functor const& interp)
	{
		ImagingMemoryInstance* const __restrict imageNoise = ImagingNew(eIMAGINGMODE::MODE_L, size, size);

		// unconst
		typedef const uint32_t (* NoiseRenderFunc_t_unconst)(float const, float const, supernoise::interpolator::functor const&);

		NoiseRenderFunc_t_unconst noiseRenderfunc(&RenderValueNoise_ImagePixel);

		switch (noiseType)
		{
		case VALUE_NOISE:
			noiseRenderfunc = RenderValueNoise_ImagePixel;
			break;
		case PERLIN_NOISE:
			noiseRenderfunc = RenderPerlinNoise_ImagePixel;
			break;
		case SIMPLEX_NOISE:
			noiseRenderfunc = RenderSimplexNoise_ImagePixel;
			break;
		}	

		return(GenerateNoiseImage(noiseRenderfunc, size, interp));
	}

	ImagingMemoryInstance* const __restrict cProcedural::Colorize_TestPattern(ImagingMemoryInstance* const imageSrc)
	{
		struct { // avoid lambda heap
			uint8_t* const* const __restrict image;
			int const xsize;
		} const p = { imageSrc->image, imageSrc->xsize };


		tbb::parallel_for(int(0), imageSrc->ysize, [&p](int y) {

			int x = p.xsize - 1;
			uint8_t* __restrict pOut(p.image[y]);
			do {

				uint32_t RGB[3];

				uint32_t const alpha = ((((uint32_t)*(pOut + 3)) + 1) >> 7);

				RGB[0] = (uint32_t)(255.0f * supernoise::getValueNoise(((float)alpha*x) * 0.0033f, ((float)alpha*y) * 0.0034f, supernoise::interpolator::SmoothStep()));
				RGB[1] = (uint32_t)(255.0f * supernoise::getValueNoise(((float)alpha*x) * 0.0045f, ((float)alpha*y) * 0.00455f, supernoise::interpolator::SmoothStep()));
				RGB[2] = (uint32_t)(255.0f * supernoise::getValueNoise(((float)alpha*x) * 0.004f, ((float)alpha*y) * 0.0025f, supernoise::interpolator::SmoothStep()));

				*pOut++ = SFM::max(RGB[0] << 1U, 16U);
				*pOut++ = SFM::max(RGB[1] << 1U, 16U);
				*pOut++ = SFM::max(RGB[2] << 1U, 16U);
				*pOut++; // height/noise remains in alpha channel
				// alpha is untouched

			} while (--x >= 0);

		});

		return(imageSrc);
	}
	cProcedural::~cProcedural()
	{
	}
} // end ns world