#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#ifndef BLUENOISE_H
#define BLUENOISE_H

#include <Math/superfastmath.h>
#include <Math/point2D_t.h>

// define BLUENOISE_DIMENSION_SZ before inclusion of this header file
#ifndef BLUENOISE_DIMENSION_SZ
#define BLUENOISE_DIMENSION_SZ 128	// Default (128x128x64) Texture Size		// todo: BLUENOISE_DIMENSION_SZ can now be pulled from the file, should be dynamic and non hard-coded.
#endif

// forward decl
namespace vku
{
	class TextureImage2D;
	class TextureImage2DArray;
}

namespace supernoise
{
	class cBlueNoise
	{
	public:
		static constexpr uint32_t const DIMENSIONS = BLUENOISE_DIMENSION_SZ;
	public:
		// accessors //
		// **only the first channel of loaded bluenoise file is accessible by get1D() get2D()
		// all get1D or get2D methods wrap around, so inputs outside the normal range are ok to use.
		__inline float const						get1D(size_t const frame) const;
		__inline float const						get2D(point2D_t const pixel) const;
		__inline float const						get2D(FXMVECTOR const uv) const;
		__inline float const						get2D(float const u, float const v) const;

		__inline float const* const __restrict		data() const { return(_blueNoise1D); }
		constexpr uint32_t const					size() const { return(DIMENSIONS * DIMENSIONS); }

		// **both channels are available in texture form
		vku::TextureImage2DArray* const& __restrict		getTexture2DArray() const { return(_blueNoiseTextures); }	// 2D Layered Texture (w/ bluenoise over time) [RG]

		// initialize //
		void Load(std::wstring_view const blueNoiseFile);

		void Release();
	private:
		vku::TextureImage2DArray* _blueNoiseTextures;

		float* _blueNoise1D;

	public:
		cBlueNoise();
		~cBlueNoise();
	};

	// supernoise::blue - global singleton instance
	__declspec(selectany) extern inline cBlueNoise blue{};			// deemed important enough to be a singleton instance accessible globally
									// plays friendlier with cache not being nested by cVoxelWorld singleton, and simplifies access from other classes
									// ** note that _blueNoise is initialized + loaded and released by cVoxelWorld singleton **
									// ** safe to access (globally) const methods only, all methods other than Load() & Release() are const **
} // end ns supernoise

namespace supernoise
{
	__inline float const cBlueNoise::get1D(size_t const frame) const // supports repeat addressing
	{
		static constexpr uint32_t const BLUENOISE_DIMENSION_SZ_MOD = (BLUENOISE_DIMENSION_SZ * BLUENOISE_DIMENSION_SZ) - 1;

		return(_blueNoise1D[frame & BLUENOISE_DIMENSION_SZ_MOD]);
	}

	__inline float const cBlueNoise::get2D(point2D_t const pixel) const // supports repeat addressing
	{
		static constexpr int32_t const BLUENOISE_DIMENSION_XY_MOD = BLUENOISE_DIMENSION_SZ - 1;

		return(_blueNoise1D[(pixel.y & BLUENOISE_DIMENSION_XY_MOD) * BLUENOISE_DIMENSION_SZ + (pixel.x & BLUENOISE_DIMENSION_XY_MOD)]);
	}

	__inline float const cBlueNoise::get2D(FXMVECTOR const xmUV) const // supports repeat addressing
	{
		static constexpr uint32_t const BLUENOISE_DIMENSION_XY_MOD = BLUENOISE_DIMENSION_SZ - 1;
		static constexpr float const BLUENOISE_DIMENSION_XY = float(BLUENOISE_DIMENSION_SZ);

		uvec4_v const xmUV_nearest = SFM::floor_to_u32(SFM::__fma(xmUV, _mm_set1_ps(BLUENOISE_DIMENSION_XY), _mm_set1_ps(0.5f)));

		uvec4_t uv_nearest;
		xmUV_nearest.xyzw(uv_nearest);

		return(_blueNoise1D[(uv_nearest.y & BLUENOISE_DIMENSION_XY_MOD) * BLUENOISE_DIMENSION_SZ + (uv_nearest.x & BLUENOISE_DIMENSION_XY_MOD)]);
	}
	__inline float const cBlueNoise::get2D(float const u, float const v) const // supports repeat addressing
	{
		return(get2D(XMVectorSet(u, v, 0.0f, 0.0f)));
	}

} // end ns noise
#endif // BLUENOISE_H





