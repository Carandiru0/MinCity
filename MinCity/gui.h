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
	 
	// isometric diagonal line draws down-right (+X)
	STATIC_INLINE void __vectorcall draw_x_line(uint32_t length, XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive = false);
	
	// isometric vertical line draws up (-Y)
	STATIC_INLINE void __vectorcall draw_y_line(uint32_t length, XMVECTOR xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive = false);
	
	// isometric diagonal line draws up-right (+Z)
	STATIC_INLINE void __vectorcall draw_z_line(uint32_t length, XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive = false);

	// isometric diagonal string draws up-right (+X)
	template <typename... Args>
	STATIC_INLINE uint32_t const __vectorcall draw_string(XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive, std::string_view const fmt, const Args& ... args);

	STATIC_INLINE_PURE void __vectorcall add_vertical_bar(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_horizontal_bar(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_bouncing_arrow(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_barcode(std::string& str, uint32_t const length, int32_t const seed);

	STATIC_INLINE uint32_t const __vectorcall draw_vertical_progress_bar(uint32_t const bars, float const t, XMVECTOR xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive = false);
	STATIC_INLINE uint32_t const __vectorcall draw_horizontal_progress_bar(uint32_t const bars, float const t, XMVECTOR const xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive = false);

	
} // end ns


// definitions :
namespace gui
{
	namespace internal
	{
		static constexpr uint32_t const mini_voxel_length = 2;

		read_only inline XMVECTORF32 const
			xmX{ Iso::MINI_VOX_STEP,  0.0f,  0.0f, 0.0f }, // isometric diagonal line draws down-right (+X)
			xmY{ 0.0f, -Iso::MINI_VOX_STEP,  0.0f, 0.0f }, // isometric vertical line draws up (-Y) 
			xmZ{ 0.0f,  0.0f,  Iso::MINI_VOX_STEP, 0.0f }; // isometric diagonal line draws up-right (+Z)
	} // end ns

	STATIC_INLINE void __vectorcall draw_x_line(uint32_t length, XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive)	// [right] or [down-right]
	{
		if (0 == length)
			return;

		// 1st pixel
		world::addVoxel(xmLocation, voxelIndex, color, emissive);

		uint32_t mini_length(internal::mini_voxel_length);
		--mini_length;
		if (--length) { // single pixel case, don't want to draw second pixel.

			xmLocation = XMVectorAdd(xmLocation, internal::xmX); // ++x

			while (--length) {

				world::addVoxel(xmLocation, voxelIndex, color, emissive);
				xmLocation = XMVectorAdd(xmLocation, internal::xmX); // ++x

				if (0 == --mini_length) {
					++voxelIndex.x; // next voxel of minivoxels
					mini_length = internal::mini_voxel_length; // reset
				}
			}

			world::addVoxel(xmLocation, voxelIndex, color, emissive); // last
		}
	}

	STATIC_INLINE void __vectorcall draw_y_line(uint32_t length, XMVECTOR xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive) // [up]
	{
		// *no change in voxelIndex is possible when drawing up. voxelIndex represents coordinates for x(right) and z(forward) axis'.
		if (0 == length)
			return;

		// 1st pixel
		world::addVoxel(xmLocation, voxelIndex, color, emissive);
		
		if (--length) { // single pixel case, don't want to draw second pixel.

			xmLocation = XMVectorAdd(xmLocation, internal::xmY); // ++y

			while (--length) {

				world::addVoxel(xmLocation, voxelIndex, color, emissive);
				xmLocation = XMVectorAdd(xmLocation, internal::xmY); // ++y
			}

			world::addVoxel(xmLocation, voxelIndex, color, emissive); // last
		}
	}

	STATIC_INLINE void __vectorcall draw_z_line(uint32_t length, XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive) // [forward] or [up-right]
	{
		if (0 == length)
			return;

		// 1st pixel
		world::addVoxel(xmLocation, voxelIndex, color, emissive);

		uint32_t mini_length(internal::mini_voxel_length);
		--mini_length;
		if (--length) { // single pixel case, don't want to draw second pixel.

			xmLocation = XMVectorAdd(xmLocation, internal::xmZ); // ++z

			while (--length) {

				world::addVoxel(xmLocation, voxelIndex, color, emissive);
				xmLocation = XMVectorAdd(xmLocation, internal::xmZ); // ++z

				if (0 == --mini_length) {
					++voxelIndex.y; // next voxel of minivoxels
					mini_length = internal::mini_voxel_length; // reset
				}
			}

			world::addVoxel(xmLocation, voxelIndex, color, emissive); // last
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

		STATIC_INLINE bool const is_uppercase_style(char const c)
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
			return (std::isupper(c));
		}

		STATIC_INLINE uint32_t const draw_character(XMVECTOR const xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive, char const character)
		{
			uint32_t const offset(uint32_t(character - ' '));

			uint8_t const* const chr(&vxlmono::data[vxlmono::lut[offset]]);

			uint32_t mini_length(internal::mini_voxel_length);
			
			uint32_t max_character_width(0);

			point2D_t voxelOffset;

			float yy(font_height_max);
			for (uint32_t y = 0; y < font_height; ++y, --yy) {

				XMVECTOR const xmOffset1D( SFM::__fma(_mm_set1_ps(yy), internal::xmY, xmLocation) );

				voxelOffset = voxelIndex;

				uint32_t character_width(0), space_width(0);
				float xx = 0.0f;
				for (uint32_t x = 0; x < font_width; ++x, ++xx) {

					if (chr[y] & (1 << x)) { // bit hit

						character_width += space_width;
						while (space_width) {

							XMVECTOR const xmOffset2D(SFM::__fma(_mm_set1_ps(xx - float(space_width)), internal::xmX, xmOffset1D));
							world::addVoxel(xmOffset2D, p2D_sub(voxelOffset, point2D_t((int32_t)space_width, 0)), 0, false); // fills in suitable empty space black, some spots on the uppercase characters are errornously filled @todo
							--space_width;
						}

						XMVECTOR const xmOffset2D(SFM::__fma(_mm_set1_ps(xx), internal::xmX, xmOffset1D));
						world::addVoxel(xmOffset2D, voxelOffset, color, emissive);

						++character_width;
					}
					else if (character_width) { // bit is empty and there was a bit hit before

						++space_width;
					}

					if (0 == --mini_length) {
						++voxelOffset.x; // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}

				max_character_width = SFM::max(max_character_width, character_width);
			}

			return(max_character_width);
		}
	} // end ns

	template <typename... Args>
	STATIC_INLINE uint32_t const __vectorcall draw_string(XMVECTOR xmLocation, point2D_t voxelIndex, uint32_t const color, bool const emissive, std::string_view const fmt, const Args& ... args)
	{
		std::string const str( fmt::format(fmt, args...) );

		char const* pChars = str.data();
		uint32_t mini_length(internal::mini_voxel_length);
		uint32_t string_width(0);
		char character(0), last_character(0);

		while ((character = *pChars)) {

			// is uppercase letter (requires additional spacing
			if (last_character && internal::is_uppercase_style(character)) { // (filled space or just blank space)
				xmLocation = XMVectorAdd(xmLocation, internal::xmX); // space width of one voxel (blank space)
				++string_width;

				if (0 == --mini_length) {
					++voxelIndex.x; // next voxel of minivoxels
					mini_length = internal::mini_voxel_length; // reset
				}
			}

			character = character & 0x7F; // clamp character to maximum of data set
			if (character <= ' ') {	      // & minimum of data set
				// is a space or unrecognized character and not the 1st character
				if (last_character) {
					if (!internal::is_uppercase_style(last_character)) { // (filled space or just blank space)
						internal::draw_character(xmLocation, voxelIndex, color, emissive, 0x5e);
					}
					xmLocation = XMVectorAdd(xmLocation, internal::xmX); // space width of one voxel (blank space)
					++string_width;

					if (0 == --mini_length) {
						++voxelIndex.x; // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}
			}
			else {
				uint32_t const character_width = internal::draw_character(xmLocation, voxelIndex, color, emissive, character);
				xmLocation = SFM::__fma(_mm_set1_ps((float)(character_width)), internal::xmX, xmLocation); // character width of n voxels + 1 space
				string_width += character_width;

				for (uint32_t i = 0; i < character_width; ++i) {
					if (0 == --mini_length) {
						++voxelIndex.x; // next voxel of minivoxels
						mini_length = internal::mini_voxel_length; // reset
					}
				}
			}

			last_character = character;
			++pChars;
		}

		return(string_width);
	}

	STATIC_INLINE_PURE void __vectorcall add_vertical_bar(std::string& str, float const t)
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x20, 0x29, 0x28, 0x27, 0x26, 0x23, 0x22
		};
		static constexpr uint32_t const count(_countof(positions));
		static constexpr float const fcount((float)count);

		float const amount = SFM::lerp(0.0f, fcount, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(SFM::min(amount, fcount - 1.0f));

		str += positions[position];
	}

	STATIC_INLINE_PURE void __vectorcall add_horizontal_bar(std::string& str, float const t)
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x20, 0x5e, 0x5c, 0x3e, 0x3c, 0x3b, 0x22
		};
		static constexpr uint32_t const count(_countof(positions));
		static constexpr float const fcount((float)count);

		float const amount = SFM::lerp(0.0f, fcount, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(SFM::min(amount, fcount - 1.0f));

		str += positions[position];
	}

	STATIC_INLINE uint32_t const __vectorcall draw_vertical_progress_bar(uint32_t const length, float const t, XMVECTOR xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive)
	{
		float tRemaining(((float)length / internal::font_height_max) * SFM::clamp(t, 0.0f, 1.0f));

		while (tRemaining > 0.0f) {
			std::string szBar("");
			add_vertical_bar(szBar, tRemaining);

			draw_string(xmLocation, voxelIndex, color, emissive, szBar);
			xmLocation = SFM::__fma(_mm_set1_ps(internal::font_height_max), internal::xmY, xmLocation);
			--tRemaining;
		}

		return(internal::font_width - 1);
	}
	STATIC_INLINE uint32_t const __vectorcall draw_horizontal_progress_bar(uint32_t const length, float const t, XMVECTOR const xmLocation, point2D_t const voxelIndex, uint32_t const color, bool const emissive)
	{
		std::string szBar("");
		float tRemaining(((float)length / internal::font_width_max) * SFM::clamp(t, 0.0f, 1.0f));

		while (tRemaining > 0.0f) {
			add_horizontal_bar(szBar, tRemaining);
			--tRemaining;
		}

		return( draw_string(xmLocation, voxelIndex, color, emissive, szBar) );
	}

	STATIC_INLINE_PURE void __vectorcall add_bouncing_arrow(std::string& str, float const t)
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x5f, 0x60, 0x7e, 0x60, 0x5f
		};
		static constexpr uint32_t const count(_countof(positions));
		static constexpr float const fcount((float)count);

		float const amount = SFM::lerp(0.0f, fcount, SFM::clamp(t, 0.0f, 1.0f));	// spreads out better
		uint32_t const position = SFM::floor_to_u32(SFM::min(amount, fcount - 1.0f));

		str += positions[position];
	}

	STATIC_INLINE_PURE void __vectorcall add_barcode(std::string& str, uint32_t const length, int32_t const seed)
	{
		static constexpr char const positions[] = {
			// characters in sequential order of animation
			0x7b, 0x7c, 0x7d
		};
		static constexpr uint32_t const count(_countof(positions));

		SetSeed(seed);
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
} // end ns
