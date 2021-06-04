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
		: _blueNoise1D(nullptr), _blueNoiseTexture(nullptr)
	{

	}

	void cBlueNoise::Load(std::wstring_view const blueNoiseFile)
	{
		ImagingMemoryInstance const* imgBlueNoise = ImagingLoadRawLA(blueNoiseFile, BLUENOISE_DIMENSION_SZ, BLUENOISE_DIMENSION_SZ);

		if (imgBlueNoise) {

			MinCity::TextureBoy.ImagingToTexture_RG<false>(imgBlueNoise, _blueNoiseTexture);
#ifndef NDEBUG
#ifdef DEBUG_EXPORT_BLUENOISE_DUAL_CHANNEL_KTX
			ImagingSaveToKTX(imgBlueNoise, DEBUG_DIR "bluenoise_dual_channel_test.ktx");
#endif
#endif
			// capture first channel for usage outside of gpu texture scope (cpu only)
			_blueNoise1D = (float* const)scalable_aligned_malloc(BLUENOISE_DIMENSION_SZ * BLUENOISE_DIMENSION_SZ * sizeof(float), 16);

			__memclr_aligned_16<BLUENOISE_DIMENSION_SZ* BLUENOISE_DIMENSION_SZ>(_blueNoise1D);

			{
				float* __restrict pOutBytes(_blueNoise1D);
				uint8_t const* __restrict pBytes(imgBlueNoise->block);

				int32_t y(BLUENOISE_DIMENSION_SZ - 1);
				do
				{
					int32_t x(BLUENOISE_DIMENSION_SZ - 1);
					do
					{
						// format is 2bytes, 2 channels - just grabbing 1st channel for 1D array
						*pOutBytes = SFM::u8_to_float(*pBytes);

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
	}

	cBlueNoise::~cBlueNoise()
	{
		Release();
	}

} // end ns noise

