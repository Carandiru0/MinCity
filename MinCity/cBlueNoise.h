#pragma once

#ifndef BLUENOISE_H
#define BLUENOISE_H

// define BLUENOISE_DIMENSION_SZ before inclusion of this header file
#ifndef BLUENOISE_DIMENSION_SZ
#define BLUENOISE_DIMENSION_SZ 128	// Default 128x128 Texture Size
#endif

// forward decl
namespace vku
{
	class TextureImage2D;
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
		__inline float const						get1D(size_t const frame) const;
		__inline float const						get2D(FXMVECTOR const uv) const;
		__inline float const						get2D(float const u, float const v) const;

		__inline float const* const __restrict		data() const { return(_blueNoise1D); }
		constexpr uint32_t const					size() const { return(DIMENSIONS * DIMENSIONS); }

		// **both channels are available in texture form
		vku::TextureImage2D* const& __restrict		getTexture2D() const { return(_blueNoiseTexture); }

		// initialize //
		void Load(std::wstring_view const blueNoiseFile);

		void Release();
	private:
		vku::TextureImage2D* _blueNoiseTexture;

		alignas(16) float* _blueNoise1D;

	public:
		cBlueNoise();
		~cBlueNoise();
	};

	// noise::blue - global singleton instance
	__declspec(selectany) extern inline cBlueNoise blue{};			// deemed important enough to be a singleton instance accessible globally
									// plays friendlier with cache not being nested by cVoxelWorld singleton, and simplifies access from other classes
									// ** note that _blueNoise is initialized + loaded and released by cVoxelWorld singleton **
									// ** safe to access (globally) const methods only, all methods other than Load() & Release() are const **
} // end ns noise

namespace supernoise
{
	__inline float const cBlueNoise::get1D(size_t const frame) const // supports repeat addressing
	{
		static constexpr uint32_t const BLUENOISE_DIMENSION_SZ_MOD = (BLUENOISE_DIMENSION_SZ * BLUENOISE_DIMENSION_SZ) - 1;

		return(_blueNoise1D[frame & BLUENOISE_DIMENSION_SZ_MOD]);
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
	__inline float const cBlueNoise::get2D(float const u, float const v) const
	{
		return(get2D(XMVectorSet(u, v, 0.0f, 0.0f)));
	}

} // end ns noise
#endif // BLUENOISE_H





