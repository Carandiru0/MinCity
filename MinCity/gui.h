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
	STATIC_INLINE_PURE void __vectorcall add_vertical_bar(std::string& str, float const t);
	template<bool const reversed = false>
	STATIC_INLINE_PURE void __vectorcall add_horizontal_bar(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_bouncing_arrow(std::string& str, float const t);
	STATIC_INLINE_PURE void __vectorcall add_barcode(std::string& str, uint32_t const length, int64_t const seed);
	STATIC_INLINE_PURE void __vectorcall add_cyberpunk_glyph(std::string& str, uint32_t const length, int64_t const seed);
} // end ns


// definitions :
namespace gui
{
	// constants //
	static constexpr uint32_t const color(0x000000FF);  // abgr - rgba backwards

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
