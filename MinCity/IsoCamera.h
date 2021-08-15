#pragma once
#include <Math/superfastmath.h>
#include <Math/v2_rotation_t.h>
#include "IsoVoxel.h"

// https://en.wikipedia.org/wiki/Isometric_video_game_graphics#/media/File:Graphical_projection_comparison.png
// 35.264 degrees up, 45 degrees left - True Isometric View Projection (requires orthographic projection matrix)
/*        +  eye
	  /   |
	 /    |
	/     |
   +------+
lookat				for 120 units -z, at 35.264 degrees lookat angle, height equals 84.85158 units +y
*/

namespace Iso
{
	// True Isometric:										  { -120.0f, -84.851589f, -120.0f },
	constexpr XMFLOAT3A   const TRUE_ISOMETRIC				= { -120.0f, -84.851589f, -120.0f };
	constexpr XMFLOAT3A   const TRUE_ISOMETRIC_ANGLES		= { 615.472907423280e-3f, XM_PIDIV4, 0.0f };   // in radians

	// 2:1 (Psuedo/Dimetric) Isometric:						  { -120.0f, -69.282032f, -120.0f },
	constexpr XMFLOAT3A   const PSUEDO_ISOMETRIC			= { -120.0f, -69.282032f, -120.0f };
	constexpr XMFLOAT3A   const PSUEDO_ISOMETRIC_ANGLES		= { XM_PIDIV4, XM_PIDIV4, 0.0f };  // in radians

	constexpr XMFLOAT3A   const ISOMETRIC_PERSPECTIVE_USED	= TRUE_ISOMETRIC;
	constexpr XMFLOAT3A   const ISOMETRIC_ANGLES_USED		= TRUE_ISOMETRIC_ANGLES;

	XMGLOBALCONST inline XMVECTORF32 const
		xmEyePt_Iso = { ISOMETRIC_PERSPECTIVE_USED.x, ISOMETRIC_PERSPECTIVE_USED.y, ISOMETRIC_PERSPECTIVE_USED.z, 0.0f },
		xmUp = { 0.0f, 1.0f, 0.0f, 0.0f };

	XMGLOBALCONST inline v2_rotation_t const
		AzimuthIsometric(ISOMETRIC_ANGLES_USED.y);

	constexpr float const
		CAMERA_SCROLL_DISTANCE = 2.5f,
		CAMERA_TRANSLATE_SPEED = SFM::GOLDEN_RATIO * 2.0f, // good speed minimum
		CAMERA_DAMPING = 0.09f;
	constexpr uint32_t const
		CAMERA_DISTANCE_RESET_MILLISECONDS = 50,	// affects speed
		CAMERA_VELOCITY_RESET_SECONDS = 4;			// stopping time

	constexpr uint32_t const
		OFFSCREEN_HEIGHT = (WORLD_GRID_SIZE),
		OFFSCREEN_WIDTH = (OFFSCREEN_HEIGHT << 1);

	constexpr float const MINIMAP_CAMERA_ZOOM = 360.0f;
	constexpr float const MINIMAP_CAMERA_ASPECT = (((float)Iso::OFFSCREEN_WIDTH) / ((float)Iso::OFFSCREEN_HEIGHT));

} // end ns



