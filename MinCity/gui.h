#pragma once
#include "globals.h"
#include "world.h"
#include <Math/superfastmath.h>
#include <fmt/fmt.h>
#include <string>
#include <Random/superrandom.hpp>
#include "vxlmono.h"

// declarations :
namespace gui
{
	// *notes:
	// - [length] is in minivoxel units, so if your units are voxels you need to double the length passed in.
	// - [xmLocation] must be transformed into world coordinates by the world origin, before passing in.
	// - [color] the alpha channel works too.
	// - [emissive] makes the minivoxels all lightsources.
	 
	// isometric diagonal line draws down-right (+X), // isometric vertical line draws up (-Y), // isometric diagonal line draws up-right (+Z)
	STATIC_INLINE void __vectorcall draw_line(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t length, uint32_t const flags = 0);
	
	template <bool const query_only = false, typename... Args>
	STATIC_INLINE auto draw_string(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t const flags, std::string_view const fmt, const Args& ... args) -> std::pair<float const, uint32_t const> const;

	STATIC_INLINE_PURE void __vectorcall add_vertical_bar(std::string& str, float const t);
	template<bool const reversed = false>
	STATIC_INLINE_PURE void __vectorcall add_horizontal_bar(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_bouncing_arrow(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_barcode(std::string& str, uint32_t const length, int64_t const seed);
	STATIC_INLINE_PURE void __vectorcall add_cyberpunk_glyph(std::string& str, uint32_t const length, int64_t const seed);

	STATIC_INLINE uint32_t const __vectorcall draw_vertical_progress_bar(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t const bars, float const t, uint32_t const flags = 0);
	STATIC_INLINE uint32_t const __vectorcall draw_horizontal_progress_bar(uint32_t const axis, XMVECTOR const xmLocation, uint32_t const color, uint32_t const bars, float const t, uint32_t const flags = 0);

	
} // end ns


// definitions :
namespace gui
{
	// constants //
	static constexpr uint32_t const color(0x000000FF);  // abgr - rgba backwards

	namespace flags {

		static constexpr uint8_t const // must match constants in Iso::mini (IsoVoxel.h)
			emissive = Iso::mini::emissive,
			hidden = Iso::mini::hidden;

	} // end ns

	namespace axis
	{
		read_only inline uint32_t const 
			x  = 0, y  = 1, z  = 2,
			xn = 3, yn = 4, zn = 5;

	} // end ns

	namespace internal
	{
		namespace axis
		{
			read_only inline XMVECTORF32 const v[] = {
				/*x*/{ Iso::MINI_VOX_STEP,  0.0f,  0.0f, 0.0f }, // isometric diagonal line draws down-right (+X)
				/*y*/{ 0.0f, -Iso::MINI_VOX_STEP,  0.0f, 0.0f }, // isometric vertical line draws up (-Y) 
				/*z*/{ 0.0f,  0.0f,  Iso::MINI_VOX_STEP, 0.0f }, // isometric diagonal line draws up-right (+Z)
				/*xn*/{ -Iso::MINI_VOX_STEP,  0.0f,  0.0f, 0.0f }, // isometric diagonal line draws down-right (+X)
				/*yn*/{ 0.0f, Iso::MINI_VOX_STEP,  0.0f, 0.0f }, // isometric vertical line draws up (-Y) 
				/*zn*/{ 0.0f,  0.0f, -Iso::MINI_VOX_STEP, 0.0f } // isometric diagonal line draws up-right (+Z)
			};

			read_only_no_constexpr inline point2D_t p[] = {
				/*x*/{ 1, 0 },
				/*y*/{ 0, 0 },
				/*z*/{ 0, 1 },
				/*xn*/{ -1, 0 },
				/*yn*/{ 0, 0 },
				/*zn*/{ 0, -1 }
			};
		}
		static constexpr uint32_t const mini_voxel_length = 2;
	} // end ns

	STATIC_INLINE void __vectorcall draw_line(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t length, uint32_t const flags) // [forward] or [up-right]
	{
		if (0 == length)
			return;
		
		point2D_t voxelIndex(v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLocation)));

		XMVECTOR const xmAxis(internal::axis::v[axis].v);
		point2D_t const ptAxis(internal::axis::p[axis]);

		// 1st pixel
		world::addVoxel(xmLocation, voxelIndex, color, flags);

		uint32_t mini_length(internal::mini_voxel_length);
		--mini_length;
		if (--length) { // single pixel case, don't want to draw second pixel.

			xmLocation = XMVectorAdd(xmLocation, xmAxis); // ++z

			while (--length) {

				world::addVoxel(xmLocation, voxelIndex, color, flags);
				xmLocation = XMVectorAdd(xmLocation, xmAxis); // ++z

				if (0 == --mini_length) {
					voxelIndex = p2D_add(voxelIndex, ptAxis); // next voxel of minivoxels
					mini_length = internal::mini_voxel_length; // reset
				}
			}

			world::addVoxel(xmLocation, voxelIndex, color, flags); // last
		}
	}

