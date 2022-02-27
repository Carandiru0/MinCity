#pragma once
#include <Math/superfastmath.h>
#include <Math/v2_rotation_t.h>
#include "IsoVoxel.h"

// https://en.wikipedia.org/wiki/Isometric_video_game_graphics#/media/File:Graphical_projection_comparison.png

namespace Iso
{
	static constexpr double       const EYE_DISTANCE				= (Globals::MAXZ_DEPTH - Globals::MINZ_DEPTH) * -0.5; // *do not change* // (see xmEyePt_Iso below)

	// True Isometric (TOO LOW):								  
	static constexpr XMFLOAT3A   const TRUE_ISOMETRIC_ANGLES		= { 0.52359877559829887308f, 0.52359877559829887308f, 0.0f };   // in radians  x,y,z ( 30 degrees, 30 degrees, 0 degrees )
	static constexpr XMFLOAT3A   const TRUE_ISOMETRIC				= { EYE_DISTANCE * 0.86602540378443864676, EYE_DISTANCE * 0.5, EYE_DISTANCE * 0.5 };		// x,y,z ( d * cos(angle.y), d * sin(angle.x), d * sin(angle.y) )
	

	// 2:1 (Psuedo/Dimetric) Isometric (TOO HIGH):			  
	static constexpr XMFLOAT3A   const PSUEDO_ISOMETRIC_ANGLES		= { XM_PIDIV4, XM_PIDIV4, 0.0f };  // in radians  x,y,z ( 45 degrees, 45 degrees, 0 degrees )
	static constexpr XMFLOAT3A   const PSUEDO_ISOMETRIC			= { EYE_DISTANCE * 0.7071067811865475244, EYE_DISTANCE * 0.7071067811865475244, EYE_DISTANCE * 0.7071067811865475244 }; // x,y,z ( d * cos(angle.y), d * sin(angle.x), d * sin(angle.y) )
	

	// Balanced Isometric (JUST RIGHT):							
	static constexpr XMFLOAT3A   const BALANCED_ISOMETRIC_ANGLES	= { 0.58177641733144319231f, XM_PIDIV4, 0.0f };  // in radians   x,y,z ( 33.333 degrees, 45 degrees, 0 degrees )
	static constexpr XMFLOAT3A   const BALANCED_ISOMETRIC			= { EYE_DISTANCE * 0.7071067811865475244, EYE_DISTANCE * 0.54950897807080603526, EYE_DISTANCE * 0.7071067811865475244 };  // x,y,z ( d * cos(angle.y), d * sin(angle.x), d * sin(angle.y) )

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	static constexpr XMFLOAT3A   const ISOMETRIC_ANGLES_USED		= BALANCED_ISOMETRIC_ANGLES;
	static constexpr XMFLOAT3A   const ISOMETRIC_PERSPECTIVE_USED	= BALANCED_ISOMETRIC;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	read_only inline XMVECTORF32 const
		xmEyePt_Iso = { ISOMETRIC_PERSPECTIVE_USED.x, ISOMETRIC_PERSPECTIVE_USED.y, ISOMETRIC_PERSPECTIVE_USED.z, 0.0f },	// - The eye point is meant to never change, except thru rotation. So it never translates / moves. The world moves instead.
		xmUp = { 0.0f, 1.0f, 0.0f, 0.0f };																					// - The eye distance for the projection used always remains at the same distance. That distance (above) is set so the eye is dead center in the visible frustum.
																															// - The distance from the lookat point and the eye will always be the same. Zooming is decoupled from the eye / view matrix.
	read_only_no_constexpr inline v2_rotation_t const																		// - Only the orthographic projection matrix controls the zoom, or the distance from the eye to the look at. This is a "feature" due to parallel projection.
		AzimuthIsometric(ISOMETRIC_ANGLES_USED.y);																			// - Perspective Projection is not used, which is the norm in 3D graphics. However, since all we care about is "isometric" - orthographic / parallel projection is required for an "isometric" view. 
																															// - Parallel Projection Pros:
																															//    - nearly infinite depth precision (extremely accurate) - vs perspective (limited precision)
																															//    - depth is linear (simpler) - vs perspective (non-linear)
																															//    - raymarch precision++
																															//    - Isometric 2.5D but in this case real 3D.
																															//    - Voxels, 3D Grid Aligned. 
																															//    - Hybrid Rasterization & Raymarch (work extremely well together, precise)
																															// - Cons:
																															//    - no true "perspective" warping of the view. perspective is desirable in 3D graphics, considered 3D.
																															
																															// So it looks pretty fricking 3D to me, or maybe I have no depth perception in my eye sight! 
																															// *or* provably for the special case of a strictly isometric voxel engine - parallel orthographic projection is the best solution!

	static constexpr fp_seconds const
		CAMERA_SCROLL_DELAY = fp_seconds(milliseconds(333));
	static constexpr int32_t const
		CAMERA_SCROLL_DISTANCE_MULTIPLIER = 1;    // 0 would be no multiplier (x1) same as input. 1 would be double multiplier (x2). 2 would be quadruple multiplier (4x)
	static constexpr float const
		CAMERA_TRANSLATE_SPEED = SFM::GOLDEN_RATIO * 2.0f, // good speed minimum
		CAMERA_DAMPING = 0.09f;
	static constexpr uint32_t const
		CAMERA_DISTANCE_RESET_MILLISECONDS = 50,	// affects speed
		CAMERA_VELOCITY_RESET_SECONDS = 4;			// stopping time

} // end ns



