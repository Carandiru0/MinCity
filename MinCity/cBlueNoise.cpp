#include "pch.h"
#include "cBlueNoise.h"
#include "cTextureBoy.h"
#include "MinCity.h"
#include "cVulkan.h"
#include <Random/superrandom.hpp>
#include <Math/superfastmath.h>

namespace supernoise
{
	cBlueNoise::cBlueNoise()
		: _blueNoise1D(nullptr), _blueNoiseTexture(nullptr), _blueNoiseTextures{}
	{

	}

	void cBlueNoise::Load(std::wstring_view const blueNoiseFile)
	{
		MinCity::TextureBoy->LoadKTXTexture<false>(_blueNoiseTextures, blueNoiseFile); // this loads the bluenoise in to the gpu texture. it should be linear (non-srgb) colorspace.

		ImagingMemoryInstance const* imgBlueNoise = ImagingLoadKTX(blueNoiseFile); // this temporarily loads the blue noise into a LA Imaging Instance (linear)

		if (imgBlueNoise) {

			// save first slice of 2D bluenoise layered image, to gpu RG texture. In some cases only a static 2D bluenoise texture is desirable. generated texture is in linear colorspace
			MinCity::TextureBoy->ImagingToTexture_RG<false, false>(imgBlueNoise, _blueNoiseTexture);

#ifndef NDEBUG
#ifdef DEBUG_EXPORT_BLUENOISE_DUAL_CHANNEL_KTX // SAVED from FILE
			ImagingSaveToKTX(imgBlueNoise, DEBUG_DIR "bluenoise_test_dual_channel.ktx"); // this saves *ONLY* the first layer of the new bluenoise texture (2D Array). RG / LA components.
#endif
#endif
			// capture first channel for usage outside of gpu texture scope (cpu only)
			_blueNoise1D = (float* const)scalable_aligned_malloc(BLUENOISE_DIMENSION_SZ * BLUENOISE_DIMENSION_SZ * sizeof(float), 16);

			memset(_blueNoise1D, 0, BLUENOISE_DIMENSION_SZ * BLUENOISE_DIMENSION_SZ * sizeof(float));

			{
				float* __restrict pOutBytes(_blueNoise1D);
				uint8_t const* __restrict pBytes(imgBlueNoise->block);

				int32_t y(BLUENOISE_DIMENSION_SZ - 1);
				do
				{
					int32_t x(BLUENOISE_DIMENSION_SZ - 1);
					do
					{
						// format is 2bytes, 2 channels - just grabbing 1st channel for 1D array (this is the correct way)
						*pOutBytes = SFM::u8_to_float(*pBytes);	// *do not* mix bluenoise channels to obtain one. 

						++pOutBytes;
						pBytes += 2;

					} while (--x >= 0);
				} while (--y >= 0);
			}

			ImagingDelete(imgBlueNoise);
		}
	}

	void cBlueNoise::Release()
	{
		if (nullptr != _blueNoise1D) {
			scalable_aligned_free(_blueNoise1D); _blueNoise1D = nullptr;
		}
		SAFE_RELEASE_DELETE(_blueNoiseTexture);
		SAFE_RELEASE_DELETE(_blueNoiseTextures);
	}

	cBlueNoise::~cBlueNoise()
	{
		Release();
	}

} // end ns noise