	namespace internal {

		// vxlmono
		   // Font Size: 7x6px
		   // offset = ascii_code(character) - ascii_code(' ')
		   // data = vxlmono[lut[offset]]

		static constexpr uint32_t const
			font_width = 7,
			font_height = 6;
		static constexpr float const
			font_width_max = float(font_width - 1),
			font_height_max = float(font_height - 1);

		STATIC_INLINE_PURE bool const is_uppercase_style(char const c)
		{
			switch (c)
			{
			case '*':
			case '+':
			case ',':
			case '-':
			case '.':
			case '/':
			case ':':
			case '=':
			case '[':
			case ']':
				return(true);
			}
			return( std::isupper(c) );// (c & (1 << 5)); // isupper includes numbers....
		}

		template <bool const query_only = false>
		STATIC_INLINE auto draw_character(FXMVECTOR xmAxis, XMVECTOR const xmLocation, point2D_t const ptAxis, point2D_t const voxelIndex, uint32_t const color, uint32_t const flags, char const character) -> std::pair<float const, uint32_t const> const
		{
			uint32_t visible(0), count(0);

			uint32_t const offset(uint32_t(character - ' '));

			uint8_t const* const chr(&vxlmono::data[vxlmono::lut[offset]]);

			uint32_t mini_length(internal::mini_voxel_length);
			
			uint32_t max_character_width(0);

			point2D_t voxelOffset;

			float yy(font_height_max);
			for (uint32_t y = 0; y < font_height; ++y, --yy) {

				XMVECTOR const xmOffset1D( SFM::__fma(_mm_set1_ps(yy), internal::axis::v[gui::axis::y], xmLocation) );

				voxelOffset = voxelIndex;

				uint32_t character_width(0), space_width(0);
				float xx = 0.0f;
				for (uint32_t x = 0; x < font_width; ++x, ++xx) {

					if (chr[y] & (1 << x)) { // bit hit

						character_width += space_width;
						while (space_width) {

							XMVECTOR const xmOffset2D(SFM::__fma(_mm_set1_ps(xx - float(space_width)), xmAxis, xmOffset1D));

							if constexpr (!query_only) {
								visible += (uint32_t)world::addVoxel(xmOffset2D, p2D_sub(voxelOffset, point2D_t((int32_t)space_width, 0)), 0); // fills in suitable empty space black
							}
							else {
								visible += (uint32_t)world::isVoxelVisible(xmOffset2D, Iso::MINI_VOX_RADIUS);
							}
							++count;
							--space_width;
						}

						XMVECTOR const xmOffset2D(SFM::__fma(_mm_set1_ps(xx), xmAxis, xmOffset1D));

						if constexpr (!query_only) {
							visible += (uint32_t)world::addVoxel(xmOffset2D, voxelOffset, color, flags);
						}
						else {
							visible += (uint32_t)world::isVoxelVisible(xmOffset2D, Iso::MINI_VOX_RADIUS);
						}
						++count;
						++character_width;
					}
					else if (character_width) { // bit is empty and there was a bit hit before

						++space_width;
					}

					if (0 == --mini_length) {
						voxelOffset = p2D_add(voxelOffset, ptAxis); // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}

				max_character_width = SFM::max(max_character_width, character_width);
			}

			return{ ((float)visible / (float)count), max_character_width };
		}
	} // end ns

	template <bool const query_only, typename... Args>
	STATIC_INLINE auto draw_string(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t const flags, std::string_view const fmt, const Args& ... args) -> std::pair<float const, uint32_t const> const
	{
		std::string const str( fmt::format(fmt, args...) );
		float visible(0.0f), count(0.0f);
		char const* pChars = str.data();
		uint32_t mini_length(internal::mini_voxel_length);
		uint32_t string_width(0);
		char character(0), last_character(0);

		point2D_t voxelIndex(v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLocation)));

		XMVECTOR const xmAxis(internal::axis::v[axis].v);
		point2D_t const ptAxis(internal::axis::p[axis]);

		while ((character = *pChars)) {

			// is uppercase letter (requires additional spacing
			if (last_character && internal::is_uppercase_style(character)) { // (filled space or just blank space)
				xmLocation = XMVectorAdd(xmLocation, xmAxis); // space width of one voxel (blank space)
				++string_width;

				if (0 == --mini_length) {
					voxelIndex = p2D_add(voxelIndex, ptAxis); // next voxel of minivoxels
					mini_length = internal::mini_voxel_length; // reset
				}
			}

			character = character & 0x7F; // clamp character to maximum of data set
			if (character <= ' ') {	      // & minimum of data set
				// is a space or unrecognized character and not the 1st character
				if (last_character) {
					if (!internal::is_uppercase_style(last_character)) { // (filled space or just blank space)
						auto const [visibility, character_width] = internal::draw_character<query_only>(xmAxis, xmLocation, ptAxis, voxelIndex, color, flags, 0x5e);
						visible += visibility;
						++count;
					}
					xmLocation = XMVectorAdd(xmLocation, xmAxis); // space width of one voxel (blank space)
					++string_width;

					if (0 == --mini_length) {
						voxelIndex = p2D_add(voxelIndex, ptAxis); // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}
			}
			else {
				auto const [visibility, character_width] = internal::draw_character<query_only>(xmAxis, xmLocation, ptAxis, voxelIndex, color, flags, character);
				visible += visibility;
				++count;

				xmLocation = SFM::__fma(_mm_set1_ps((float)(character_width)), xmAxis, xmLocation); // character width of n voxels + 1 space
				string_width += character_width;

				for (uint32_t i = 0; i < character_width; ++i) {
					if (0 == --mini_length) {
						voxelIndex = p2D_add(voxelIndex, ptAxis); // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}
			}

			last_character = character;
			++pChars;
		}

		return{ (visible / count), string_width };
	}

	// w/ vxlmono font
	STATIC_INLINE_PURE void __vectorcall add_vertical_bar(std::string& str, float const t)  // number, fractional
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x20, 0x29, 0x28, 0x27, 0x26, 0x23, 0x22
		};
		static constexpr uint32_t const count(_countof(positions) - 1);
		static constexpr float const index((float)count);

		float const amount = SFM::lerp(0.0f, index, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(amount);

		str += positions[position];
	}

	// w/ vxlmono font
	template<bool const reversed>
	STATIC_INLINE_PURE void __vectorcall add_horizontal_bar(std::string& str, float const t) // number, fractional
	{
		static constexpr uint32_t const position_count = 7;

		static constexpr uint32_t const count(position_count - 1);
		static constexpr float const index((float)count);

		float const amount = SFM::lerp(0.0f, index, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(amount);

		if constexpr (reversed) {

			static constexpr char const positions_backward[position_count] = {
				// characters in reversed sequential order of animation
				0x22, 0x3b, 0x3c, 0x3e, 0x5c, 0x5e, 0x20
			};
			str += positions_backward[position];
		}
		else {

			static constexpr char const positions_forward[position_count] = {
				// characters in sequential order of animation
				0x20, 0x5e, 0x5c, 0x3e, 0x3c, 0x3b, 0x22
			};
			str += positions_forward[position];
		}
	}

	STATIC_INLINE uint32_t const __vectorcall draw_vertical_progress_bar(uint32_t const axis, XMVECTOR xmLocation, uint32_t const color, uint32_t const length, float const t, uint32_t const flags)
	{
		float tRemaining(((float)length / internal::font_height_max) * SFM::clamp(t, 0.0f, 1.0f));

		while (tRemaining > 0.0f) {
			std::string szBar("");
			add_vertical_bar(szBar, tRemaining);

			draw_string(axis, xmLocation, color, flags, szBar);
			xmLocation = SFM::__fma(_mm_set1_ps(internal::font_height_max), internal::axis::v[gui::axis::y], xmLocation);
			--tRemaining;
		}

		return(internal::font_width - 1);
	}
	STATIC_INLINE uint32_t const __vectorcall draw_horizontal_progress_bar(uint32_t const axis, XMVECTOR const xmLocation, uint32_t const color, uint32_t const length, float const t, uint32_t const flags)
	{
		std::string szBar("");
		float tRemaining(((float)length / internal::font_width_max) * SFM::clamp(t, 0.0f, 1.0f));

		while (tRemaining > 0.0f) {
			add_horizontal_bar(szBar, tRemaining);
			--tRemaining;
		}

		auto const [visibility, width] = draw_string(axis, xmLocation, color, flags, szBar);

		return( width );
	}

	// w/ vxlmono font
	STATIC_INLINE_PURE void __vectorcall add_bouncing_arrow(std::string& str, float const t) // w/ vxlmono font
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x5f, 0x60, 0x7e, 0x60, 0x5f
		};
		static constexpr uint32_t const count(_countof(positions) - 1);
		static constexpr float const index((float)count);

		float const amount = SFM::lerp(0.0f, index, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(amount);

		str += positions[position];
	}

	// w/ vxlmono font
	STATIC_INLINE void __vectorcall add_barcode(std::string& str, uint32_t const length, int64_t const seed = 0) // optional seed to set b4 string of length is generated - seed can be set exterior to this function instead
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x7b, 0x7c, 0x7d
		};
		static constexpr uint32_t const count(_countof(positions));

		if (0 != seed) {
			SetSeed(seed);
		}
		for (uint32_t i = 0; i < length; ++i) {

			uint32_t const position = PsuedoRandomNumber(0, count - 1);
			if (position & 1) {
				str += positions[position];
			}
			else { // must double to escape '{' and '}' special characters to "{{" and "}}"
				str += positions[position];
				str += positions[position];
			}
		}
	}

	read_only inline char const sSs[]{0x17,0x17,0x17,0x00}; // null terminated sSs logo string
	constinit static inline char const cyberpunk_glyphs[] = {
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a
	};
	// w/ vxlmono font
	STATIC_INLINE void __vectorcall add_cyberpunk_glyph(std::string& str, uint32_t const length, int64_t const seed = 0) // optional seed to set b4 string of length is generated - seed can be set exterior to this function instead
	{
		static constexpr uint32_t const count(_countof(cyberpunk_glyphs));

		if (0 != seed) {
			SetSeed(seed);
		}
		for (uint32_t i = 0; i < length; ++i) {

			uint32_t const position = PsuedoRandomNumber(0, count - 1);
			str += cyberpunk_glyphs[position];
		}

	}
} // end ns
