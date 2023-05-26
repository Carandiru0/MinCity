/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#include "pch.h"

#include "Declarations.h"
#include "sBatched.h"
#include "sBatchedByIndexIn.h"
#include "sBatchedByIndexOut.h"
#include "cVoxelWorld.h"
#include "MinCity.h"
#include "IsoCamera.h"
#include "cTextureBoy.h"
#include "cProcedural.h"
#include <Noise/supernoise.hpp>
#include <Random/superrandom.hpp>
#include <Imaging/Imaging/Imaging.h>
#include <Utility/bit_volume.h>
#include <Utility/bit_row.h>
#include "cNuklear.h"
#include "cPostProcess.h"
#include "adjacency.h"
#include "cPhysics.h"
#include <Utility/async_long_task.h>
#include "Interpolator.h"

#define V2_ROTATION_IMPLEMENTATION
#include "voxelAlloc.h"
#include "voxelModel.h"
#include "eVoxelModels.h"
#include "cUserInterface.h"
#include "cCity.h"

#include "Adjacency.h"
#include "eDirection.h"

#include "cExplosionGameObject.h"
#include "cCharacterGameObject.h"

#include <queue>
#include <tracy.h>

// for the texture indices used in large texture array
#include "../Data/Shaders/texturearray.glsl"

#pragma intrinsic(_InterlockedExchangePointer)
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)
#pragma intrinsic(_BitScanReverse64) // MSB set
#pragma intrinsic(_BitScanForward64) // LSB set

using namespace world;

// From default point of view (isometric):
/*
										      x
							  [  N  ]<--             -->[  E  ]
					  x       			------x------ 	             x  
							  [  W  ]<--             -->[  S  ]
											  x  

*/

namespace // private to this file (anonymous)
{
	static inline struct CameraEntity /* note - changing camera elevation (y) fucks up the raymarch and causes strange behaviour, be warned. */
	{
		static constexpr fp_seconds const TRANSITION_TIME = fp_seconds(milliseconds(32));

		interpolated<XMFLOAT3A> Origin;  // always stored swizzled to x,z coordinates
		XMFLOAT3A voxelFractionalGridOffset; // always stored swizzled to x,z coordinates

		point2D_t
			voxelIndex_TopLeft,
			voxelIndex_Center;
		
		v2_rotation_t Yaw;
		float PrevYawAngle, TargetYawAngle;	

		XMFLOAT2A PrevPosition, TargetPosition;

		XMFLOAT3A ZoomExtents;
		
		float InitialDistanceToTarget;

		float ZoomFactor;

		float PrevZoomFactor,
			  TargetZoomFactor;

		tTime tTranslateStart,
			  tRotateStart,
			  tZoomStart;

		bool Motion,
			 ZoomToExtentsInitialDirection,
			 ZoomToExtents;

		CameraEntity()
			:
			Origin{}, // virtual coordinates
			voxelFractionalGridOffset{},
			ZoomExtents{},
			ZoomFactor(Globals::DEFAULT_ZOOM_SCALAR),
			tTranslateStart{ zero_time_point },
			tRotateStart{ zero_time_point },
			tZoomStart{ zero_time_point },
			Motion(false), 
			ZoomToExtentsInitialDirection(false),
			ZoomToExtents(false)
		{
			Interpolator.push(Origin);
			Interpolator.reset(Origin, XMVectorZero());
		}

		void reset()
		{
			XMStoreFloat3A(&voxelFractionalGridOffset, XMVectorZero());
			Interpolator.reset(Origin, XMVectorZero());
			Yaw.zero();

			ZoomFactor = Globals::DEFAULT_ZOOM_SCALAR;
			tTranslateStart = zero_time_point;
			tRotateStart = zero_time_point;
			tZoomStart = zero_time_point;

			Motion = false;
		}
	} oCamera;

} // end ns

/* deprecated, moved to full 16bit heightstep
namespace // private to this file (anonymous)
{
	static inline uint32_t const GROUND_HEIGHT_NOISE[NUM_DISTINCT_GROUND_HEIGHTS] = {
		UINT8_MAX - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[0] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[1] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[2] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[3] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[4] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[5] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[6] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[7] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[8] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[9] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[10] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[11] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[12] - GROUND_HEIGHT_NOISE_STEP,
		GROUND_HEIGHT_NOISE[13] - GROUND_HEIGHT_NOISE_STEP
	};
} // end ns
*/

// in: local voxel index only 
static auto const __vectorcall groundAdjacency(point2D_t const voxelIndex) // Adjacency is only used during Rendering to cull hidden faces.
{                                                                              // *bugfix - Dynamically called during run-time -- faster than before where it was pre-computed before & terrain height updates are seamless with adjacency no longer being updated seperately, 
	                                                                           // it's automatic now. Also no longer have huge memory spike at loadtime when generating ground! Was 21GB!! 

	struct structured_binding {
		
		uint16_t const	heightstep;
		uint8_t const	minimum_heightstep;
		uint8_t const   adjacency;

		structured_binding(uint16_t const heightstep_, uint8_t const minimum_heightstep_, uint8_t const adjacency_)
			: heightstep(heightstep_), minimum_heightstep(minimum_heightstep_), adjacency(adjacency_)
		{}
	};

	// Therefore the neighbours of a voxel, follow same isometric layout/order //
	/*

								NBR_TR
					[NBR_T]				   [NBR_R]
		NBR_TL					VOXEL					NBR_BR
					[NBR_L]				   [NBR_B]
								NBR_BL
	*/
																			   // @TODO - Occlusion could be passed to the gpu here for every voxel based on adjacency of neighbours, and if that neighbouring voxel is of greater height than the current voxel.
	// Adjacency - that is sent to the gpu already - does this already, with face culling.
	// This gives information for 4 adjacent voxels being greater in height or not
	// Each corners' occlusion value, 4 vertices per face, can than be deduced (applies only from perspective of the top face, sides would be different on a voxel)
	
	// [  faces  ]              [ vertex ]
	// 0.5 * (top + left)     = top left vertex occlusion
	// 0.5 * (top + right)    = top right vertex ""     ""
	// 0.5 * (bottom + left)  = bottom left vertex ""   ""
	// 0.5 * (bottom + right) = bottom right vertex  "" ""
	
	// where the face (top, right, bottom, left) is either a 1.0 or 0.0
	// this would also be computed in the geometry shader, for the top face.
	// the fragment shader will then naturally interpolate between vertices (aka vertex lighting) for the entire face.
	// the fragment shader should raise the pow() of these linear occlusion values to finish faking the occlusion and making it look proper for what ambient occlusion looks like (lots of white, minimal greys and blacks - representing the occluded area)
	// shading would be along the edges of voxels and it's neighbours and not extend to far into the face from the edge. some multiplied noise, for occluded areas, may look less repetitive and give the occlusion some variance.
	// ...as for the combinations of faces to vertices for the sides of the voxel, well it may not be worth it. good for terrain only! and only if this type of edge occlusion is desirable for every terrain voxel.... (local occlusion)

	uint32_t const curVoxelHeightStep(::Iso::getHeightStep(voxelIndex));
	uint32_t minimum_heightstep(curVoxelHeightStep);

	uint32_t Adjacency(0);

	// Therefore the neighbours of a voxel, follow same isometric layout/order //
	/*

								NBR_TR
					[NBR_T]				   [NBR_R]
		NBR_TL					VOXEL					NBR_BR
					[NBR_L]				   [NBR_B]
								NBR_BL
	*/
	 
	// Side Back = NBR_T
	{
		auto const localvoxelIndexNeighbour = getNeighbourLocalVoxelIndex(voxelIndex, ADJACENT[NBR_T]);

		uint32_t const neighbourHeightStep(::Iso::getHeightStep(localvoxelIndexNeighbour));

		// adjacency
		if (neighbourHeightStep >= curVoxelHeightStep) {

			Adjacency |= (1 << ::Volumetric::adjacency::back);
		}
		else {
			minimum_heightstep = neighbourHeightStep;
		}
	}

	// Side Left = NBR_L
	{
		auto const localvoxelIndexNeighbour = getNeighbourLocalVoxelIndex(voxelIndex, ADJACENT[NBR_L]);

		uint32_t const neighbourHeightStep(::Iso::getHeightStep(localvoxelIndexNeighbour));

		// adjacency
		if (neighbourHeightStep >= curVoxelHeightStep) {

			Adjacency |= (1 << ::Volumetric::adjacency::left);
		}
		else {
			minimum_heightstep = neighbourHeightStep;
		}
	}

	// Side Right = NBR_R
	{
		auto const localvoxelIndexNeighbour = getNeighbourLocalVoxelIndex(voxelIndex, ADJACENT[NBR_R]);

		uint32_t const neighbourHeightStep(::Iso::getHeightStep(localvoxelIndexNeighbour));

		// adjacency
		if (neighbourHeightStep >= curVoxelHeightStep) {

			Adjacency |= (1 << ::Volumetric::adjacency::right);
		}
		else {
			minimum_heightstep = neighbourHeightStep;
		}
	}

	// Side Front = NBR_B
	{
		auto const localvoxelIndexNeighbour = getNeighbourLocalVoxelIndex(voxelIndex, ADJACENT[NBR_B]);

		uint32_t const neighbourHeightStep(::Iso::getHeightStep(localvoxelIndexNeighbour));

		// adjacency
		if (neighbourHeightStep >= curVoxelHeightStep) {

			Adjacency |= (1 << ::Volumetric::adjacency::front);
		}
		else {
			minimum_heightstep = neighbourHeightStep;
		}
	}


	// convert to each type, capturing 65536 levels of heightstep, 256 levels of minimum heightstep of neighbours, more than enough adjacency bits
	return(structured_binding{ (uint16_t)SFM::max(1u, curVoxelHeightStep), (uint8_t)(minimum_heightstep >> 8), (uint8_t)Adjacency }); // *bugfix-adjust heights for rendering requirements
}

/*
// https://www.shadertoy.com/view/ftSfRw
struct Crater {
	float radius;
	float depth;
	float floorHeight;
	float rimHeight;
	float rimWidth;
};

// Crater shape, inspired from : https://www.youtube.com/watch?v=lctXaT9pxA0
float craterShape(float x, Crater c) {
	// x : distance to the center of the crater

	float def = SFM::step(x, c.radius + c.rimWidth);

	float cavity = (c.rimHeight - c.depth) / (c.radius * c.radius);
	cavity *= x * x;
	cavity += c.depth;

	float rim = x - c.radius - c.rimWidth;
	rim *= rim;
	rim *= c.rimHeight / (c.rimWidth * c.rimWidth);

	return SFM::smin(SFM::smax(cavity, c.floorHeight, 0.01), rim, 0.01) * def;

}
*/

namespace // private to this file (anonymous)
{
	constinit static inline struct // purposely anonymous union, protected pointer implementation for the::grid
	{
		StreamingGrid* __restrict		 _protected = nullptr;

		__declspec(safebuffers) __forceinline operator StreamingGrid* const __restrict() const {
			return(_protected);
		}
	} grid{};
} // end ns


static uint32_t const RenderNoiseImagePixel(float const u, float const v, float const in, supernoise::interpolator::functor const& interp)
{
	// static constexpr float const NOISE_SCALAR_HEIGHT = 1.0f * (Iso::WORLD_GRID_FSIZE / 512.0f); // fixed so scale of terrain height is constant irregardless of width/depth of map
																								 // 512 happens to be the right number / base number *do not change*
																								 // *can change the first number only*
																								 // less = lower frequency of change in elevation
																								 // more = higher   ""          ""          ""
#ifdef DEBUG_FLAT_GROUND
	return(SFM::saturate_to_u8(supernoise::blue.get2D(NOISE_SCALAR_HEIGHT * u, NOISE_SCALAR_HEIGHT * v) * (6.0f))); /// perturbed by a small amount of bluenoise to show some details when in the dark moving around.
#endif
	return(SFM::saturate_to_u16(in * 65535.0f));
}

void cVoxelWorld::GenerateGround() // *bugfix - much much faster now and real lunar surface details are weighted into the final terrain generated
{
	supernoise::NewNoisePermutation();

	Imaging imageNoise(nullptr); // elevation only

	{
		// Generate 
		Imaging imageTerrain = ImagingLoadKTX(TEXTURE_DIR "moon_heightmap.ktx"); // single channel texture

#ifndef NDEBUG
#ifdef DEBUG_ALIGNMENT_TERRAIN
		ImagingDelete(imageTerrain);
		imageTerrain = ImagingLoadKTX(DEBUG_DIR "test_height_map.ktx");
#endif
#endif
		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(20));
		// bilateral filter removed - bug fix was not 16bit comptabile, terrain detail is better w/o the filter.

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(25));

		// resample terrain image to world grid dimensions
		if (Iso::WORLD_GRID_WIDTH != imageTerrain->xsize || Iso::WORLD_GRID_HEIGHT != imageTerrain->ysize) {

			// resample to required dimensions
			Imaging resampledImg = ImagingResample(imageTerrain, Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT, IMAGING_TRANSFORM_BILINEAR);
			ImagingDelete(imageTerrain); imageTerrain = resampledImg;
		}

		// not customizing with additional features, using real moon heightmap data
		//imageNoise = MinCity::Procedural->GenerateNoiseImage(&RenderNoiseImagePixel, Iso::WORLD_GRID_SIZE, supernoise::interpolator::SmoothStep(), imageTerrain); // single channel image
		//ImagingDelete(imageTerrain);
		imageNoise = imageTerrain; // pass on final heightmap
		imageTerrain = nullptr; // no longer needed

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(30));
#ifndef NDEBUG
#ifdef DEBUG_EXPORT_TERRAIN_KTX
		ImagingSaveToKTX(imageNoise, DEBUG_DIR L"last_terrain_elevation.ktx");
#endif
#endif
	}

	// Traverse Grid
	if (_heightmap) {
		ImagingDelete(_heightmap); _heightmap = nullptr;
	}
	_heightmap = imageNoise; // main pointer to heightmap becomes the new 16 bit memory location
	
	// otherwise texture creation from image takes place in onloaded event.
	MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(50));

	FMT_LOG_OK(GAME_LOG, "Generated Ground");
}

void cVoxelWorld::UpdateCamera(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	static constexpr milliseconds const
		SMOOTH_MS_ZOOM{ 200 },
		SMOOTH_MS_ROTATE{ 400 },
		SMOOTH_MS_TRANS(SMOOTH_MS_ROTATE);

	// **** camera designed to allow rotation and zooming (no scrolling) while paused

	// TRANSLATION //
	if (zero_time_point != oCamera.tTranslateStart) {
		static constexpr fp_seconds const tSmooth = fp_seconds(SMOOTH_MS_TRANS);

		fp_seconds const tDelta = critical_now() - oCamera.tTranslateStart;

		XMVECTOR const targetPosition(XMVectorRound(XMLoadFloat2A(&oCamera.TargetPosition)));

		if (tDelta >= tSmooth) { // reset, finished transition
			oCamera.tTranslateStart = zero_time_point;
			Interpolator.set(oCamera.Origin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W >(targetPosition));
		}
		else {
			float const tInvDelta = SFM::saturate(time_to_float(tDelta / tSmooth));
			XMVECTOR xmInterpPosition(SFM::lerp(XMLoadFloat2A(&oCamera.PrevPosition), targetPosition, tInvDelta));
			// wrap around camera coords //
			XMVECTOR const xmHalfExtent(XMVectorSet(Iso::WORLD_GRID_FHALF_WIDTH, Iso::WORLD_GRID_FHALF_HEIGHT, 0.0f, 0.0f));
			xmInterpPosition = XMVectorSubtract(XMVectorMod(XMVectorAdd(xmInterpPosition, xmHalfExtent), XMVectorSet(Iso::WORLD_GRID_FWIDTH, Iso::WORLD_GRID_FHEIGHT, 1.0f, 1.0f)), xmHalfExtent);
			Interpolator.set(oCamera.Origin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W >(xmInterpPosition));
			//oCamera.Yaw = v2_rotation_t(); // eliminate any camera rotation ?
		}
	}

	// ZOOM //
	if (zero_time_point != oCamera.tZoomStart) {
		static constexpr fp_seconds const tSmooth = fp_seconds(SMOOTH_MS_ZOOM);

		fp_seconds const tDelta = critical_now() - oCamera.tZoomStart;

		if (tDelta >= tSmooth) { // reset, finished transition
			oCamera.tZoomStart = zero_time_point;
			oCamera.ZoomFactor = oCamera.TargetZoomFactor;
		}
		else {
			float const tInvDelta = SFM::saturate(time_to_float(tDelta / tSmooth));
			float const fNewZoom = SFM::lerp(oCamera.PrevZoomFactor, oCamera.TargetZoomFactor, tInvDelta);
			
			oCamera.ZoomFactor = fNewZoom;
		}
	}

	// ROTATION //
	if (zero_time_point != oCamera.tRotateStart) {
		static constexpr fp_seconds const tSmooth = fp_seconds(SMOOTH_MS_ROTATE);

		fp_seconds const tDelta = critical_now() - oCamera.tRotateStart;

		if (tDelta >= tSmooth) { // reset, finished transition
			oCamera.tRotateStart = zero_time_point;
			oCamera.Yaw = oCamera.TargetYawAngle;
		}
		else {
			float const tInvDelta = SFM::saturate(time_to_float(tDelta / tSmooth));
			oCamera.Yaw = v2_rotation_t( SFM::lerp(oCamera.PrevYawAngle, oCamera.TargetYawAngle, tInvDelta) );
		}
	}

	_bIsEdgeScrolling = false; // always reset
	if (eInputEnabledBits::MOUSE_EDGE_SCROLL == (_inputEnabledBits & eInputEnabledBits::MOUSE_EDGE_SCROLL))
	{
		// MOUSE-SIDE-SCROLLING //
		if (eMouseButtonState::RIGHT_PRESSED != _mouseState) { // only if right mouse button is not pressed (dissallow camera rotation+sidescrolling)
			XMFLOAT2A screenextent;
			XMStoreFloat2A(&screenextent, p2D_to_v2(MinCity::getFramebufferSize()));

			// screen scrolling input //
			static constexpr float const BORDER_SCROLL = 4.0f;  // in pixels
			point2D_t const vScroll(

				-(int32_t)(_vMouse.x - BORDER_SCROLL < 0.0f) + (int32_t)(_vMouse.x >= (screenextent.x - BORDER_SCROLL)),
				(int32_t)(_vMouse.y - BORDER_SCROLL < 0.0f) - (int32_t)(_vMouse.y >= (screenextent.y - BORDER_SCROLL))

			);

			if (!vScroll.isZero()) {

				_tBorderScroll -= delta();
				_tBorderScroll = fp_seconds(SFM::max(0.0, _tBorderScroll.count()));

				if (zero_time_duration == _tBorderScroll) {
					translateCamera(p2D_shiftl(vScroll, Iso::CAMERA_SCROLL_DISTANCE_MULTIPLIER));
					_bIsEdgeScrolling = true;
					_bMotionDelta = true; // override so that dragging continue
					_tBorderScroll = Iso::CAMERA_SCROLL_DELAY; // reset
				}
			}
			else {
				_tBorderScroll += delta();
				_tBorderScroll = fp_seconds(SFM::min(Iso::CAMERA_SCROLL_DELAY.count(), _tBorderScroll.count()));
			}
		}
	}

	bool bOneButtonDragging(false); // exclusively allow either left dragging or right dragging, not both at the same time, left takes priority. 

	if (eInputEnabledBits::MOUSE_LEFT_DRAG == (_inputEnabledBits & eInputEnabledBits::MOUSE_LEFT_DRAG)) {

		if (eMouseButtonState::LEFT_PRESSED == _mouseState) {  // LEFT DRAGGING //

			if (_bMotionDelta) {

				if (_bDraggingMouse) {

					MinCity::UserInterface->LeftMouseDragAction(XMLoadFloat2A(&_vMouse), XMLoadFloat2A(&_vDragLast), _tDragStart);

					_vDragLast = _vMouse;
					_tDragStart = now();
					bOneButtonDragging = true;
				}
			}
		}
	}

	if (!bOneButtonDragging && eInputEnabledBits::MOUSE_LEFT_DRAG == (_inputEnabledBits & eInputEnabledBits::MOUSE_LEFT_DRAG)) {
		
		if (eMouseButtonState::RIGHT_PRESSED == _mouseState) {  // RIGHT DRAGGING - ROTATION //

			if (_bMotionDelta) {
				  
				if (_bDraggingMouse) {
					float const fXDelta = _vMouse.x - _vDragLast.x;

					rotateCamera(fXDelta * time_to_float(tDelta));

					_vDragLast = _vMouse;
					_tDragStart = now();
					bOneButtonDragging = true;
				}
			}
		}
		
	}

	// get Origin's location in voxel units
	// range is (-144,-144) TopLeft, to (144,144) BottomRight
	//
	// Clamp Camera Origin and update //
	XMVECTOR const xmOrigin3D(XMLoadFloat3A(&(XMFLOAT3A const&)oCamera.Origin));
	XMVECTOR const xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin3D);  // only care about xz components, make it a 2D vector
	
	point2D_t voxelIndex(v2_to_p2D(xmOrigin));
	// wrap bounds //
	voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

	point2D_t const visibleRadius(p2D_half(point2D_t(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z))); // want radius, hence the half value

	// get starting voxel in TL corner of screen
	voxelIndex = p2D_sub(voxelIndex, visibleRadius);
	
	// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
	point2D_t voxelIndex_TopLeft = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));
	voxelIndex_TopLeft = p2D_sub(voxelIndex_TopLeft, Iso::GRID_OFFSET);
	// wrap bounds //
	oCamera.voxelIndex_TopLeft = p2D_wrap_pow2(voxelIndex_TopLeft, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

	point2D_t voxelIndex_Center( oCamera.voxelIndex_TopLeft.v );
	// Change from  (0,0) => (x,y) to  (-x,-y) => (x,y)
	voxelIndex_Center = p2D_sub(voxelIndex_Center, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));
	// wrap bounds //
	oCamera.voxelIndex_Center = p2D_wrap_pow2(voxelIndex_Center, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

	// Convert Fractional component from GridSpace
	XMVECTOR const xmFract(SFM::sfract(xmOrigin3D));  // FOR TRUE SMOOTH SCROLLING //
	/* important output */
	XMStoreFloat3A(&oCamera.voxelFractionalGridOffset, xmFract); // store fractional offset in natural xyz form, keeping fractional y component for future usage.
	oCamera.voxelFractionalGridOffset.y = 0.0f;
	/* important output */                                                                                                                                    // *bugfix - important to zero the y component, otherwise the "staircase effect" in motion for elevation occurs.
	                                                                                                                                                          // otherwise the component gets added incorrectly *twice* for the *actual* camera origin.
#ifndef NDEBUG
	static XMVECTOR DebugVariable; 
	DebugVariable = XMLoadFloat3A(&oCamera.voxelFractionalGridOffset);
	setDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET, DebugVariable);
#endif
	
	// MOTION DELTA TRACKING //
	{
		oCamera.Motion = (zero_time_point != oCamera.tZoomStart) | (zero_time_point != oCamera.tRotateStart) | (zero_time_point != oCamera.tTranslateStart);
	}

	if ((MinCity::isPaused() | _bCameraTurntable) && MinCity::isFocused()) {

		rotateCamera(time_to_float(fp_seconds(MinCity::critical_delta())));
	}
}
void cVoxelWorld::setCameraTurnTable(bool const enable)
{
	_bCameraTurntable = enable;
}

// Camera follow always keeps user ship in view, never offscreen even if ship changes elevation! Follows elevation too.
void __vectorcall cVoxelWorld::updateCameraFollow(FXMVECTOR xmPosition, XMVECTOR xmVelocity, Volumetric::voxelModelInstance_Dynamic const* const instance, fp_seconds const& __restrict tDelta) // expects 3D coordinates
{	
	rect2D_t const vLocalArea(instance->getModel()._LocalArea);			   // convert to voxel index
	int32_t volume_visible = world::testVoxelsAt(r2D_add(vLocalArea, v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmPosition))), instance->getYaw()); // *major* prevents ship passing outside the "visible volume", which at high heights can be very near to the center of the screen. Glitches, missing rasterization vs raymarch is now gone for camera followed ship!

	//xmVelocity = XMVectorSetY(xmVelocity, 0.0f); // only xz

	float const max_speed(time_to_float(tDelta * 1.0 / duration_cast<fp_seconds>(critical_delta())));

	XMVECTOR const xmFutureFocusPosition(SFM::__fma(xmVelocity, XMVectorReplicate(max_speed), xmPosition));

	float const current_speed(XMVectorGetX(XMVector3Length(xmVelocity)) * time_to_float(tDelta));
	float const t = SFM::__exp(-10.0f * (current_speed / max_speed + (float)(volume_visible <= 0)));

	//Interpolator very smooth :)
	XMVECTOR xmNewPosition(SFM::lerp(xmPosition, xmFutureFocusPosition, t));
	xmNewPosition = XMVectorSetY(xmNewPosition, -XMVectorGetY(xmNewPosition));
	Interpolator.set(oCamera.Origin, xmNewPosition);
}

void cVoxelWorld::OnKey(int32_t const key, bool const down, bool const ctrl)
{
	if (eInputEnabledBits::KEYS != (_inputEnabledBits & eInputEnabledBits::KEYS))
		return; // input disabled!

#ifndef NDEBUG // debug variables
#ifdef DEBUG_WORLD_PLANE_HEIGHT
	static constexpr float ADJUST_SCALE = 0.1f;
	static float fPlaneHeightOffset{};
	bool bChangedPlaneHeight{};
#endif
#endif

	switch (key)
	{ // overrides (these cases are higher priority)
	case GLFW_KEY_SPACE:
		if (!down) { // on released
			MinCity::Pause(!MinCity::isPaused());
		}
		break;
	case GLFW_KEY_R:
		if (!down) { // on released
			resetCameraAngleZoom();
		}
		break;
	//case GLFW_KEY_S:
	//	if (!down) { // on released
	//		cMinCity::UserInterface->setActivatedTool(eTools::SELECT);
	//	}
	//	break;
#ifndef NDEBUG
#ifdef DEBUG_WORLD_PLANE_HEIGHT
	case GLFW_KEY_UP:
		fPlaneHeightOffset -= ADJUST_SCALE;
		bChangedPlaneHeight = true;
		break;
	case GLFW_KEY_DOWN:
		fPlaneHeightOffset += ADJUST_SCALE;
		bChangedPlaneHeight = true;
		break;
#endif
#endif
	
	default: // further processing delegated to user interface and the tools it consists of
		cMinCity::UserInterface->KeyAction(key, down, ctrl);
		break;
	}

#ifndef NDEBUG
#ifdef DEBUG_WORLD_PLANE_HEIGHT
	if (bChangedPlaneHeight) {
		setDebugVariable(float, DebugLabel::WORLD_PLANE_HEIGHT, fPlaneHeightOffset);
		FMT_NUKLEAR_DEBUG(false, "world plane height offset: {:02f}\n", fPlaneHeightOffset);
	}
#endif
#endif
}

bool const __vectorcall cVoxelWorld::OnMouseMotion(FXMVECTOR xmMotionIn, bool const bIgnore)
{
	if (eInputEnabledBits::MOUSE_MOTION != (_inputEnabledBits & eInputEnabledBits::MOUSE_MOTION))
		return(false); // input disabled!

	static constexpr float const EPSILON = 0.00001f;
	XMVECTORF32 const xmEPSILON{ EPSILON, EPSILON, 0.0f, 0.0f };

	XMVECTOR xmMotionCurrent;

	if (_bMotionInvalidate)
	{
		_bMotionDelta = true;
		_bMotionInvalidate = false;
		XMStoreFloat2A(&_vMouse, xmMotionIn);
		xmMotionCurrent = xmMotionIn;
	}
	else
	{
		XMVECTOR xmCompare;
		{
			xmMotionCurrent = XMLoadFloat2A(&_vMouse);

			xmCompare = SFM::abs(XMVectorSubtract(xmMotionCurrent, xmMotionIn));
		}

		if (XMComparisonAnyTrue(XMVector4GreaterR(xmCompare, xmEPSILON))) {
			_bMotionDelta = true;
			_bMotionInvalidate = false;
			XMStoreFloat2A(&_vMouse, xmMotionIn);
			xmMotionCurrent = xmMotionIn;
		}
	}

	// *************** use xmMotionCurrent ********************
	if (!bIgnore && _bMotionDelta) {	// handling motion //

		if (!_bDraggingMouse) // handle dragging start
		{
			if ((eMouseButtonState::LEFT_PRESSED|eMouseButtonState::RIGHT_PRESSED) & _mouseState) {
				_bDraggingMouse = true;
				XMStoreFloat2A(&_vDragLast, xmMotionCurrent);
				_tDragStart = now();
			}
		}		

		// only if still not dragging
		if (!_bDraggingMouse)
		{
			// Mouse Move
			cMinCity::UserInterface->MouseMoveAction(xmMotionCurrent);
		}
	}

	return(_bMotionDelta);
}
void cVoxelWorld::OnMouseLeft(int32_t const state)
{
	if (eInputEnabledBits::MOUSE_BUTTON_LEFT != (_inputEnabledBits & eInputEnabledBits::MOUSE_BUTTON_LEFT))
		return; // input disabled!

	if (eMouseButtonState::RELEASED == state) {
		
		_bDraggingMouse = false;

		cMinCity::UserInterface->LeftMouseReleaseAction(XMLoadFloat2A(&_vMouse));
	}
	else { // PRESSED

		cMinCity::UserInterface->LeftMousePressAction(XMLoadFloat2A(&_vMouse));

	}


	_mouseState = state;
}
void cVoxelWorld::OnMouseRight(int32_t const state)
{
	if (eInputEnabledBits::MOUSE_BUTTON_RIGHT != (_inputEnabledBits & eInputEnabledBits::MOUSE_BUTTON_RIGHT))
		return; // input disabled!

	if (eMouseButtonState::RELEASED == state) {
		
		_bDraggingMouse = false;
	}
	else { // PRESSED

	}


	_mouseState = state;
}
void cVoxelWorld::OnMouseLeftClick()
{
	if (eInputEnabledBits::MOUSE_BUTTON_LEFT != (_inputEnabledBits & eInputEnabledBits::MOUSE_BUTTON_LEFT))
		return; // input disabled!

	cMinCity::UserInterface->LeftMouseClickAction(XMLoadFloat2A(&_vMouse));
}
void cVoxelWorld::OnMouseRightClick()
{
	if (eInputEnabledBits::MOUSE_BUTTON_RIGHT != (_inputEnabledBits & eInputEnabledBits::MOUSE_BUTTON_RIGHT))
		return; // input disabled!

	placeUpdateableInstanceAt<cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(getHoveredVoxelIndex(),
			Volumetric::eVoxelModel::DYNAMIC::NAMED::GROUND_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
}
void cVoxelWorld::OnMouseScroll(float const delta)
{
	if (eInputEnabledBits::MOUSE_SCROLL_WHEEL != (_inputEnabledBits & eInputEnabledBits::MOUSE_SCROLL_WHEEL))
		return; // input disabled!

	zoomCamera(delta);
}
void cVoxelWorld::OnMouseInactive()
{
	if (eMouseButtonState::RELEASED == _mouseState) {
		_mouseState = eMouseButtonState::INACTIVE;
		_bDraggingMouse = false;
	}
}

void cVoxelWorld::clearOcclusionInstances()
{
	static constexpr uint32_t const
		STATIC = 0,
		DYNAMIC = 1;

	{ // static
		for (unordered_set<uint32_t>::const_iterator iter = _occlusion.fadedInstances[STATIC].cbegin(); iter != _occlusion.fadedInstances[STATIC].cend(); ++iter) {

			auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<false>(*iter);

			if (instance) {
				instance->setFaded(false); // reset instance
			}
		}

		_occlusion.fadedInstances[STATIC].clear(); // reset
	}
	{ // dynamic
		for (unordered_set<uint32_t>::const_iterator iter = _occlusion.fadedInstances[DYNAMIC].cbegin(); iter != _occlusion.fadedInstances[DYNAMIC].cend(); ++iter) {

			auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<true>(*iter);

			if (instance) {
				instance->setFaded(false); // reset instance
			}
		}

		_occlusion.fadedInstances[DYNAMIC].clear(); // reset
	}

	_occlusion.state = Globals::UNLOADED;
}
void cVoxelWorld::setOcclusionInstances()
{
	static constexpr uint32_t const
		STATIC = 0,
		DYNAMIC = 1;

	uint32_t hasOcclusion(Globals::UNLOADED);

	{ // static
		for (unordered_set<uint32_t>::const_iterator iter = _occlusion.fadedInstances[STATIC].cbegin(); iter != _occlusion.fadedInstances[STATIC].cend(); ++iter) {

			auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<false>(*iter);

			if (instance) {
				instance->setFaded(true);
				//instance->setTransparency(Volumetric::eVoxelTransparency::ALPHA_100);
				hasOcclusion = Globals::LOADED;
			}
		}
	}
	{ // dynamic
		for (unordered_set<uint32_t>::const_iterator iter = _occlusion.fadedInstances[DYNAMIC].cbegin(); iter != _occlusion.fadedInstances[DYNAMIC].cend(); ++iter) {

			auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<true>(*iter);

			if (instance) {
				instance->setFaded(true);
				//instance->setTransparency(Volumetric::eVoxelTransparency::ALPHA_100);
				hasOcclusion = Globals::LOADED;
			}
		}
	}

	_occlusion.state = hasOcclusion;
}
void cVoxelWorld::updateMouseOcclusion(bool const bPaused)
{
	static constexpr uint32_t const
		STATIC = 0,
		DYNAMIC = 1;
	
	//FMT_NUKLEAR_DEBUG(true, "{:d} , {:d}   {:d} , {:d}  {:s}", _occlusion.groundVoxelIndex.x, _occlusion.groundVoxelIndex.y, _occlusion.occlusionVoxelIndex.x, _occlusion.occlusionVoxelIndex.y, (_lastOcclusionQueryValid ? "true" : "false"));
	[[unlikely]] if (bPaused || _bIsEdgeScrolling) { // *bugfix - add exception to not hide buildings / etc while scrolling the view (gives the appearance of "pop-in" depending on what is under the mouse *undesired).
		clearOcclusionInstances();
		_occlusion.tToOcclude = Volumetric::Konstants::OCCLUSION_DELAY; //*bugfix - reset countdown timer
		return;
	}

	{
		if (_lastOcclusionQueryValid) {

			_occlusion.state = Globals::PENDING;
		}
		else {
			_occlusion.state = Globals::UNLOADED;
		}
	}

	if (_occlusion.state < 0) { // PENDING ?

		clearOcclusionInstances(); //  always clear working correctly

		if (_occlusion.occlusionVoxelIndex != _occlusion.groundVoxelIndex) { // if ground is occluded by some voxels...

			point2D_t voxelIndex;

			// hovered voxel on ground
			voxelIndex = getHoveredVoxelIndex();

			Iso::Voxel const oHoveredVoxel(world::getVoxelAt(voxelIndex));

			// occlusion voxel not ground
			voxelIndex = _occlusion.occlusionVoxelIndex;

			Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

			if (Iso::isOwnerAny(oVoxel)) {

				for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {

					if (Iso::isOwner(oVoxel, i)) {

						// get hash, which should be the voxel model instance ID
						uint32_t const hash = Iso::getHash(oVoxel, i);

						if (0 != hash) {

							// Check hash against bottom (exclusion)
							bool excluded(false);

							for (uint32_t j = Iso::STATIC_HASH; j < Iso::HASH_COUNT; ++j) {

								if (Iso::getHash(oHoveredVoxel, j) == hash) {
									excluded = true;
									break;
								}
							}

							if (!excluded) {

								// add to set of unique instance hashes
								uint32_t const set = (uint32_t const)(Iso::STATIC_HASH != i); // DYNAMIC or STATIC

								// only add if fadeable
								if (set) {
									auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<DYNAMIC>(hash);
									if (instance) {
										excluded = !instance->isFadeable();
									}
								}
								else {
									auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<STATIC>(hash);
									if (instance) {
										excluded = !instance->isFadeable();
									}
								}

								if (!excluded) {
									_occlusion.fadedInstances[set].emplace(hash);
								}
							}
						}
					}
				}
			}

			_occlusion.tToOcclude -= delta();
			_occlusion.tToOcclude = fp_seconds(SFM::max(0.0, _occlusion.tToOcclude.count()));

			if (zero_time_duration == _occlusion.tToOcclude) {

				// adjust instances to faded
				setOcclusionInstances();

			}
		}
	}
	else {
		_occlusion.tToOcclude += delta();
		_occlusion.tToOcclude = fp_seconds(SFM::min(Volumetric::Konstants::OCCLUSION_DELAY.count(), _occlusion.tToOcclude.count()));
	}
}

namespace world
{
	// World Space (-x,-z) to (X, Z) Coordinates Only - (Camera Origin) - *swizzled*(
	XMVECTOR const __vectorcall getOrigin()
	{
		return(XMLoadFloat3A(&(XMFLOAT3A const&)oCamera.Origin));
	}																										
	v2_rotation_t const& getYaw()
	{
		return(oCamera.Yaw);
	}

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	bool const __vectorcall isVoxelVisible(point2D_t const voxelIndex)   /// used in the red ground lines (cAbstractToolMethods)
	{
		auto const localvoxelIndex(world::getLocalVoxelIndexAt(voxelIndex));

		XMVECTOR xmLocation(p2D_to_v2(voxelIndex));
		xmLocation = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLocation);
		xmLocation = XMVectorSetY(xmLocation, -Iso::getRealHeight(localvoxelIndex));

		// need to transform grid space coordinates to world space coordinates
		xmLocation = XMVectorSubtract(xmLocation, world::getOrigin());

		return(Volumetric::VolumetricLink->Visibility.SphereTestFrustum(xmLocation, Volumetric::volumetricVisibility::getVoxelRadius()));
	}
	// World Space Coordinates Only
	bool const __vectorcall isVoxelVisible(FXMVECTOR const xmLocation, float const voxelRadius) // working w/ voxel height adjustment - see usage in RenderGrid
	{
		return(Volumetric::VolumetricLink->Visibility.SphereTestFrustum(xmLocation, voxelRadius));
	}

	namespace zoning
	{
		void zoneArea(rect2D_t voxelArea, uint32_t const zone_type) // supports zoning-zoning overwrite, excludes any area that is not strictly ground, 
																	// synchronizes residential, commercial, industrial area exclusivity.
																	// exclusivity is strictly maintained here, so the other zoning operations work without having to worry about it
		{															// as from this point on it's an impossible state to have bits set for the same voxel in more than one zoning type.
			// clamp to world/minmax coords
			voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

			// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
			voxelArea = r2D_add(voxelArea, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

			uint32_t const zoning(Iso::MASK_ZONING & (zone_type + 1));
			point2D_t voxelIterate;

			for (voxelIterate.y = voxelArea.top; voxelIterate.y < voxelArea.bottom; ++voxelIterate.y) {

				for (voxelIterate.x = voxelArea.left; voxelIterate.x < voxelArea.right; ++voxelIterate.x) {

					// pre-transformed to [0,0 to X,Y]
					Iso::Voxel oVoxel(world::getVoxelAtLocal(voxelIterate));

					if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) { // only apply to ground area excluding the static & dynamic instances

						Iso::setZoning(oVoxel, zoning);
						Iso::setColor(oVoxel, world::ZONING_COLOR[zoning]);

						world::setVoxelAtLocal(voxelIterate, std::forward<Iso::Voxel const&&>(oVoxel));
					}
				}
			}
			/*
			// Highlighting Edges of Zones only (must be done after zoning is applied, for neighbours need to be set before they are checked)
			for (voxelIterate.y = voxelArea.top; voxelIterate.y < voxelArea.bottom; ++voxelIterate.y) {

				for (voxelIterate.x = voxelArea.left; voxelIterate.x < voxelArea.right; ++voxelIterate.x) {

					Iso::Voxel const* const __restrict pVoxel(world::getVoxelAtLocal(voxelIterate)); // pre-transformed to [0,0 to X,Y]

					if (pVoxel) {
						Iso::Voxel oVoxel(*pVoxel);

						if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) { // only apply to ground area excluding the static & dynamic instances

							uint32_t ground_count(0);
							for (uint32_t adjacent = 0; adjacent < ADJACENT_NEIGHBOUR_COUNT; ++adjacent) {

								Iso::Voxel const* const pNeighbour = world::getNeighbourLocal(voxelIterate, ADJACENT[adjacent]);
								if (nullptr != pNeighbour) {
									ground_count += ((Iso::getZoning(oVoxel) == zoning)); // count of adjacent ground voxels that are the same zoning type
								}
							}

							if (!(ADJACENT_NEIGHBOUR_COUNT == ground_count)) { // only edge voxels (voxels that are not surrounded by ground voxels with the same zoning type)

								Iso::setEmissive(oVoxel);	// the level of emission can be coontrolled thru the voxel color. A light is automatically added.
							}
							else {
								Iso::clearEmissive(oVoxel); // compatibility w/overwrite
							}

							world::setVoxelAtLocal(voxelIterate, std::forward<Iso::Voxel const&&>(oVoxel));
						}
					}
				}
			}*/
		}

		void dezoneArea(rect2D_t voxelArea) // area dezones if not built, bulldozing must be done first otherwise. automatic zoning type handling.
		{
			// clamp to world/minmax coords
			voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

			// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
			voxelArea = r2D_add(voxelArea, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

			point2D_t voxelIterate;

			for (voxelIterate.y = voxelArea.top; voxelIterate.y < voxelArea.bottom; ++voxelIterate.y) {

				for (voxelIterate.x = voxelArea.left; voxelIterate.x < voxelArea.right; ++voxelIterate.x) {

					// pre-transformed to [0,0 to X,Y]
					Iso::Voxel oVoxel(world::getVoxelAtLocal(voxelIterate));

					if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) { // only apply to ground area excluding the static & dynamic instances

						Iso::clearZoning(oVoxel);
						Iso::clearColor(oVoxel);
						Iso::clearEmissive(oVoxel);
							
						world::setVoxelAtLocal(voxelIterate, std::forward<Iso::Voxel const&&>(oVoxel));
					}
				}
			}
		}
	} // end ns

	uint32_t const __vectorcall getVoxelHeightAt(point2D_t voxelIndex)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(Iso::getHeightStep(voxelIndex));
	}

	Iso::Voxel const __vectorcall getVoxelAt(point2D_t voxelIndex)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(((StreamingGrid const* const __restrict)::grid)->getVoxel(voxelIndex));
	}

	point2D_t const __vectorcall getLocalVoxelIndexAt(point2D_t voxelIndex)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(voxelIndex);
	}
	Iso::Voxel const __vectorcall getVoxelAt(FXMVECTOR const Location)
	{	//          	equal same voxel	
		// still in Grid Space (-x,-y) to (X, Y) Coordinates 
		return(getVoxelAt(v2_to_p2D(Location)));
	}

	Iso::Voxel const __vectorcall getVoxelAtLocal(point2D_t voxelIndex)
	{
		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(((StreamingGrid const* const __restrict)::grid)->getVoxel(voxelIndex));
	}

	uint32_t const getVoxelsAt_AverageHeight(rect2D_t voxelArea)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		uint32_t uiSumHeight(0), uiNumSamples(0);

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				// for any voxel still has ground underneath
				uiSumHeight += getVoxelHeightAt(voxelIterate);
				++uiNumSamples;

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(SFM::round_to_u32((float)uiSumHeight / (float)uiNumSamples));
	}
	
	uint32_t const __vectorcall getVoxelsAt_MaximumHeight(rect2D_t const voxelArea, v2_rotation_t const& __restrict vR)
	{
		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());
		point2D_t p;

		uint32_t maximum_height(0);

		// oriented rect filling algorithm, see processing sketch "rotation" for reference (using revised version)
		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				point2D_t const p0(p);
				p = p2D_rotate(voxelIterate, voxelArea.center(), -vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					maximum_height = SFM::max(maximum_height, getVoxelHeightAt(p0));
				}

				// center 
				maximum_height = SFM::max(maximum_height, getVoxelHeightAt(p));

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(maximum_height);
	}

	void setVoxelHeightAt(point2D_t const voxelIndex, uint32_t const heightstep)
	{
		Iso::setHeightStep(getLocalVoxelIndexAt(voxelIndex), heightstep);
	}

	void setVoxelsHeightAt(rect2D_t voxelArea, uint32_t const heightstep)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				setVoxelHeightAt(voxelIterate, heightstep);

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	void __vectorcall setVoxelAt(point2D_t voxelIndex, Iso::Voxel const&& __restrict newData)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		// Update Voxel
		((StreamingGrid* const)::grid)->setVoxel(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(newData));
	}
	void __vectorcall setVoxelAt(FXMVECTOR const Location, Iso::Voxel const&& __restrict newData)
	{
		point2D_t const voxelIndex(v2_to_p2D(Location));		// this always floors, fractional part between 0.00001 and 0.99999 would
																																																			//          	equal same voxel	
		// still in Grid Space (-x,-y) to (X, Y) Coordinates 
		setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(newData));
	}

	void __vectorcall setVoxelAtLocal(point2D_t voxelIndex, Iso::Voxel const&& __restrict newData)
	{
		// wrap bounds //
		voxelIndex = p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		// Update Voxel
		((StreamingGrid* const __restrict)::grid)->setVoxel(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(newData));
	}

	void __vectorcall setVoxelsAt(rect2D_t voxelArea, Iso::Voxel const&& __restrict voxelReference)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				setVoxelAt(voxelIterate, std::forward<Iso::Voxel const&& __restrict>(voxelReference));

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

	}

	void __vectorcall setVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash) // static only
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				setVoxelHashAt<false>(voxelIterate, hash);
				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}
	void __vectorcall setVoxelsHashAt(rect2D_t const voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR) // dynamic only
	{
		// clamp is errornous at boundaries of world grid while voxelArea is rotated.
		// setVoxel clips to the bounds of the world grid, so no out of bounds access happens.

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());
		point2D_t p;

		// oriented rect filling algorithm, see processing sketch "rotation" for reference (using revised version)
		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				point2D_t const p0(p);
				p = p2D_rotate(voxelIterate, voxelArea.center(), -vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					setVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
					Iso::Voxel oVoxel(getVoxelAt(point2D_t(p.x, p0.y)));
					Iso::setColor(oVoxel, 0x00ff0000); // bgr
					Iso::setEmissive(oVoxel);
					setVoxelAt(point2D_t(p.x, p0.y), std::forward<Iso::Voxel const&&>(oVoxel));
#endif
#endif
				}

				// center 
				setVoxelHashAt<true>(p, hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
				Iso::Voxel oVoxel(getVoxelAt(p));
				Iso::setColor(oVoxel, 0x00ff0000); // bgr
				Iso::setEmissive(oVoxel);
				setVoxelAt(p, std::forward<Iso::Voxel const&&>(oVoxel));
#endif
#endif

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	int32_t const __vectorcall testVoxelsAt(rect2D_t const voxelArea, v2_rotation_t const& __restrict vR)
	{
		rect2D_t const visible_area(MinCity::VoxelWorld->getVisibleGridBounds());

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());
		point2D_t p;

		uint32_t visible_count(0);
		uint32_t voxel_count(0);

		// oriented rect filling algorithm, see processing sketch "rotation" for reference (using revised version)
		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				point2D_t const p0(p);
				p = p2D_rotate(voxelIterate, voxelArea.center(), -vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					
					visible_count += r2D_contains(visible_area, point2D_t(p.x, p0.y));
					++voxel_count;
				}

				// center 
				visible_count += r2D_contains(visible_area, p);
				++voxel_count;

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		if (0 == visible_count) { // completely outside
			return(0);
		}
		else if (visible_count != voxel_count) { // intersecting, some outside some inside
			return(-1);
		}
		
		// else, completely inside
		return(1);
		
	}

	int32_t const __vectorcall testVoxelsAt(rect2D_t const voxelArea)
	{
		rect2D_t const visible_area(MinCity::VoxelWorld->getVisibleGridBounds());

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		uint32_t visible_count(0);
		uint32_t voxel_count(0);

		// oriented rect filling algorithm, see processing sketch "rotation" for reference (using revised version)
		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				// center 
				visible_count += r2D_contains(visible_area, voxelIterate);
				++voxel_count;

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		if (0 == visible_count) { // completely outside
			return(0);
		}
		else if (visible_count != voxel_count) { // intersecting, some outside some inside
			return(-1);
		}

		// else, completely inside
		return(1);

	}

	static void __vectorcall clearVoxelsAt(rect2D_t voxelArea) // resets to "ground only"
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				Iso::Voxel oVoxel(getVoxelAt(voxelIterate));

				// Reset everything EXCEPT [ Height, Occlusion, ... ]
				Iso::resetAsGroundOnly(oVoxel);

				setVoxelAt(voxelIterate, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	void __vectorcall resetVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash) // static only
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				resetVoxelHashAt<false>(voxelIterate, hash);
				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}
	void __vectorcall resetVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR) // dynamic only
	{
		// clamp is errornous at boundaries of world grid while voxelArea is rotated.
		// setVoxel clips to the bounds of the world grid, so no out of bounds access happens.

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());
		point2D_t p;

		// oriented rect filling algorithm, see processing sketch "rotation" for reference (revised version)
		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				point2D_t const p0(p);
				p = p2D_rotate(voxelIterate, voxelArea.center(), -vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					resetVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
					Iso::Voxel oVoxel(getVoxelAt(point2D_t(p.x, p0.y)));
					Iso::setColor(oVoxel, 0); // bgr
					Iso::clearEmissive(oVoxel);
					setVoxelAt(point2D_t(p.x, p0.y), std::forward<Iso::Voxel const&&>(oVoxel));
#endif
#endif
				}

				// center 
				resetVoxelHashAt<true>(p, hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
				Iso::Voxel oVoxel(getVoxelAt(p));
				Iso::setColor(oVoxel, 0); // bgr
				Iso::clearEmissive(oVoxel);
				setVoxelAt(p, std::forward<Iso::Voxel const&&>(oVoxel));
#endif
#endif

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	// Grid Space (0,0) to (X, Y) Coordinates Only
	point2D_t const getNeighbourLocalVoxelIndex(point2D_t const voxelIndex, point2D_t const relativeOffset)
	{
		point2D_t voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

		// wrap bounds //
		voxelNeighbour = p2D_wrap_pow2(voxelNeighbour, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		// this function will also return the owning voxel
		// if zero,zero is passed in for the relative offset
		return(voxelNeighbour);
	}

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const getNeighbour(point2D_t voxelIndex, point2D_t const relativeOffset)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALF_WIDTH, Iso::WORLD_GRID_HALF_HEIGHT));

		point2D_t voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

		// this function will also return the owning voxel
		// if zero,zero is passed in for the relative offset

		// wrap bounds //
		voxelNeighbour = p2D_wrap_pow2(voxelNeighbour, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(((StreamingGrid const* const __restrict)::grid)->getVoxel(voxelNeighbour));
	}
	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const getNeighbourLocal(point2D_t const voxelIndex, point2D_t const relativeOffset)
	{
		point2D_t voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

		// this function will also return the owning voxel
		// if zero,zero is passed in for the relative offset

		// wrap bounds //
		voxelNeighbour = p2D_wrap_pow2(voxelNeighbour, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT));

		return(((StreamingGrid const* const __restrict)::grid)->getVoxel(voxelNeighbour));
	}
	
	static void smoothRow(point2D_t start, int32_t width)  // iterates from start.x to start.x + width (Left 2 Right)
	{
		int32_t const yMin(start.y - 1), yMax(start.y + 1);

		while (--width >= 0) {

			uint32_t uiWidthSum(0);
			float fNumSamples(0.0f);

			{ // Middle //
				uiWidthSum += getVoxelHeightAt(start); ++fNumSamples;
			}

			{ // Left //
				uiWidthSum += getVoxelHeightAt(point2D_t(start.x, yMin)); ++fNumSamples;
			}

			{ // Right //
				uiWidthSum += getVoxelHeightAt(point2D_t(start.x, yMax)); ++fNumSamples;
			}

			Iso::setHeightStep(getLocalVoxelIndexAt(start), SFM::round_to_u32((float)uiWidthSum / fNumSamples));

			++start.x;
		}
	}
	static void smoothColumn(point2D_t start, int32_t height)  // iterates from start.y to start.y + height (Top 2 Bottom)
	{
		int32_t const xMin(start.x - 1), xMax(start.x + 1);

		while (--height >= 0) {

			uint32_t uiHeightSum(0);
			float fNumSamples(0.0f);

			{ // Middle //
				uiHeightSum += getVoxelHeightAt(start); ++fNumSamples;
			}

			{ // Left //
				uiHeightSum += getVoxelHeightAt(point2D_t(xMin, start.y)); ++fNumSamples;
			}

			{ // Right //
				uiHeightSum += getVoxelHeightAt(point2D_t(xMax, start.y)); ++fNumSamples;
			}

			Iso::setHeightStep(getLocalVoxelIndexAt(start), SFM::round_to_u32((float)uiHeightSum / fNumSamples));

			++start.y;
		}
	}
	rect2D_t const voxelArea_grow(rect2D_t const voxelArea, point2D_t const grow)
	{
		// checks width and height too see if its and odd or even number
		// then grows according to the result - this gives the correct "growing" expected
		point2D_t const width_height(voxelArea.width_height());

		bool const width_even(!(width_height.x & 1)), height_even(!(width_height.y & 1));

		if (width_even & height_even) { // both even

			return(r2D_grow<true, true>(voxelArea, grow));

		}
		else if (!(width_even & height_even)) { // both odd 

			return(r2D_grow<true, false>(voxelArea, grow));
		}
		/*else*/ // xor, odd
		
		return(r2D_grow<true, false>(voxelArea, point2D_t(width_even ? grow.x : 0, height_even ? grow.y : 0)));
	}

	void smoothRect(rect2D_t voxelArea)
	{
		// grow by one to get desired "outline" area to smooth //
		voxelArea = voxelArea_grow(voxelArea, point2D_t(1,1));

		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t const width_height(voxelArea.width_height());
		// Left
		smoothColumn(voxelArea.left_top(), width_height.y);
		// Right
		smoothColumn(voxelArea.right_top(), width_height.y);
		// Top
		smoothRow(voxelArea.left_top(), width_height.x);
		// Bottom
		smoothRow(voxelArea.left_bottom(), width_height.x);
	}

	// Random //
	point2D_t const __vectorcall getRandomVoxelIndexInArea(rect2D_t const area)
	{
		return(point2D_t(PsuedoRandomNumber32(area.left, area.right),
						 PsuedoRandomNumber32(area.top, area.bottom)));
	}
	point2D_t const __vectorcall getRandomVisibleVoxelIndexInArea(rect2D_t const area)
	{
		rect2D_t const visible(MinCity::VoxelWorld->getVisibleGridBounds());

		if (r2D_contains(visible, area)) {
			return(getRandomVoxelIndexInArea(area));
		}
		return(getRandomVoxelIndexInArea(visible));
	}
	point2D_t const __vectorcall getRandomVisibleVoxelIndex()
	{
		return(getRandomVoxelIndexInArea(MinCity::VoxelWorld->getVisibleGridBounds()));
	}
	rect2D_t const __vectorcall getRandomNonVisibleAreaNear()
	{
		rect2D_t const
			visible_area(MinCity::VoxelWorld->getVisibleGridBounds());
		rect2D_t
			selected_area{};
		// randomly select area of voxels that are exterior to the visible voxel rect

		// - by direction (kinda like a quadrant, surrounding the visible rect)

		// note that vector indices != direction value after first round of finding a valid quadrant
		// so at no point assume they are equal, it only coindentally starts out that way
		vector<uint32_t> enabledDirections({  eDirection::NW, eDirection::N,  eDirection::NE,
											  eDirection::E,  eDirection::SE, eDirection::S,
											  eDirection::SW, eDirection::W });
		random_shuffle(enabledDirections.begin(), enabledDirections.end()); // adds more randomization at a very low cost

		bool bInvalidDirection(false);

		do
		{
			uint32_t direction;

			bool bIsEnabledDirection(false);
			vector<uint32_t>::iterator iterCurrent;

			do {
				uint32_t const pending_direction = (uint32_t const)PsuedoRandomNumber32(0, (int32_t)(enabledDirections.size()) - 1);

				for (vector<uint32_t>::iterator iter = enabledDirections.begin(); iter != enabledDirections.end(); ++iter) {
					if (pending_direction == *iter) {
						iterCurrent = iter; // for *potential* erasure
						direction = pending_direction;
						bIsEnabledDirection = true;
						break;
					}
				}
			} while (!bIsEnabledDirection); // must be an enabled direction, poll until randomly selected

			switch (direction)
			{
			case eDirection::NW:
				selected_area = r2D_add(visible_area, point2D_t(-visible_area.width(), -visible_area.height()));
				break;

			case eDirection::N:
				selected_area = r2D_add(visible_area, point2D_t(0, -visible_area.height()));
				break;

			case eDirection::NE:
				selected_area = r2D_add(visible_area, point2D_t(visible_area.width(), -visible_area.height()));
				break;

			case eDirection::E:
				selected_area = r2D_add(visible_area, point2D_t(visible_area.width(), 0));
				break;

			case eDirection::SE:
				selected_area = r2D_add(visible_area, point2D_t(visible_area.width(), visible_area.height()));
				break;

			case eDirection::S:
				selected_area = r2D_add(visible_area, point2D_t(0, visible_area.height()));
				break;

			case eDirection::SW:
				selected_area = r2D_add(visible_area, point2D_t(-visible_area.width(), visible_area.height()));
				break;

			case eDirection::W:
				selected_area = r2D_add(visible_area, point2D_t(-visible_area.width(), 0));
				break;
			}

			// clamp to world grid
			selected_area = r2D_min(selected_area, point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));
			selected_area = r2D_max(selected_area, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V));

			bInvalidDirection = ((0 == selected_area.width()) | (0 == selected_area.height()));

			if (bInvalidDirection) {
				enabledDirections.erase(iterCurrent); // reduce the set of quadrants to check for next iteration
			}

		} while (bInvalidDirection); // only if resulting quadrant, after clamping has a width and height that is greater than zero!

		return(selected_area);
	}
	point2D_t const __vectorcall getRandomNonVisibleVoxelIndexNear()
	{
		return(getRandomVoxelIndexInArea(getRandomNonVisibleAreaNear()));
	}

	/*
	point2D_t const p2D_GridToScreen(point2D_t thePt)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		thePt = p2D_add(thePt, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		return(p2D_add(Iso::p2D_GridToIso(p2D_sub(thePt, oCamera.voxelIndex_TopLeft)), oCamera.voxelOffset));
	}
	*/
	/*XMVECTOR const XM_CALLCONV v2_GridToScreen(FXMVECTOR thePt)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		thePt = XMVectorAdd(thePt, { (float)Iso::WORLD_GRID_HALFSIZE, (float)Iso::WORLD_GRID_HALFSIZE, 0.0f, 0.0f });

		return(v2_add(Iso::v2_GridToIso(v2_sub(thePt, oCamera.voxelIndex_vector_TopLeft)), oCamera.voxelOffset_vector));
	}*/

} // end ns world

// Lighting:
// move from uniform lightpos to per voxel vertex decl
// need position + color
// these are calculated from "neighbouring" voxels that have emiissive properties (palette color + position), number of levels out (neighbours out) to be determined,,,
// contribution/weighting of light from n emmisive minivoxels is defined by normalized distance from emmisive voxel
// to *this voxel. Final weighted color and position are passed per voxel
// the "uniform constrained light" that currently exists could be used as ambient light
// emmisive voxels could be "expanded" (along normals for volumetric scattering effect, regular emmisive voxel + expansion "scattering fade"
// todo!

// PREVENT REDUNDANT RELOADS BY USING XMGLOBALCONST for XMVECTORF32 for applicable variables
//XMGLOBALCONST inline XMVECTORF32 const _voxelGridToLocal{ Volumetric::Allocation::VOXEL_GRID_VISIBLE_X, Volumetric::Allocation::VOXEL_GRID_VISIBLE_Z };
//XMGLOBALCONST inline XMVECTORF32 const _xmInverseVisible{ Volumetric::INVERSE_GRID_VISIBLE_X, Volumetric::INVERSE_GRID_VISIBLE_Z, Volumetric::INVERSE_GRID_VISIBLE_X, Volumetric::INVERSE_GRID_VISIBLE_Z };
//XMGLOBALCONST inline XMVECTORF32 const _visibleLight{ 2.0f, 1.0f, 2.0f, 1.0f }; // for scaling model extents to account for potential lighting radius

template<typename VertexDeclaration>
struct voxelBuffer {
	inline static constexpr VertexDeclaration const type;
	
	VertexDeclaration*                     direct{};
	size_t                                 active_size{}; // direct buffer active size only

	vku::double_buffer<vku::GenericBuffer> staging;  // WC (WriteCombined) Memory, only sequential writes and no reads allowed.

	constexpr voxelBuffer() = default; // allow constinit optimization.

	~voxelBuffer()
	{
		if (nullptr != direct) {
			tbb::cache_aligned_allocator< VertexDeclaration > deallocator;

			deallocator.deallocate(direct, 0);
			direct = nullptr;
		}
		active_size = 0;
	}
private:
	// Deny assignment
	voxelBuffer& operator=(const voxelBuffer&) = delete;

	// Deny copy construction
	voxelBuffer(const voxelBuffer&) = delete;
};

// all instances belong in here
namespace // private to this file (anonymous)
{
	constinit inline static struct voxelData
	{
		// *bugfix - bit_row must be atomic, otherwise random voxels are not rendered
		struct {

			struct {
				voxelBuffer<VertexDecl::VoxelDynamic>
					buffer;
				bit_row_atomic<Volumetric::dynamic_direct_buffer_size>*
					bits{};
			} opaque;

			struct {
				voxelBuffer<VertexDecl::VoxelDynamic>
					buffer;
				bit_row_atomic<Volumetric::dynamic_direct_buffer_size>*
					bits{};
			} trans;
		} visibleDynamic;

		struct {
			voxelBuffer<VertexDecl::VoxelNormal>
				buffer;
			bit_row_atomic<Volumetric::static_direct_buffer_size>*
				bits{};
		} visibleStatic;

		struct {
			voxelBuffer<VertexDecl::VoxelNormal>
				buffer;
			bit_row_atomic<Volumetric::terrain_direct_buffer_size>*
				bits{};
		} visibleTerrain;

	} voxels;
}

namespace // private to this file (anonymous)
{
	struct voxelRender // ** all methods must be static inline ** //
	{
#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
		static struct
		{
			size_t	numDynamicVoxelsRendered = 0,
					numStaticVoxelsRendered = 0,
					numTerrainVoxelsRendered = 0;

			size_t  numLightVoxelsRendered = 0;

		} inline render_state{};
#endif
#endif

		// this construct significantly improves throughput of voxels, by batching the streaming stores //
		// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)
		using VoxelLocalBatch = sBatchedByIndexOut<VertexDecl::VoxelNormal, eStreamingBatchSize::GROUND>;
		
		STATIC_INLINE_PURE void XM_CALLCONV RenderGround(XMVECTOR xmVoxelOrigin, point2D_t const voxelIndex, point2D_t renderIndex,      // voxelIndex is already transformed local
			Iso::Voxel const& __restrict oVoxel,
			Volumetric::voxelBufferReference_Terrain& __restrict grounds,
			VoxelLocalBatch& __restrict localGround)
		{
			XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmVoxelOrigin, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));
			
			[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						   && XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
			{
				// **** HASH FORMAT 32bits available //
				uint32_t const color(0x00FFFFFF & Iso::getColor(oVoxel));
				bool const emissive((Iso::isEmissive(oVoxel) & (bool)color)); // if color is true black it's not emissive

				// Build hash //
				uint32_t groundHash(0);
				auto const [heightstep, minheightstep, adjacency] = groundAdjacency(voxelIndex);

				groundHash |= adjacency;			                                //			        	          0011 1111
				groundHash |= (emissive << 6);                                      //		                          R1xx xxxx
				groundHash |= (((uint32_t)heightstep) << 8);                        //            1111 1111 1111 1111 xxxx xxxx
				groundHash |= (((uint32_t)minheightstep) << 24);	                //  1111 1111 xxxx xxxx xxxx xxxx xxxx xxxx
				
				XMVECTOR xmUVs(XMVectorMultiply(p2D_to_v2(voxelIndex), XMVectorSet(Iso::INVERSE_WORLD_GRID_FWIDTH, Iso::INVERSE_WORLD_GRID_FHEIGHT, 0.0f, 0.0f)));

				xmUVs = XMVectorSetW(xmUVs, (float)color);

				renderIndex = p2D_add(renderIndex, point2D_t(Iso::SCREEN_VOXELS >> 1)); // convert [-128...128] to [0...256]
				uint32_t const index(renderIndex.y * Iso::SCREEN_VOXELS + renderIndex.x);

				localGround.emplace_back(
					grounds.voxels, index,

					xmVoxelOrigin,
					xmUVs,
					groundHash
				);

				grounds.bits->set_bit(index);

				if (emissive) {

					// add light, offset to the height of this ground voxel
					Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(XMVectorSetY(xmIndex, Iso::getRealHeight(voxelIndex) / Iso::VOX_SIZE + 1.0f), color); // transform, so its on the top effectively applies proper height for light
				}
#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
				++render_state.numTerrainVoxelsRendered;
#endif
#endif
			}
		}

		template<bool const Dynamic>
		STATIC_INLINE bool const XM_CALLCONV RenderModel(uint32_t const index, FXMVECTOR xmVoxelOrigin, point2D_t const& __restrict voxelIndex,
			bool bVisible, Iso::Voxel const& __restrict oVoxel,
			Volumetric::voxelBufferReference_Static& __restrict statics,
			Volumetric::voxelBufferReference_Dynamic& __restrict dynamics,
			Volumetric::voxelBufferReference_Dynamic& __restrict trans,
			tbb::affinity_partitioner& __restrict part)
		{
			auto const ModelInstanceHash = Iso::getHash(oVoxel, index);
			auto const * const FoundModelInstance = MinCity::VoxelWorld->lookupVoxelModelInstance<Dynamic>(ModelInstanceHash);

			[[likely]] if (FoundModelInstance) {

				XMVECTOR xmPreciseOrigin(FoundModelInstance->getLocation()); // *bugfix - precise model origin is now used properly so fractional component is actually there. extremely precise. *do not change* *major bugfix*
				XMVECTOR const xmOffset(XMVectorSubtract(XMVectorSetY(xmPreciseOrigin, 0.0f), xmVoxelOrigin));
				// this all allows fractional movement for models (smooth) *do not change*
				xmPreciseOrigin = XMVectorSubtract(xmPreciseOrigin, XMVectorFloor(xmOffset));
				xmPreciseOrigin = XMVectorSelect(xmPreciseOrigin, XMVectorNegate(xmPreciseOrigin), XMVectorSelectControl(0, 1, 0, 0)); // negate y-axis starts here

				if (!bVisible) { // *bugfix - models could still be visible if ground voxel is not, this will also force the ground voxel visible if the model is visible (back in RenderGrid) - good thing for the opacitymap will have thoe ground voxels that would be otherwise missing when they are so close to the view frustum.
					bVisible = Volumetric::VolumetricLink->Visibility.AABBTestFrustum(xmPreciseOrigin, XMVectorScale(XMLoadFloat3A(&FoundModelInstance->getModel()._Extents), Iso::VOX_STEP));
				}
				// lighting from instance is still "rendered/added to light buffer" but no voxels are rendered.
				// voxels of model are not rendered. it is not currently visible. the light emitted from the model may still be visible - so the ^^^^above is done.

				bVisible = FoundModelInstance->Render(xmPreciseOrigin, voxelIndex, bVisible, 
					                                  statics, dynamics, trans, part);

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS

				if (bVisible) {
					size_t const voxel_count = (size_t const)FoundModelInstance->getModel()._numVoxels;
					if constexpr (Dynamic) {
						render_state.numDynamicVoxelsRendered += voxel_count;
					}
					else {
						render_state.numStaticVoxelsRendered += voxel_count;
					}
				}

				// light is rendered either way //
				size_t const voxel_count = (size_t const)FoundModelInstance->getModel()._numVoxelsEmissive;
				render_state.numLightVoxelsRendered += voxel_count;
#endif
#endif
			}

			return(bVisible);
		}

		STATIC_INLINE void __vectorcall RenderMaxVisibleVolumeLight(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex)  // this is for optimizing compute light dispatch, by exeecuting only below the maximum *visible* volume height
		{                                                                                                                 // by placing a "dummy" light that registers the maximum bound of the heoght of the light volume that is visible on-screen
			static constexpr float const voxel_radius(Volumetric::volumetricVisibility::getVoxelRadius());                                 // because nothing above this height requires lighting, so it's not computed!
			float current_height(Iso::getRealHeight(voxelIndex));

			while (isVoxelVisible(XMVectorSetY(xmVoxelOrigin, -current_height), voxel_radius)) {
				current_height += voxel_radius * 2.0f;
			}

			XMVECTOR const xmIndex(XMVectorMultiplyAdd(XMVectorSetY(xmVoxelOrigin, -current_height), Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

			[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
				           && XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
			{
				Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, 0u); // no color, "dummy" light that is ignored in effect, however the bounds of the light volume still adjust.
			}
			else {
				// just add to the very top of the volume if errornous on bounds check
				Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(XMVectorSetY(xmIndex, (float)(Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y - 1)), 0u); // no color, "dummy" light that is ignored in effect, however the bounds of the light volume still adjust.
			}
		}

		static void XM_CALLCONV RenderGrid(point2D_t const voxelStart,
			Volumetric::voxelBufferReference_Terrain&& __restrict grounds,
			Volumetric::voxelBufferReference_Static&& __restrict statics,
			Volumetric::voxelBufferReference_Dynamic&& __restrict dynamics,
			Volumetric::voxelBufferReference_Dynamic&& __restrict trans,
			tbb::affinity_partitioner& __restrict part)
		{
			ZoneScopedN("RenderGrid");

			typedef struct no_vtable sRenderFuncBlockChunk {

			private:
				StreamingGrid const* const __restrict		    streamingGrid;

				point2D_t const 								voxelStart;
				rect2D_t const                                  visibleArea;
				
				Volumetric::voxelBufferReference_Terrain& __restrict grounds;
				Volumetric::voxelBufferReference_Static& __restrict  statics;
				Volumetric::voxelBufferReference_Dynamic& __restrict dynamics;
				Volumetric::voxelBufferReference_Dynamic& __restrict trans;
				
				tbb::affinity_partitioner& __restrict part;

				sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
			public:
				__forceinline explicit sRenderFuncBlockChunk(
					StreamingGrid const* const __restrict streamingGrid_,
					point2D_t const voxelStart_,
					rect2D_t const visibleArea_,
					Volumetric::voxelBufferReference_Terrain& __restrict grounds_,
					Volumetric::voxelBufferReference_Static& __restrict statics_,
					Volumetric::voxelBufferReference_Dynamic& __restrict dynamics_,
					Volumetric::voxelBufferReference_Dynamic& __restrict trans_,
					tbb::affinity_partitioner& __restrict part_)
					: streamingGrid(streamingGrid_), voxelStart(voxelStart_), visibleArea(visibleArea_),
					grounds(grounds_),
					statics(statics_), 
					dynamics(dynamics_), 
					trans(trans_),
					part(part_)
				{}

				void __vectorcall operator()(tbb::blocked_range2d<int32_t, int32_t> const& r) const {

					VoxelLocalBatch localGround{};

					int32_t const	// pull out into registers from memory
						y_begin(r.rows().begin()),
						y_end(r.rows().end()),
						x_begin(r.cols().begin()),
						x_end(r.cols().end());

					point2D_t voxelIndex; // *** range is [0...WORLD_GRID_SIZE] for voxelIndex here *** //

#pragma loop( ivdep )
					for (voxelIndex.y = y_begin; voxelIndex.y < y_end; ++voxelIndex.y) 
					{
#pragma loop( ivdep )
						for (voxelIndex.x = x_begin; voxelIndex.x < x_end; ++voxelIndex.x)
						{
							// *bugfix: Rendering is FRONT to BACK only (roughly)
							point2D_t const voxelIndexWrapped(p2D_wrap_pow2(voxelIndex, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT))); // [0...16384] world grid coord
							
							// Make index relative to starting index voxel
							point2D_t const renderIndex(p2D_sub(voxelIndex, voxelStart)); // [-128...128] visible grid relative coord

							// *bugfix - this trickles down thru the voxel output position, to uv's, to light emitter position in the lightmap. All is the same. don't fuck with the fractional offset, it's not required here
							XMVECTOR const xmVoxelOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(renderIndex))); // [0.0...256.0]
							
							// test ground voxel visible at ground height
#if !defined(NDEBUG) && defined(DEBUG_WORLD_PLANE_HEIGHT)
							float const fWorldPlaneHeightOffset(getDebugVariable(float, DebugLabel::WORLD_PLANE_HEIGHT));
							bool bRenderVisible(isVoxelVisible(XMVectorSetY(xmVoxelOrigin, -Iso::getRealHeight(voxelIndexWrapped) + fWorldPlaneHeightOffset), Volumetric::volumetricVisibility::getVoxelRadius()));
#else // normal:
							bool bRenderVisible(isVoxelVisible(XMVectorSetY(xmVoxelOrigin, -Iso::getRealHeight(voxelIndexWrapped)), Volumetric::volumetricVisibility::getVoxelRadius()));
#endif
							/* @todo (optional)
							if (Iso::isOwner(oVoxel, Iso::GROUND_HASH) && isExtended(oVoxel))
							{
								switch (getExtendedType(oVoxel))
								{
								case Iso::EXTENDED_TYPE_ROAD:
									// todo
									break;
								case Iso::EXTENDED_TYPE_WATER:
									// todo
									break;
									// default should not exist //
								}
							} // extended
							*/
							Iso::Voxel const oVoxel(streamingGrid->getVoxel(voxelIndexWrapped));

							if (Iso::isOwner(oVoxel, Iso::STATIC_HASH))	// only roots actually do rendering work.
							{
#ifndef DEBUG_NO_RENDER_STATIC_MODELS
 								bRenderVisible |= RenderModel<false>(Iso::STATIC_HASH, xmVoxelOrigin, voxelIndexWrapped, bRenderVisible, oVoxel, statics, dynamics, trans, part);
#endif
							} // root
							// a voxel in the grid can have a static model and dynamic model simultaneously
							if (Iso::isOwnerAny(oVoxel, Iso::DYNAMIC_HASH)) { // only if there are dynamic hashes which this voxel owns
								for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
									if (Iso::isOwner(oVoxel, i)) {

										bRenderVisible |= RenderModel<true>(i, xmVoxelOrigin, voxelIndexWrapped, bRenderVisible, oVoxel, statics, dynamics, trans, part);
									}
								}
							}

							if (bRenderVisible && r2D_contains(visibleArea, voxelIndexWrapped)) {
								RenderGround(xmVoxelOrigin, voxelIndexWrapped, renderIndex, oVoxel, grounds, localGround);
							}
#if !defined(NDEBUG) && defined(DEBUG_WORLD_PLANE_HEIGHT)
							else {
								Iso::Voxel oOutVoxel(oVoxel);
								Iso::setColor(oOutVoxel, 0x0000007f);
								Iso::setEmissive(oOutVoxel);
								RenderGround(xmVoxelOrigin, voxelIndexWrapped, renderIndex, oOutVoxel, grounds, localGround);
							}
#endif
						} // for

					} // for                                                                                                                             

					// ####################################################################################################################
			        // ensure all batches are output (RESIDUAL)
					localGround.out(grounds.voxels);
					// ####################################################################################################################
				} // operation

			} const RenderFuncBlockChunk;

			// *****************************************************************************************************************//
																																
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION																					
			tTime const tGridStart(high_resolution_clock::now());																
#endif		
#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
			render_state.numDynamicVoxelsRendered = 0,
			render_state.numStaticVoxelsRendered = 0,
			render_state.numTerrainVoxelsRendered = 0;
			render_state.numLightVoxelsRendered = 0;
#endif
#endif
			{
				point2D_t voxelReset(p2D_add(voxelStart, Iso::GRID_OFFSET));
				point2D_t voxelEnd(p2D_add(voxelReset, point2D_t(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z)));

				rect2D_t const visibleArea(voxelReset, voxelEnd); // store original visible area before extension rect2D_t const visible_area(MinCity::VoxelWorld->getVisibleGridBounds());

				// account for maximum model dimensions / 2 (extending in or out of the screen visible area, prevents "popping" visibility of voxel models //
				//constexpr uint32_t const half_maximum_extent((Volumetric::MODEL_MAX_DIMENSION_XYZ >> 1u) / MINIVOXEL_FACTOR);
				//voxelReset = p2D_subs(voxelReset, half_maximum_extent);
				//voxelEnd = p2D_adds(voxelEnd, half_maximum_extent);

				
				// serial
				//RenderFuncBlockChunk(((StreamingGrid const* const __restrict)::grid), voxelStart, visibleArea, grounds, statics, dynamics, trans, part)
				//	(tbb::blocked_range2d<int32_t, int32_t>(voxelReset.y, voxelEnd.y, eThreadBatchGrainSize::GRID_RENDER_2D,
				//			                                voxelReset.x, voxelEnd.x, eThreadBatchGrainSize::GRID_RENDER_2D));
				
				
				// parallel
				tbb::parallel_for(tbb::blocked_range2d<int32_t, int32_t>(voxelReset.y, voxelEnd.y, eThreadBatchGrainSize::GRID_RENDER_2D,
					                                                     voxelReset.x, voxelEnd.x, eThreadBatchGrainSize::GRID_RENDER_2D), // **critical loop** 
					RenderFuncBlockChunk(((StreamingGrid const* const __restrict)::grid), voxelStart, visibleArea, 
						                 grounds, statics, dynamics, trans, part), part
				);
				
			}

			{ // optimization for compute:

				point2D_t voxelReset(p2D_add(voxelStart, Iso::GRID_OFFSET));
				point2D_t voxelCenter(p2D_add(voxelReset, p2D_half(point2D_t(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z))));

				XMVECTOR const xmVoxelOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(p2D_sub(voxelCenter, voxelStart)))); // make relative to render start position

				RenderMaxVisibleVolumeLight(xmVoxelOrigin, p2D_wrap_pow2(voxelCenter, point2D_t(Iso::WORLD_GRID_WIDTH, Iso::WORLD_GRID_HEIGHT)));
			}

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			PerformanceResult& result(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
			result.grid_duration = high_resolution_clock::now() - tGridStart;
			++result.grid_count;
#endif
		}
	}; // end struct voxelRender
} // end ns

namespace Volumetric
{
	constinit inline voxLink* VolumetricLink{ nullptr };
} // end ns

namespace world
{
	cVoxelWorld::cVoxelWorld()
		:
		_lastState{}, _currentState{}, _targetState{}, _occlusion{},
		_vMouse{}, _lastOcclusionQueryValid(false), _inputEnabledBits{},
		_onLoadedRequired(true),
		_terrainDetail(nullptr),
		_terrainTexture(nullptr), _terrainTexture2(nullptr), _gridTexture(nullptr), _blackbodyTexture(nullptr), _heightmap(nullptr), _spaceCubemapTexture(nullptr),
		_blackbodyImage(nullptr), _tBorderScroll(Iso::CAMERA_SCROLL_DELAY),
		_sequence(GenerateVanDerCoruptSequence<30, 2>()),
		_AsyncClearTaskID(0)
#ifdef DEBUG_STORAGE_BUFFER
		, DebugStorageBuffer(nullptr)
#endif
	{
		Volumetric::VolumetricLink = new Volumetric::voxLink{ *this, _OpacityMap, _Visibility };
		
		_occlusion.tToOcclude = Volumetric::Konstants::OCCLUSION_DELAY;
	}
} // end ns

namespace Iso
{
	ImagingMemoryInstance* const& __restrict Voxel::HeightMap() {
		return(MinCity::VoxelWorld->getHeightMapImage());
	}

	/*
	STATIC_INLINE point2D_t const __vectorcall cartToSphere(point2D_t voxelIndex, ImagingMemoryInstance const* const image)
	{
		float const image_size(image->xsize); // square images only
		XMVECTOR xmUV = uvec4_v(voxelIndex.v).v4f();
		xmUV = XMVectorScale(xmUV, 1.0f/image_size);
		xmUV = SFM::__fms(xmUV, XMVectorReplicate(2.0f), XMVectorReplicate(1.0f));

		XMVECTOR xmN = xmUV;
		xmN = XMVectorSetZ(xmUV, SFM::__sqrt(1.0f - XMVectorGetX(xmUV)*XMVectorGetX(xmUV) - XMVectorGetY(xmUV) * XMVectorGetY(xmUV)));

		float const r = XMVectorGetX(XMVector3Dot(xmN, xmN));
		xmN = XMVectorScale(xmN, 1.0f / r);

		XMVECTOR xmSph = XMVectorSet(atan2(XMVectorGetX(xmN), XMVectorGetZ(xmN)), asin(XMVectorGetY(xmN)), 0.0f, 0.0f);

		xmSph = SFM::__fma(xmSph, XMVectorReplicate(0.5f), XMVectorReplicate(0.5f));
		xmSph = XMVectorScale(xmSph, image_size - 1.0f);
		xmSph = SFM::clamp(xmSph, XMVectorZero(), XMVectorReplicate(image_size - 1.0f));

		return(v2_to_p2D(xmSph));
		
		// n = vec3(uv, sqrt(1.0 - uv.x * uv.x - uv.y * uv.y));
		/*
		vec3 cartToSphere(vec3 cart) {

			float r = dot(cart, cart);
			vec3 n = cart / (r);
			return vec3(atan(n.x, n.z), asin(n.y), 1.0 / (1.0 + r * r));
		}
	}*/

	__declspec(safebuffers) heightstep const __vectorcall Voxel::HeightMap(point2D_t const voxelIndex)
	{
		uint16_t const* const __restrict heights((uint16_t const* const __restrict)HeightMap()->block);

		return{ heights[voxelIndex.y * Iso::WORLD_GRID_WIDTH + voxelIndex.x] };
	}
	__declspec(safebuffers) heightstep* const __restrict __vectorcall Voxel::HeightMapReference(point2D_t const voxelIndex)
	{
		uint16_t* const __restrict heights((uint16_t* const __restrict)HeightMap()->block);

		return( &heights[voxelIndex.y * Iso::WORLD_GRID_WIDTH + voxelIndex.x] );
	}
} // end ns

namespace world
{
	//
	//[[deprecated]] void cVoxelWorld::makeTextureShaderOutputsReadOnly(vk::CommandBuffer const& __restrict cb)
	//{
	//	for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {
	//		_textureShader[shader].output->setLayoutFragmentFromCompute(cb, vku::ACCESS_WRITEONLY);
	//	}
	//}

	/* [[deprecated]] void cVoxelWorld::createTextureShader(uint32_t const shader, vku::GenericImage* const& __restrict input, bool const referenced, point2D_t const shader_dimensions, vk::Format const format)
	{
		static constexpr uint32_t const
			COMPUTE_LOCAL_SIZE_BITS = 3u,	// 2^3 = 8
			COMPUTE_LOCAL_SIZE = (1u << COMPUTE_LOCAL_SIZE_BITS);	// 2^3 = 8

		_textureShader[shader].input = input;
		_textureShader[shader].referenced = referenced;

		vk::Extent3D extents{};
		
		if (!referenced || shader_dimensions.isZero()) {	// output extents based on new input textures only
															// if input texture is referenced - output texture (shader_dimensions) should be defined / customized
			extents = _textureShader[shader].input->extent();
		}
		else {
			extents.width = shader_dimensions.x;
			extents.height = shader_dimensions.y;
		}

		// output texture
		_textureShader[shader].output = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, MinCity::Vulkan->getDevice(), extents.width, extents.height, 1, vk::SampleCountFlagBits::e1, format);

		// indirect dispatch
		vk::DispatchIndirectCommand const dispatchCommand{

					(extents.width >> COMPUTE_LOCAL_SIZE_BITS) + (0u == (extents.width % COMPUTE_LOCAL_SIZE) ? 0u : 1u), // local size x = 8
					(extents.height >> COMPUTE_LOCAL_SIZE_BITS) + (0u == (extents.height % COMPUTE_LOCAL_SIZE) ? 0u : 1u), // local size y = 8
					1
		};

		_textureShader[shader].indirect_buffer = new vku::IndirectBuffer(sizeof(dispatchCommand));

		_textureShader[shader].indirect_buffer->upload(MinCity::Vulkan->getDevice(), MinCity::Vulkan->computePool(1), MinCity::Vulkan->computeQueue(1), dispatchCommand); // uses opposite compute queue in all cases, to be parallel with first compute queue which is for lighting only.

	}*/

	// *** best results if input texture is square, a power of two, and cleanly divides by computes local size (8) with no remainder.
	/* [[deprecated]] void cVoxelWorld::createTextureShader(uint32_t const shader, std::wstring_view const szInputTexture)
	{
		// input texture
		MinCity::TextureBoy->LoadKTXTexture(reinterpret_cast<vku::TextureImage3D*& __restrict>(_textureShader[shader].input), szInputTexture);
		createTextureShader(shader, _textureShader[shader].input, false);
	} */

	void cVoxelWorld::LoadTextures()
	{
		MinCity::TextureBoy->Initialize();

		// Load bluenoise *** must be done here first so that noise is available
		{
			supernoise::blue.Load(TEXTURE_DIR L"bluenoise.ktx");
			MinCity::TextureBoy->AddTextureToTextureArray(supernoise::blue.getTexture2DArray(), TEX_BLUE_NOISE);
#ifndef NDEBUG
#ifdef DEBUG_EXPORT_BLUENOISE_KTX // Saved from MEMORY (float data)
			// validation test - save blue noise texture from resulting 1D blue noise function
			Imaging imgNoise = ImagingNew(eIMAGINGMODE::MODE_L, BLUENOISE_DIMENSION_SZ, BLUENOISE_DIMENSION_SZ);

			size_t psuedoFrame(0);

			for (int32_t y = BLUENOISE_DIMENSION_SZ - 1; y >= 0; --y) {
				for (int32_t x = BLUENOISE_DIMENSION_SZ - 1; x >= 0; --x) {
					imgNoise->block[y * BLUENOISE_DIMENSION_SZ + x] = SFM::saturate_to_u8(supernoise::blue.get1D(psuedoFrame++) * 255.0f);
				}
			}

			ImagingSaveToKTX(imgNoise, DEBUG_DIR "bluenoise_test.ktx"); // R or L - Single channel

			ImagingDelete(imgNoise);
#endif
#endif
		}
		
		// Load Terrain Texture
#if !defined(NDEBUG) && defined(DEBUG_ALIGNMENT_TERRAIN)
		MinCity::TextureBoy->LoadKTXTexture(_terrainTexture, DEBUG_DIR "test_pattern_map.ktx");
#else
		MinCity::TextureBoy->LoadKTXTexture(_terrainTexture, TEXTURE_DIR "moon_height_derivative.ktx");
#endif
		MinCity::TextureBoy->AddTextureToTextureArray(_terrainTexture, TEX_TERRAIN);

		MinCity::TextureBoy->LoadKTXTexture(_terrainTexture2, TEXTURE_DIR "moon_albedo_rough_ao.ktx");

		MinCity::TextureBoy->AddTextureToTextureArray(_terrainTexture2, TEX_TERRAIN2);

		// other textures:
		// 
		// terrain grid mipmapped texture
		MinCity::TextureBoy->LoadKTXTexture(_gridTexture, TEXTURE_DIR L"grid.ktx");
		MinCity::TextureBoy->AddTextureToTextureArray(_gridTexture, TEX_GRID);

		Imaging const blackbodyImage(ImagingLoadRawBGRA(TEXTURE_DIR "blackbody.data", BLACKBODY_IMAGE_WIDTH, 1));
		MinCity::TextureBoy->ImagingToTexture<false>(blackbodyImage, _blackbodyTexture);
		MinCity::TextureBoy->AddTextureToTextureArray(_blackbodyTexture, TEX_BLACKBODY);

		_blackbodyImage = blackbodyImage; // save image to be used for blackbody radiation light color lookup

		// other textures, not part of common texture array:
		MinCity::TextureBoy->LoadKTXTexture(_terrainDetail, TEXTURE_DIR "moon_detail.ktx");
		MinCity::TextureBoy->LoadKTXTexture(_spaceCubemapTexture, TEXTURE_DIR "space_cubemap.ktx2");
		 
		// [[deprecated]] texture shaders:
		//createTextureShader(eTextureShader::WIND_FBM, supernoise::blue.getTexture2D(), true, point2D_t(256, 256), vk::Format::eR16Unorm); // texture shader uses single channel, single slice of bluenoise.
		//createTextureShader(eTextureShader::WIND_DIRECTION, _textureShader[eTextureShader::WIND_FBM].output, true, point2D_t(256, 256), vk::Format::eR16G16B16A16Unorm); // will use same dimensions as input, which is the output defined previously.

#ifndef NDEBUG
#ifdef DEBUG_EXPORT_BLACKBODY_KTX
		ImagingSaveToKTX(blackbodyImage, DEBUG_DIR "blackbody_test.ktx");
#endif
#endif
	}

	// image functions
	uvec4_v const __vectorcall cVoxelWorld::blackbody(float const norm) const
	{
		static constexpr float const FBLACKBODY_IMAGE_WIDTH = float(BLACKBODY_IMAGE_WIDTH - 1);

		uint32_t const index(SFM::floor_to_u32(SFM::__fma(SFM::clamp(norm, 0.0f, 1.0f), FBLACKBODY_IMAGE_WIDTH, 0.5f)));

		uint32_t const* const __restrict pIn = reinterpret_cast<uint32_t const* const __restrict>(_blackbodyImage->block);
		uint32_t const packed_rgba = pIn[index];

		uvec4_t rgba;
		SFM::unpack_rgba(packed_rgba, rgba);

		return(uvec4_v(rgba));
	}

	void cVoxelWorld::Initialize()
	{
		_Visibility.Initialize();

		::grid._protected = &_streamingGrid; // assign global reference (global only to this file)
		_streamingGrid.Initialize();

		GenerateGround();
#ifndef NDEBUG
		OutputVoxelStats();
#endif
		_OpacityMap.create(MinCity::Vulkan->getDevice(), MinCity::Vulkan->computePool(), MinCity::Vulkan->computeQueue(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue(), MinCity::getFramebufferSize(), MinCity::hardware_concurrency());
		MinCity::PostProcess->create(MinCity::Vulkan->getDevice(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue(), MinCity::getFramebufferSize(), MinCity::Vulkan->isHDR());
		createAllBuffers(MinCity::Vulkan->getDevice(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue());
		
		Volumetric::LoadAllVoxelModels();

		if (!Volumetric::isNewModelQueueEmpty()) {
			MinCity::DispatchEvent(eEvent::SHOW_IMPORT_WINDOW);
			_importing = true;
		}
		else {
			MinCity::DispatchEvent(eEvent::SHOW_MAIN_WINDOW);
		}
#ifdef DEBUG_STORAGE_BUFFER
		DebugStorageBuffer = new vku::StorageBuffer(sizeof(UniformDecl::DebugStorageBuffer), false, vk::BufferUsageFlagBits::eTransferDst);
		DebugStorageBuffer->upload(MinCity::Vulkan->getDevice(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue(), init_debug_buffer);
#endif
	}

	void cVoxelWorld::create_game_object(uint32_t const hash, uint32_t const gameobject_type)
	{
		world::access::create_game_object(hash, gameobject_type, std::forward<mapVoxelModelInstancesStatic&& __restrict>(_hshVoxelModelInstances_Static), std::forward<mapVoxelModelInstancesDynamic&& __restrict>(_hshVoxelModelInstances_Dynamic));
	}

	void cVoxelWorld::upload_model_state(vector<model_root_index> const& __restrict data_rootIndex, vector<model_state_instance_static> const& __restrict data_models_static, vector<model_state_instance_dynamic> const& __restrict data_models_dynamic)
	{
		Clear();
		
		{ // do all root indices
			for (size_t i = 0; i < data_rootIndex.size(); ++i) {
				_hshVoxelModelRootIndex.emplace(data_rootIndex[i].hash, data_rootIndex[i].voxelIndex);
			}
		}

		{ // do all static
			for (size_t i = 0; i < data_models_static.size(); ++i) {

				uint32_t const hash(data_models_static[i].hash);

				mapRootIndex::const_iterator const iter(_hshVoxelModelRootIndex.find(hash));

				if (_hshVoxelModelRootIndex.cend() != iter) {

					using voxelModelStatic = Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC>;
					// get model
					voxelModelStatic const* const __restrict voxelModel = Volumetric::getVoxelModel<false>(data_models_static[i].identity._modelGroup, data_models_static[i].identity._index);
					if (voxelModel) {
						Volumetric::voxelModelInstance_Static* pInstance = Volumetric::voxelModelInstance_Static::create(*voxelModel, hash, iter->second);
						if (pInstance) {
							// keep in unordered map container
							_hshVoxelModelInstances_Static[hash] = pInstance;
							// create associated game object
							create_game_object(hash, data_models_static[i].gameobject_type);

							// set additional varying data

							// * static location is derived from voxelIndex
						}
					}

				}
			}
		}

		{ // do all dynamic
			for (size_t i = 0; i < data_models_dynamic.size(); ++i) {

				uint32_t const hash(data_models_dynamic[i].hash);

				mapRootIndex::const_iterator const iter(_hshVoxelModelRootIndex.find(hash));

				if (_hshVoxelModelRootIndex.cend() != iter) {

					using voxelModelDynamic = Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC>;
					// get model
					voxelModelDynamic const* const __restrict voxelModel = Volumetric::getVoxelModel<true>(data_models_dynamic[i].identity._modelGroup, data_models_dynamic[i].identity._index);
					if (voxelModel) {
						Volumetric::voxelModelInstance_Dynamic* pInstance = Volumetric::voxelModelInstance_Dynamic::create(*voxelModel, hash, iter->second);
						if (pInstance) {
							// keep in unordered map container
							_hshVoxelModelInstances_Dynamic[hash] = pInstance;
							// create associated game object
							create_game_object(hash, data_models_dynamic[i].gameobject_type);

							// set additional varying data
							pInstance->setRoll(v2_rotation_t(data_models_dynamic[i].roll.x, data_models_dynamic[i].roll.y, data_models_dynamic[i].roll.z));
							pInstance->setYaw( v2_rotation_t(data_models_dynamic[i].yaw.x, data_models_dynamic[i].yaw.y, data_models_dynamic[i].yaw.z));
							pInstance->setPitch(v2_rotation_t(data_models_dynamic[i].pitch.x, data_models_dynamic[i].pitch.y, data_models_dynamic[i].pitch.z));
							pInstance->setLocation(XMLoadFloat3(&data_models_dynamic[i].location));
						}
					}

				}
			}
		}


	}

#ifdef GIF_MODE
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_guitarer(XMVECTOR xmLocation, v2_rotation_t vYaw, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
	{
		{
			static constexpr float const REFERENCE_HEIGHT = 5.31f;
			static constexpr milliseconds JUMP_TIME = milliseconds(600);
			static constexpr float INV_JUMP_TIME = 1.0f / fp_seconds(JUMP_TIME).count();

			static fp_seconds accumulator{};

			if ((accumulator += tDelta) >= fp_seconds(JUMP_TIME)) {

				accumulator -= fp_seconds(JUMP_TIME);
			}

			float const height = SFM::triangle_wave(REFERENCE_HEIGHT - 1.0f, REFERENCE_HEIGHT + 1.0f, accumulator.count() * INV_JUMP_TIME);

			xmLocation = XMVectorSetY(xmLocation, height);
		}
		{
			static constexpr float const REFERENCE_ANGLE = -1.3333f;
			static constexpr milliseconds SWING_TIME = milliseconds(3600);
			static constexpr float INV_SWING_TIME = 1.0f / fp_seconds(SWING_TIME).count();

			static fp_seconds accumulator{};

			if ((accumulator += tDelta) >= fp_seconds(SWING_TIME)) {

				accumulator -= fp_seconds(SWING_TIME);
			}

			float const swing = SFM::triangle_wave(-XM_PI * 0.5f, XM_PI * 0.5f, accumulator.count() * INV_SWING_TIME);

			vYaw = v2_rotation_t(REFERENCE_ANGLE + swing);
		}
		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vYaw));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_singer(XMVECTOR xmLocation, v2_rotation_t vYaw, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
	{
		{
			static constexpr float const REFERENCE_HEIGHT = 5.31f;
			static constexpr milliseconds JUMP_TIME = milliseconds(600);
			static constexpr float INV_JUMP_TIME = 1.0f / fp_seconds(JUMP_TIME).count();

			static fp_seconds accumulator{};

			if ((accumulator += tDelta) >= fp_seconds(JUMP_TIME)) {

				accumulator -= fp_seconds(JUMP_TIME);
			}

			float const height = SFM::triangle_wave(REFERENCE_HEIGHT, REFERENCE_HEIGHT + 2.0f, accumulator.count() * INV_JUMP_TIME);

			xmLocation = XMVectorSetY(xmLocation, height);
		}
		{
			static constexpr float const REFERENCE_X = 1.0f;
			static constexpr milliseconds MOVE_TIME = milliseconds(1200);
			static constexpr float INV_MOVE_TIME = 1.0f / fp_seconds(MOVE_TIME).count();

			static fp_seconds accumulator{};

			if ((accumulator += tDelta) >= fp_seconds(MOVE_TIME)) {

				accumulator -= fp_seconds(MOVE_TIME);
			}

			float const x = SFM::triangle_wave(REFERENCE_X, REFERENCE_X + 6.0f, accumulator.count() * INV_MOVE_TIME);

			xmLocation = XMVectorSetX(xmLocation, x);
		}
		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vYaw));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_keyboarder(XMVECTOR xmLocation, v2_rotation_t vYaw, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
	{
		static constexpr float const REFERENCE_HEIGHT = 5.31f;
		static constexpr milliseconds JUMP_TIME = milliseconds(300);
		static constexpr float INV_JUMP_TIME = 1.0f / fp_seconds(JUMP_TIME).count();

		static fp_seconds accumulator{};

		if ((accumulator += tDelta) >= fp_seconds(JUMP_TIME)) {

			accumulator -= fp_seconds(JUMP_TIME);
		}

		float const height = SFM::triangle_wave(REFERENCE_HEIGHT - 1.0f, REFERENCE_HEIGHT + 1.0f, accumulator.count() * INV_JUMP_TIME);

		xmLocation = XMVectorSetY(xmLocation, height);

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vYaw));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_drummer(XMVECTOR xmLocation, v2_rotation_t vYaw, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
	{
		static constexpr float const REFERENCE_HEIGHT = 9.31f;
		static constexpr milliseconds JUMP_TIME = milliseconds(500);
		static constexpr float INV_JUMP_TIME = 1.0f / fp_seconds(JUMP_TIME).count();

		static fp_seconds accumulator{};

		if ((accumulator += tDelta) >= fp_seconds(JUMP_TIME)) {

			accumulator -= fp_seconds(JUMP_TIME);
		}
		
		float const height = SFM::triangle_wave(REFERENCE_HEIGHT - 1.0f, REFERENCE_HEIGHT + 1.0f, accumulator.count() * INV_JUMP_TIME);

		xmLocation = XMVectorSetY(xmLocation, height);

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vYaw));
	}
	
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_crowd(XMVECTOR xmLocation, v2_rotation_t vYaw, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
	{
		static constexpr float const REFERENCE_HEIGHT = 0.5f;
		static constexpr milliseconds JUMP_TIME = milliseconds(750);
		static constexpr float INV_JUMP_TIME = 1.0f / fp_seconds(JUMP_TIME).count();

		using map = std::unordered_map<uint32_t, fp_seconds>;
		static map member;

		HashSetSeed((int32_t)hash);

		if (member.end() == member.find(hash)) {
			// initialize unique timing offset
			member[hash] = PsuedoRandomFloat() * fp_seconds(JUMP_TIME);
		}

		fp_seconds& accumulator(member[hash]);

		if ((accumulator += tDelta) >= fp_seconds(JUMP_TIME)) {

			accumulator -= fp_seconds(JUMP_TIME);
		}

		float const variance = PsuedoRandomFloat() + 1.0f;
		float const height = SFM::triangle_wave(REFERENCE_HEIGHT - variance, REFERENCE_HEIGHT + variance, accumulator.count() * INV_JUMP_TIME);

		xmLocation = XMVectorSetY(xmLocation, height);

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vYaw));
	}
#endif
	void cVoxelWorld::Clear()
	{
		// clear all voxel model instance containers
		_queueWatchedInstances.clear();
		_queueCleanUpInstances.clear();
		_hshVoxelModelRootIndex.clear();
		_hshVoxelModelInstances_Static.clear();
		_hshVoxelModelInstances_Dynamic.clear();

		// clear *all* game object colonies
		world::access::release_game_objects();
		
		// reset world / camera *bugfix important
		resetCamera();
	}

	void cVoxelWorld::GarbageCollect(bool const bForce)
	{
		_streamingGrid.GarbageCollect(critical_now(), critical_delta(), bForce);
	}

	void cVoxelWorld::NewWorld()
	{
		Clear();
		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(10));
		 
		GenerateGround(); // generate new ground
		
		// must be last
		_onLoadedRequired = true; // trigger onloaded() inside Update of VoxelWorld
	}

	void cVoxelWorld::ResetWorld()
	{
		Clear();
		_onLoadedRequired = true;
	}

	void cVoxelWorld::OnLoaded(tTime const& __restrict tNow)
	{
		oCamera.reset();
		MinCity::UserInterface->OnLoaded();

		//placeUpdateableInstanceAt<cCharacterGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(getHoveredVoxelIndex(),
		//	Volumetric::eVoxelModel::DYNAMIC::NAMED::ALIEN_GRAY, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		/*
		static constexpr int32_t const SIZE = 512,
			                           OFFSET = SIZE >> 2;

		bool odd(false);
		for (int32_t offsetz = -SIZE; offsetz <= SIZE; offsetz += OFFSET)
		{
			for (int32_t offsetx = -SIZE; offsetx <= SIZE; offsetx += OFFSET)
			{
				//if (!odd) {
					placeUpdateableInstanceAt<cCharacterGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(getHoveredVoxelIndex(), point2D_t(offsetx, offsetz)),
						Volumetric::eVoxelModel::DYNAMIC::NAMED::ALIEN_GRAY, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
				//}

				odd = !odd;
			}
		}
		*/

#ifdef GIF_MODE

#define STAGE
//#define BALL
		point2D_t const center(MinCity::VoxelWorld->getHoveredVoxelIndex());
		using flags = Volumetric::eVoxelModelInstanceFlags;

#ifdef STAGE
		
		rotateCamera(-v2_rotation_constants::v45.angle());
		
		point2D_t start(p2D_add(center, point2D_t(32, 0)));

		{ // screen
			world::cVideoScreenGameObject* pGameObject;

			pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cVideoScreenGameObject, Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>(start,
				6,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);

			pGameObject->setSequence(58);
		}

		{ // stage
			world::cRockStageGameObject* pGameObject;

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRockStageGameObject, Volumetric::eVoxelModels_Static::NAMED>(p2D_add(start, point2D_t(-34, 0)),
				Volumetric::eVoxelModels_Indices::ROCK_STAGE,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
		}

		{ // band
			world::cRemoteUpdateGameObject* pGameObject;

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::GUITAR,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_guitarer);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(7.0f, 5.31f, 8.0f, 0.0f));
				pGameObject->getModelInstance()->setYaw(v2_rotation_t(-1.3333f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::SINGER,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_singer);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(1.0f, 5.31f, 3.0f, 0.0f));
				pGameObject->getModelInstance()->setYaw(v2_rotation_t(-1.4666f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::MUSICIAN,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_keyboarder);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(7.0f, 5.31f, -7.0f, 0.0f));
				pGameObject->getModelInstance()->setYaw(v2_rotation_t(-1.5999f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::MUSICIAN,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_drummer);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(26.0f, 9.316f, 0.0f, 0.0f));
				pGameObject->getModelInstance()->setYaw(v2_rotation_t(-1.6666f));
			}
		}

		{ // crowd
			static constexpr int32_t const
				CROWD_WIDTH = 32,
				CROWD_HEIGHT = 16;

			

			point2D_t const startCrowd(-10, 40);

			for (int32_t y = 0; y < CROWD_HEIGHT; ++y) {

				for (int32_t x = 0; x < CROWD_WIDTH; ++x) {

					world::cRemoteUpdateGameObject* pGameObject;

					pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_sub(startCrowd, point2D_t(x * 3, y * 5 + (int32_t)PsuedoRandomNumber32(-1, 1))),
						Volumetric::eVoxelModels_Indices::CROWD,
						flags::GROUND_CONDITIONING);
					if (pGameObject) {
						pGameObject->setUpdateFunction(&do_update_crowd);
						//pGameObject->getModelInstance()->setLocation3D(XMVectorSet(26.0f, 9.316f, 0.0f, 0.0f));
						pGameObject->getModelInstance()->setYaw(v2_rotation_t(1.5999f));
					}

				}
			}
		}

		// big lights
		cTestGameObject* pGameObj;

		static constexpr int32_t const offset(48);

		for (float i = 1.0f; i < 128.0f; i += 33.0f)
		{
			pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), p2D_add(center, point2D_t(offset, offset))),
				Volumetric::eVoxelModels_Indices::GIF_LIGHT);
			pGameObj->getModelInstance()->setElevation(i);

			pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), p2D_add(center, point2D_t(offset, -offset))),
				Volumetric::eVoxelModels_Indices::GIF_LIGHT);
			pGameObj->getModelInstance()->setElevation(i);

			pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), p2D_add(center, point2D_t(-(offset >> 4), offset + 10))),
				Volumetric::eVoxelModels_Indices::GIF_LIGHT);
			pGameObj->getModelInstance()->setElevation(i);

			pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), p2D_add(center, point2D_t(-(offset >> 4), -(offset + 10)))),
				Volumetric::eVoxelModels_Indices::GIF_LIGHT);
			pGameObj->getModelInstance()->setElevation(i);
		}
#endif

#ifdef BALL
		static constexpr float const NUM_LIGHTS = 128.0f,
									 SPREAD = 128.0f * Iso::VOX_SIZE;

		rotateCamera(-v2_rotation_constants::v45.angle());

		cTestGameObject* pGameObj(nullptr);

		XMVECTOR const xmHover(XMVectorSwizzle< XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_X >(p2D_to_v2(center)));		// x_0_z
		XMVECTOR const xmVisibleCenter(XMVectorSwizzle< XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_X >(p2D_to_v2(getVisibleGridCenter())));

		for (float i = 0.0f; i < NUM_LIGHTS; i += 1.0f)
		{
			XMVECTOR xmGen(SFM::golden_sphere_coord(i, NUM_LIGHTS)); // x_y_z
			xmGen = XMVectorScale(xmGen, SPREAD);

			XMVECTOR const xmLocation(XMVectorAdd(xmVisibleCenter, XMVectorAdd(xmHover, xmGen)));

			// back to x_z_0
			point2D_t const location = v2_to_p2D(XMVectorSwizzle< XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_X >(xmLocation));

			if ( location.y < 0) {
				pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(location,
					Volumetric::eVoxelModels_Indices::GIF_LIGHT);

				if (pGameObj) {
					// y
					pGameObj->getModelInstance()->setElevation(XMVectorGetY(xmLocation));
				}
			}
		}
#endif

#else
		

#ifdef DEBUG_DEPTH_CUBE

		placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(getVisibleGridCenter(),
						Volumetric::eVoxelModels_Indices::DEPTH_CUBE,
						Volumetric::eVoxelModelInstanceFlags::INSTANT_CREATION);
#endif
		{ // test dynamics 
			//cTestGameObject* pTstGameObj(nullptr);
			//cExplosionGameObject* pExplosionGameObj(nullptr);
			//cLevelSetGameObject* pSphereGameObj(nullptr);

			//pSphereGameObj = placeProceduralInstanceAt<cLevelSetGameObject, true>(p2D_add(getVisibleGridCenter(), point2D_t(10, 10)));

			//pTstGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(getVisibleGridCenter(),
				//Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
			
			/*
			if (PsuedoRandom5050()) {
				pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), point2D_t(10, 10)),
					Volumetric::eVoxelModels_Indices::HOLOGRAM_GIRL);
				pGameObj->getModelInstance()->setTransparency(Volumetric::eVoxelTransparency::ALPHA_25);
			}
			else {
				if (PsuedoRandom5050()) {
					pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), point2D_t(10, 10)),
						Volumetric::eVoxelModels_Indices::HOLOGRAM_GIRL2);
					pGameObj->getModelInstance()->setTransparency(Volumetric::eVoxelTransparency::ALPHA_25);
				}
				else {
			*/
				//pTstGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), point2D_t(25, 25)),
				//			Volumetric::eVoxelModels_Indices::VOODOO_SKULL);
				//pTstGameObj->getModelInstance()->setTransparency(Volumetric::eVoxelTransparency::ALPHA_25);
			/*	}
			}*/
		}


		/*{ // copter dynamic
			cCopterGameObject* pGameObj = placeCopterInstanceAt(p2D_add(getVisibleGridCenter(), point2D_t(-25, -25)));
			if (pGameObj) {
				FMT_LOG_OK(GAME_LOG, "Copter launched");
			}
			else {
				FMT_LOG_FAIL(GAME_LOG, "No Copter for you - nullptr");
			}
		}*/
#endif

		_onLoadedRequired = false; // reset (must be last)
	}

	// *** no simultaneous !writes! to grid or grid data can occur while calling this function ***
	// simultaneous reads (read-only) is OK.
	// RenderGrid is protected only for read-only access
	// GridSnapshot is protected for read-only access                                                             // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	auto cVoxelWorld::GridSnapshot() const -> std::pair<Iso::Voxel const* const __restrict, uint32_t const> const // !broken! - when required again need @TODO compatibility w/ StreamingGrid. Grid is large enough now that it does not all fit in memory at once, StreamingGrid fixes that.
	{                                                                                                             // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		/*static constexpr uint32_t const voxel_count(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
		static constexpr size_t const gridSz(sizeof(Iso::Voxel) * size_t(voxel_count));

		Iso::Voxel* __restrict gridSnapshot = (Iso::Voxel * __restrict)scalable_malloc(gridSz);

		{
			tbb::queuing_rw_mutex::scoped_lock lock(_streamingGrid.access_lock(), false); // read-only access
			////////////////////////////memcpy(gridSnapshot, the::grid, gridSz);
		}

		return(std::make_pair( gridSnapshot, voxel_count ));*/
		return {};
	}

	// *** no simultaneous !writes! to grid or grid data can occur while calling this function ***
	// simultaneous reads (read-only) is NOT OK - unless protected
	// RenderGrid is protected only for read-only access
	// GridSnapshotLoad is protected for write access                               // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	void cVoxelWorld::GridSnapshotLoad(Iso::Voxel const* const __restrict new_grid) // !broken! - when required again need @TODO compatibility w/ StreamingGrid. Grid is large enough now that it does not all fit in memory at once, StreamingGrid fixes that.
	{                                                                               // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		/*static constexpr uint32_t const voxel_count(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
		static constexpr size_t const gridSz(sizeof(Iso::Voxel) * size_t(voxel_count));

		// internally memory for the grid uses VirtualAlloc2 / VirtualFree for it's data
		Iso::Voxel* const __restrict theGrid = (Iso::Voxel* const __restrict)scalable_aligned_malloc(gridSz, CACHE_LINE_BYTES);

		memcpy(theGrid, new_grid, gridSz);

		// the sizes are the same for the new grid, and existing grid so we could just memcpy 
		// however to ensure atomicity we would rather do a interlocked pointer swap, making this routine threadsafe
		Iso::Voxel* __restrict oldGrid(nullptr);
		{
			// the data inside of the pointer (memory allocated, voxels of the grid) needs to be protected by a mutex
			// to be thread safe. This lock has very little contention put on it from this side, however if the lock is already 
			// obtained by RenderGrid the wait this function will have is high (time for RenderGrid to complete)

			tbb::queuing_rw_mutex::scoped_lock lock(_streamingGrid.access_lock(), true); // write access
			///////////////////////////////oldGrid = (Iso::Voxel*)_InterlockedExchangePointer((PVOID * __restrict)&the::grid._protected, theGrid);
		}
		// free the old grid's large allocation
		if (oldGrid) {
			scalable_aligned_free(oldGrid);
		}*/
	}

} // end ns world

namespace world
{
#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
	size_t const cVoxelWorld::numDynamicVoxelsRendered() const
	{
		return(voxelRender::render_state.numDynamicVoxelsRendered);
	}
	size_t const cVoxelWorld::numStaticVoxelsRendered() const
	{
		return(voxelRender::render_state.numStaticVoxelsRendered);
	}
	size_t const cVoxelWorld::numTerrainVoxelsRendered() const
	{
		return(voxelRender::render_state.numTerrainVoxelsRendered);
	}
	size_t const cVoxelWorld::numLightVoxelsRendered() const
	{
		return(voxelRender::render_state.numLightVoxelsRendered);
	}
#endif
#endif

	rect2D_t const __vectorcall cVoxelWorld::getVisibleGridBounds() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		point2D_t const gridBounds(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z);

		rect2D_t const area(r2D_set_by_width_height(point2D_t{}, gridBounds));
		// this specifically does not clamp - to not modify the width/height of the visible rect
		// ie.) randomVoxel functions depend on this
		return(r2D_add(area, p2D_sub(oCamera.voxelIndex_Center, p2D_half(gridBounds))));
	}
	rect2D_t const __vectorcall cVoxelWorld::getVisibleGridBoundsClamped() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		// clamp to world grid
		return(r2D_clamp(getVisibleGridBounds(), point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V)));
	}
	point2D_t const __vectorcall cVoxelWorld::getVisibleGridCenter() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		return(oCamera.voxelIndex_Center);
	}
	v2_rotation_t const& cVoxelWorld::getYaw() const
	{
		return(oCamera.Yaw);
	}
	float const	cVoxelWorld::getZoomFactor() const
	{
		return(oCamera.ZoomFactor);
	}

	void cVoxelWorld::createAllBuffers(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue)
	{
		// ##### allocation totals for gpu buffers of *minigrid* voxels are always halved. requirement/assumption is given
		// a linear access pattern (rendering) - only half of the total volume is occupied ####
		// main vertex buffer for static voxels - **** buffer allocation sqrt'd and doubled
		tbb::cache_aligned_allocator< VertexDecl::VoxelDynamic > allocator_dynamic;
		tbb::cache_aligned_allocator< VertexDecl::VoxelNormal > allocator_normal;

		// these are cpu side "staging" buffers, matching in size to the gpu buffers that are allocated for them in cVulkan
		// *** these staging buffers are dynamic, the active size is reset to zero for a reason here.

		// dynamic - opaque
		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {

			voxels.visibleDynamic.opaque.buffer.staging[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleDynamic.opaque.buffer.type), vku::eMappedAccess::Sequential, true, true);
			voxels.visibleDynamic.opaque.buffer.staging[i].setActiveSizeBytes(0);
		}
		voxels.visibleDynamic.opaque.buffer.direct = allocator_dynamic.allocate(Volumetric::dynamic_direct_buffer_size);
		memset(voxels.visibleDynamic.opaque.buffer.direct, 0, Volumetric::dynamic_direct_buffer_size * sizeof(voxels.visibleDynamic.opaque.buffer.type));
		voxels.visibleDynamic.opaque.bits = bit_row_atomic<Volumetric::dynamic_direct_buffer_size>::create();

		// dynamic - transparents
		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {

			voxels.visibleDynamic.trans.buffer.staging[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleDynamic.trans.buffer.type), vku::eMappedAccess::Sequential, true, true);
			voxels.visibleDynamic.trans.buffer.staging[i].setActiveSizeBytes(0);
		}
		voxels.visibleDynamic.trans.buffer.direct = allocator_dynamic.allocate(Volumetric::dynamic_direct_buffer_size);
		memset(voxels.visibleDynamic.trans.buffer.direct, 0, Volumetric::dynamic_direct_buffer_size * sizeof(voxels.visibleDynamic.trans.buffer.type));
		voxels.visibleDynamic.trans.bits = bit_row_atomic<Volumetric::dynamic_direct_buffer_size>::create();

		// static
		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {

			voxels.visibleStatic.buffer.staging[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleStatic.buffer.type), vku::eMappedAccess::Sequential, true, true);
			voxels.visibleStatic.buffer.staging[i].setActiveSizeBytes(0);
		}
		voxels.visibleStatic.buffer.direct = allocator_normal.allocate(Volumetric::static_direct_buffer_size);
		memset(voxels.visibleStatic.buffer.direct, 0, Volumetric::static_direct_buffer_size * sizeof(voxels.visibleStatic.buffer.type));
		voxels.visibleStatic.bits = bit_row_atomic<Volumetric::static_direct_buffer_size>::create();

		// terrain
		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {

			voxels.visibleTerrain.buffer.staging[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(voxels.visibleTerrain.buffer.type), vku::eMappedAccess::Sequential, true, true);
			voxels.visibleTerrain.buffer.staging[i].setActiveSizeBytes(0);
		}
		voxels.visibleTerrain.buffer.direct = allocator_normal.allocate(Volumetric::terrain_direct_buffer_size);
		memset(voxels.visibleTerrain.buffer.direct, 0, Volumetric::terrain_direct_buffer_size * sizeof(voxels.visibleTerrain.buffer.type));
		voxels.visibleTerrain.bits = bit_row_atomic<Volumetric::terrain_direct_buffer_size>::create();
		

		// shared buffer and other buffers
		{
			using buf = vk::BufferUsageFlagBits;

			point2D_t const frameBufferSize(MinCity::getFramebufferSize());
			size_t const buffer_size(frameBufferSize.x * frameBufferSize.y * sizeof(uint8_t));

			_buffers.reset_subgroup_layer_count_max.createAsGPUBuffer(device, commandPool, queue, buffer_size, buf::eTransferSrc); // reset buffer contains all zeroes on creation  (gpu-local zero copy)

			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
				_buffers.subgroup_layer_count_max[resource_index] = vku::StorageBuffer(buffer_size, false, vk::BufferUsageFlagBits::eTransferDst);
				VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)_buffers.subgroup_layer_count_max[resource_index].buffer(), vkNames::Buffer::SUBGROUP_LAYER_COUNT);
			}


			_buffers.reset_shared_buffer.createAsGPUBuffer(device, commandPool, queue, sizeof(BufferDecl::VoxelSharedBuffer), buf::eTransferSrc); // reset buffer contains all zeroes on creation  (gpu-local zero copy)

			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
				_buffers.shared_buffer[resource_index] = vku::StorageBuffer(sizeof(BufferDecl::VoxelSharedBuffer), false, vk::BufferUsageFlagBits::eTransferDst);
				VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)_buffers.shared_buffer[resource_index].buffer(), vkNames::Buffer::SHARED);
			}

		}
	}
	void cVoxelWorld::OutputVoxelStats() const
	{	/* broken
		//format("{:*^30}", "centered");  // use '*' as a fill char
		fmt::print(fg(fmt::color::yellow), "\n{:#<32}", "|Metric| ");
		fmt::print(fg(fmt::color::yellow), "{:#>16}\n", " |Value|");

		uint32_t uiCnt(0);
		fmt::color valueColor;
		for (auto const& stat : Volumetric::Allocation::_values()) {
			if (uiCnt++ & 1) {
				valueColor = fmt::color::yellow;
			}
			else {
				valueColor = fmt::color::lime_green;
			}
			fmt::print(fg(fmt::color::white), "\n{:_<32}", stat._to_string());
			fmt::print(fg(valueColor), "{:_>16}\n", stat._to_integral());
		}
		fmt::print(fg(fmt::color::yellow), "\n{:#^48}\n", " ");
		*/
	}

#ifdef DEBUG_VOXEL_BANDWIDTH
	static NO_INLINE size_t const frameBandwidth(tTime const& tNow, size_t const frame_voxel_count) // real-time domain
	{
		static constexpr milliseconds const PRINT_FRAME_INTERVAL = milliseconds(4000); // ms
		static constexpr uint32_t const OVERLAP_WEIGHT_CURRENT = 6, // frames
			                            OVERLAP_WEIGHT_LAST = OVERLAP_WEIGHT_CURRENT >> 2; // frames

		constinit static microseconds tSum{}, tLastAverage{};
		constinit static size_t sum{}, peak{}, aboveaveragelevel{}, lastaverage{};

		constinit static size_t framecount(0), aboveaveragecount(0), belowaveragecount(0);

		static auto start = high_resolution_clock::now();
		static auto lastPrint = high_resolution_clock::now();

		static_assert(sizeof(VertexDecl::VoxelNormal) == sizeof(VertexDecl::VoxelDynamic), "VoxelNormal != VoxelDynamic declaration in size, voxel bandwidth is incorrect.");
		static constexpr size_t const voxelSize = sizeof(VertexDecl::VoxelNormal);

		auto const deltaNano = tNow - start;
		start = tNow;
		tSum += duration_cast<microseconds>(deltaNano);

		size_t const deltaVoxel = frame_voxel_count;

		peak = std::max(peak, deltaVoxel);
		sum += deltaVoxel;
		if (deltaVoxel > aboveaveragelevel)
			++aboveaveragecount;
		else if (deltaVoxel < lastaverage)
			++belowaveragecount;

		++framecount;

		microseconds const tAvgDelta = tSum / framecount;
		size_t const avgdelta = sum / framecount;

		if (tNow - lastPrint > PRINT_FRAME_INTERVAL) {
			lastPrint = tNow;

			size_t const frame_avg_memory = avgdelta * voxelSize;
			double const frame_avg_bandwidth = (double)frame_avg_memory / duration_cast<fp_seconds>(tAvgDelta).count();  // in  bytes/s
			double const frame_avg_bw = ((double)frame_avg_bandwidth) / double(1 << 20); // in MB/s

			size_t const frame_peak_memory = peak * voxelSize;
			double const frame_peak_bandwidth = (double)frame_peak_memory / duration_cast<fp_seconds>(tAvgDelta).count();  // in  bytes/s
			double const frame_peak_bw = ((double)frame_peak_bandwidth) / double(1 << 20); // in MB/s

			fmt::print(fg(fmt::color::dodger_blue), "\n" "[ {:.1f} MB/s, {:.1f} MB/s peak ]\n", frame_avg_bw, frame_peak_bw);
			
			if (aboveaveragecount && belowaveragecount) {

				size_t const belowaverage = (belowaveragecount * 100) / framecount;   // percentage of frames below average
				size_t const aboveaverage = (aboveaveragecount * 100) / framecount;   //  ""    ""  ""  "" "" avove ""   ""

				fmt::print(fg(fmt::color::white), "[ ");

				if (belowaverage < 24) {
					fmt::print(fg(fmt::color::red), "-{:d}% ", belowaverage);
				}
				else if (belowaverage < 48) {
					fmt::print(fg(fmt::color::yellow), "-{:d}% ", belowaverage);
				}
				else {
					fmt::print(fg(fmt::color::lime_green), "-{:d}% ", belowaverage);
				}
				if (aboveaverage < 24) {
					fmt::print(fg(fmt::color::lime_green), " +{:d}% ", aboveaverage);
				}
				else if (aboveaverage < 48) {
					fmt::print(fg(fmt::color::yellow), " +{:d}% ", aboveaverage);
				}
				else {
					fmt::print(fg(fmt::color::red), " +{:d}% ", aboveaverage);
				}

				fmt::print(fg(fmt::color::white), " ]\n");
			}

			aboveaveragelevel = (avgdelta + peak) >> 1;                          // bugfix - there would be a large spike in framerate every print interval due to framecount being set to zero while the next starting sum contained the last average.
			peak = 0; 
			
			tSum = tAvgDelta * OVERLAP_WEIGHT_CURRENT + tLastAverage * OVERLAP_WEIGHT_LAST;
			sum = avgdelta * OVERLAP_WEIGHT_CURRENT + lastaverage * OVERLAP_WEIGHT_LAST; 

			framecount = OVERLAP_WEIGHT_CURRENT + OVERLAP_WEIGHT_LAST; 
			aboveaveragecount = 0; belowaveragecount = 0;
			lastaverage = avgdelta;
			tLastAverage = tAvgDelta;
		}

		return(avgdelta);
	}
#endif

	template<typename VertexDeclaration, size_t const direct_buffer_size>
	__declspec(safebuffers) STATIC_INLINE_PURE size_t const StreamCompaction(VertexDeclaration* __restrict out, VertexDeclaration const* const __restrict in, size_t const max_count, bit_row_atomic<direct_buffer_size> const* const __restrict bits)
	{
		using streaming_batch = sBatchedByIndexIn<VertexDeclaration, eStreamingBatchSize::MODEL>;
		static constexpr size_t const block_count(bit_row_atomic<direct_buffer_size>::stride());
		
		streaming_batch local;
		auto const* const __restrict stream_bits(bits->data());
		size_t active_count(0);

		// iterate thru each chunk of 64 bits
		for (size_t block = 0; block < block_count; ++block) {

			// current block
			size_t block_bits = stream_bits[block];

			// integral component for "stream compaction"
			// public domain - Daniel Lemire's blog
			// algorithm: https://github.com/lemire/Code-used-on-Daniel-Lemire-s-blog/blob/master/2018/02/20/bitmapdecode.c
	
			// ** this iterates only on the set bits, zero-skipping upto 64 bits / iteration
			while (0 != block_bits) {

				// bitstream memory layout:
				// [           0             ] [            1            ] [            2            ]              [            n            ]  : block
				// [64                      0] [64                      0] [64                      0]     ....     [64                      0]  : bits
				//   1111 1111 1111 .... 1111    1111 1111 1111 .... 1111    1111 1111 1111 .... 1111                 1111 1111 1111 .... 1111   
				//   ^                           ^                           ^                                        ^
				//   MSB  
				//   0111 1111 1111 .... 1111 
				//    ^
				//    MSB   
				//   0011 1111 1111 .... 1111
				//     ^
				//     MSB
				//            ....
				// 
				//   0000 0000 0000 .... 0001     -and then iterate to next block....
				//                          ^
				//                          MSB
				//
				// get index of highest bit set	(MSB)
				unsigned long r;
				(void)_BitScanForward64(&r, block_bits); // block_bits is never zero here, so return value does not need to be checked

				// clear that bit for this block
				block_bits &= ~(1ui64 << ((uint64_t)r));

				local.emplace_back(out, in, r + block * 64); // out is sequentially streamed to, while in is accessed at index r of the current block plus the count of all bits before this bit that have been processed as a block 
				
				if (++active_count >= max_count) {
					block = block_count; // found all possible voxels, early exit (skipping zeroes at the end of the bitstream)
					break;
				}
			}
		}

		local.out(out, in); // residual

		return(active_count);
	}

	void cVoxelWorld::RenderTask_Normal(uint32_t const resource_index) const // OPACITY is enabled for update, and reources are mapped during normal rendering
	{
		ZoneScopedN("Stage Resources");

		// mapping direct buffers //
		VertexDecl::VoxelNormal const* const MappedVoxels_Terrain_Start(voxels.visibleTerrain.buffer.direct);
		std::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Terrain(voxels.visibleTerrain.buffer.direct);

		VertexDecl::VoxelNormal const* const MappedVoxels_Static_Start(voxels.visibleStatic.buffer.direct);
		std::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Static(voxels.visibleStatic.buffer.direct);

		VertexDecl::VoxelDynamic const* const MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::_size()]{
			voxels.visibleDynamic.opaque.buffer.direct,
			voxels.visibleDynamic.trans.buffer.direct,
		};
		std::atomic<VertexDecl::VoxelDynamic*> MappedVoxels_Dynamic[Volumetric::eVoxelType::_size()]{
			voxels.visibleDynamic.opaque.buffer.direct,
			voxels.visibleDynamic.trans.buffer.direct
		};

		// lru streaming grid deadzone
		MinCity::VoxelWorld->GarbageCollect(); // optimal position to exploit wait at begining of RenderTask_Normal, this is the ideal "dead-zone"

		// ensure the async clears are done
		async_long_task::wait<background_critical>(_AsyncClearTaskID, "async clears");
		___streaming_store_fence(); // ensure "streaming" clears are coherent

		_OpacityMap.map(); // (maps, should be done once clear for lights has completed, and before any lights are added)

		// GRID RENDER //
		tbb::affinity_partitioner part{}; // *bugfix - lifetime of partioner should be in this scope

		voxelRender::RenderGrid(
			oCamera.voxelIndex_TopLeft,
			std::forward<Volumetric::voxelBufferReference_Terrain&& __restrict>(Volumetric::voxelBufferReference_Terrain(MappedVoxels_Terrain, MappedVoxels_Terrain_Start, voxels.visibleTerrain.bits)),
			std::forward<Volumetric::voxelBufferReference_Static&& __restrict>(Volumetric::voxelBufferReference_Static(MappedVoxels_Static, MappedVoxels_Static_Start, voxels.visibleStatic.bits)),
			std::forward<Volumetric::voxelBufferReference_Dynamic&& __restrict>(Volumetric::voxelBufferReference_Dynamic(MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque], MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque], voxels.visibleDynamic.opaque.bits)),
			std::forward<Volumetric::voxelBufferReference_Dynamic&& __restrict>(Volumetric::voxelBufferReference_Dynamic(MappedVoxels_Dynamic[Volumetric::eVoxelType::trans], MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans], voxels.visibleDynamic.trans.bits)),
			part
		);

		// game related asynchronous methods
		// physics can be "cleared" as early as here. corresponding wait is in Update() of VoxelWorld.
		MinCity::Physics->AsyncClear();

		_OpacityMap.commit(); // (commits the new bounds, unmaps)

		/// all voxels
#ifdef DEBUG_VOXEL_BANDWIDTH
		size_t voxel_count(0);
#endif
		// mapping staging buffers //

		tbb::parallel_invoke( // [dynamic] [static] [terrain] staging buffers compaction are independent of each other

		[&]{ // dynamic voxels (dynamic partition size and offset updates) //
			vku::VertexBufferPartition* const __restrict& __restrict dynamic_partition_info_updater(MinCity::Vulkan->getDynamicPartitionInfo(resource_index));
			size_t running_offset_size(0);

			{ // dynamic (opaques)
				VertexDecl::VoxelDynamic* const MappedVoxels_Dynamic_End = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
				size_t activeSize = MappedVoxels_Dynamic_End - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque];
				voxels.visibleDynamic.opaque.buffer.active_size = activeSize * sizeof(VertexDecl::VoxelDynamic); // direct buffer size

				VertexDecl::VoxelDynamic* const __restrict Mapped_Staging_Voxels_Dynamic_Start = (VertexDecl::VoxelDynamic* const __restrict)voxels.visibleDynamic.opaque.buffer.staging[resource_index].map();
				//___memcpy_threaded<32>(Mapped_Staging_Voxels_Dynamic_Start, MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque], bytes);
				activeSize = StreamCompaction<VertexDecl::VoxelDynamic, Volumetric::dynamic_direct_buffer_size>(Mapped_Staging_Voxels_Dynamic_Start, MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque],
					                                                                                            activeSize, voxels.visibleDynamic.opaque.bits);
				voxels.visibleDynamic.opaque.buffer.staging[resource_index].unmap();
				voxels.visibleDynamic.opaque.buffer.staging[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelDynamic)); // staging buffer size

				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].active_vertex_count = (uint32_t const)activeSize;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += activeSize;

#ifdef DEBUG_VOXEL_BANDWIDTH
				voxel_count += activeSize;
#endif
			}
			{
				// Update Dynamic VertexBuffer Offsets for "Custom Voxel Shader Children"
				// ########### only use [Volumetric::eVoxelType::opaque] even if the child is a transparent pipeline
				// ########### the [Volumetric::eVoxelType::trans] is reserved for voxels belong to model instances on the grid and is soley for PARENT_TRANS usage

				/* do not delete, required reference for custom voxel fragment shader implementation details
				if (_activeRain) {

					RenderRain(_activeRain, MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque]);
				}
				{ // RAIN
					VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
					size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - running_offset_start;
					// set the parent / main partition info
					dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_RAIN].active_vertex_count = (uint32_t const)current_dynamic_size;
					dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_RAIN].vertex_start_offset = (uint32_t const)running_offset_size;
					running_offset_size += current_dynamic_size;
					running_offset_start = MappedVoxels_Dynamic_Current;

	#ifdef DEBUG_RAIN_COUNT
					if (dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_RAIN].active_vertex_count) {
						FMT_NUKLEAR_DEBUG(false, "      {:d} rain voxels being rendered", dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_RAIN].active_vertex_count);
					}
					else {
						FMT_NUKLEAR_DEBUG_OFF();
					}
	#endif

				}
				*/
				// todo add other children here for added partitions of dynamic voxel parent
			}
			{ // dynamic (transparents) ***MUST BE LAST DYNAMIC***
				VertexDecl::VoxelDynamic* const MappedVoxels_Dynamic_End = MappedVoxels_Dynamic[Volumetric::eVoxelType::trans];
				size_t activeSize = MappedVoxels_Dynamic_End - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans];
				voxels.visibleDynamic.trans.buffer.active_size = activeSize * sizeof(VertexDecl::VoxelDynamic); // direct buffer size

				VertexDecl::VoxelDynamic* const __restrict Mapped_Staging_Voxels_Dynamic_Start = (VertexDecl::VoxelDynamic* const __restrict)voxels.visibleDynamic.trans.buffer.staging[resource_index].map();
				//___memcpy_threaded<32>(Mapped_Staging_Voxels_Dynamic_Start, MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans], bytes);
				activeSize = StreamCompaction<VertexDecl::VoxelDynamic, Volumetric::dynamic_direct_buffer_size>(Mapped_Staging_Voxels_Dynamic_Start, MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans],
					                                                                                            activeSize, voxels.visibleDynamic.trans.bits);
				
				voxels.visibleDynamic.trans.buffer.staging[resource_index].unmap();
				voxels.visibleDynamic.trans.buffer.staging[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelDynamic)); // staging buffer size

				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_TRANS].active_vertex_count = (uint32_t const)activeSize;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_TRANS].vertex_start_offset = (uint32_t const)running_offset_size;  // special handling injecting transparents into opaque / main part
#ifdef DEBUG_VOXEL_BANDWIDTH
				voxel_count += activeSize;
#endif
			}
		},

		[&]{ // static
			VertexDecl::VoxelNormal const* const MappedVoxels_Static_End = MappedVoxels_Static;
			size_t activeSize = MappedVoxels_Static_End - MappedVoxels_Static_Start;
			voxels.visibleStatic.buffer.active_size = activeSize * sizeof(VertexDecl::VoxelNormal); // direct buffer size

			VertexDecl::VoxelNormal* const __restrict Mapped_Staging_Voxels_Static_Start = (VertexDecl::VoxelNormal* const __restrict)voxels.visibleStatic.buffer.staging[resource_index].map();
			//___memcpy_threaded<32>(Mapped_Staging_Voxels_Static_Start, MappedVoxels_Static_Start, activeSize * sizeof(VertexDecl::VoxelNormal));
			activeSize = StreamCompaction<VertexDecl::VoxelNormal, Volumetric::static_direct_buffer_size>(Mapped_Staging_Voxels_Static_Start, MappedVoxels_Static_Start,
				                                                                                          activeSize, voxels.visibleStatic.bits);
			voxels.visibleStatic.buffer.staging[resource_index].unmap();
			voxels.visibleStatic.buffer.staging[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal)); // staging buffer size
			
#ifdef DEBUG_VOXEL_BANDWIDTH
			voxel_count += activeSize;
#endif 
		},

		[&]{ // terrain
			voxels.visibleTerrain.buffer.active_size = Volumetric::terrain_direct_buffer_size * sizeof(VertexDecl::VoxelNormal); // direct buffer size

			VertexDecl::VoxelNormal* const __restrict Mapped_Staging_Voxels_Terrain_Start = (VertexDecl::VoxelNormal* const __restrict)voxels.visibleTerrain.buffer.staging[resource_index].map();
			///___memcpy_threaded<32>(Mapped_Staging_Voxels_Terrain_Start, MappedVoxels_Terrain_Start, bytes);
			size_t const activeSize = StreamCompaction<VertexDecl::VoxelNormal, Volumetric::terrain_direct_buffer_size>(Mapped_Staging_Voxels_Terrain_Start, MappedVoxels_Terrain_Start,
				                                                                                                        Volumetric::terrain_direct_buffer_size, voxels.visibleTerrain.bits);
			voxels.visibleTerrain.buffer.staging[resource_index].unmap();
			voxels.visibleTerrain.buffer.staging[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal)); // staging buffer size
			
#ifdef DEBUG_VOXEL_BANDWIDTH
			voxel_count += activeSize;
#endif
		}
	   ); // end parallel_invoke
#ifdef DEBUG_VOXEL_BANDWIDTH
		frameBandwidth(high_resolution_clock::now(), voxel_count);
#endif
	}

	void cVoxelWorld::RenderTask_Minimap() const // OPACITY is not updated on minimap rendering enabled, so it is removed at compile time and not resources for it are mapped
	{
		/*
		static int32_t minimapChunkCurrentLine = Iso::WORLD_GRID_SIZE - 1;

		vk::Device const& device(MinCity::Vulkan->getDevice());

		VertexDecl::VoxelNormal* const __restrict MappedVoxels_Terrain_Start = (VertexDecl::VoxelNormal* const __restrict)voxels.minimapChunkTerrain.stagingBuffer[0].map(device);
		tbb::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Terrain(MappedVoxels_Terrain_Start);

		VertexDecl::VoxelNormal* const __restrict MappedVoxels_Start = (VertexDecl::VoxelNormal* __restrict)voxels.minimapChunkStatic.stagingBuffer[0].map(device);
		tbb::atomic<VertexDecl::VoxelNormal*> MappedVoxels(MappedVoxels_Start);

		__memclr_aligned_32<true>(MappedVoxels_Terrain_Start, voxels.minimapChunkTerrain.stagingBuffer[0].activesizebytes());
		__memclr_aligned_32<true>(MappedVoxels_Start, voxels.minimapChunkStatic.stagingBuffer[0].activesizebytes());

		__streaming_store_fence(); // ensure clears are coherent

		point2D_t vStart(minimapChunkCurrentLine, 0);
		voxelRender::RenderGrid<Volumetric::Allocation::VOXEL_GRID_MINIMAP_CHUNK_X, Volumetric::Allocation::VOXEL_GRID_MINIMAP_CHUNK_Z, false, false, false>(
			XMVectorSet((float)minimapChunkCurrentLine, 0.0f, 160.0f, 0.0f),
			vStart, MappedVoxels_Terrain, MappedVoxels, MappedVoxels);	// dynamic part missing/not used for minimap

		__streaming_store_fence(); // ensure writes are finished before unmap

		// Update stagingBuffere inside this thread (no two memory regions are overlapping while mapped across threads)
		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_End = MappedVoxels;
			size_t const activeSize = MappedVoxels_End - MappedVoxels_Start;
			voxels.minimapChunkStatic.stagingBuffer[0].unmap(device);

			voxels.minimapChunkStatic.stagingBuffer[0].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}

		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_Terrain_End = MappedVoxels_Terrain;
			size_t const activeSize = MappedVoxels_Terrain_End - MappedVoxels_Terrain_Start;
			voxels.minimapChunkTerrain.stagingBuffer[0].unmap(device);

			voxels.minimapChunkTerrain.stagingBuffer[0].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}

		minimapChunkCurrentLine -= Volumetric::VOXEL_MINIMAP_LINES_PER_CHUNK;
		if (minimapChunkCurrentLine < 0) {
			minimapChunkCurrentLine = Iso::WORLD_GRID_SIZE - 1;
		}
		*/
	}

	void cVoxelWorld::Render(uint32_t const resource_index) const
	{
		RenderTask_Normal(resource_index);
	}
	bool const cVoxelWorld::renderCompute(vku::compute_pass&& __restrict c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data)
	{
		// texture shaders [[deprecated]]
		/*if (c.cb_render_texture) {

			static tTime tLast(now());
			constinit static uint32_t temporal_size(0);
			constinit static bool bComputeRecorded(false);

			tTime const tNow(now());
			fp_seconds const tDelta(tNow - tLast);

			// update memory for push constants
			XMVECTOR const origin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOriginNoFractionalOffset())); // *bugfix: fractional offset is double added by the viewmatrix if included in origin here.
			
			for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {
				
				static constexpr fp_seconds const target_frametime(fp_seconds(critical_delta()) * 4.0);

				uint32_t const has_frames(_textureShader[shader].input->extent().depth);

				if (has_frames) {
					double const total_frames((double)has_frames);
					fp_seconds const loop_time(total_frames * target_frametime);

					fp_seconds accumulator(_textureShader[shader].accumulator);

					float const progress = (float)SFM::lerp(0.0, total_frames - 1.0, accumulator / loop_time);
					_textureShader[shader].push_constants.frame_or_time = progress;

					accumulator += tDelta;
					if (accumulator >= loop_time) {
						accumulator -= loop_time;
					}
					_textureShader[shader].accumulator = accumulator;
				}
				else {
					_textureShader[shader].accumulator += tDelta;
					_textureShader[shader].push_constants.frame_or_time = time_to_float(_textureShader[shader].accumulator); // defaults to time elapsed since synchronized start timestamp of texture shaders
				}

				XMStoreFloat2(&_textureShader[shader].push_constants.origin, origin);
			}

			tLast = tNow;

			// only record once
			if (!bComputeRecorded) {

				vk::CommandBufferBeginInfo bi{};

				c.cb_render_texture.begin(bi); VKU_SET_CMD_BUFFER_LABEL(c.cb_render_texture, vkNames::CommandBuffer::COMPUTE_TEXTURE);

				// batch barriers all together
				for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {
					_textureShader[shader].output->setLayoutCompute<true>(c.cb_render_texture, vku::ACCESS_WRITEONLY);
				}

				// run texture shaders (compute)
				for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {

					c.cb_render_texture.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.texture.pipelineLayout, 0, render_data.texture.sets[shader][0], nullptr);
					c.cb_render_texture.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.texture.pipeline[shader]);

					c.cb_render_texture.pushConstants(*render_data.texture.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
						(uint32_t)0U,
						(uint32_t)sizeof(UniformDecl::TextureShaderPushConstants), reinterpret_cast<void const* const>(&_textureShader[shader].push_constants));

					c.cb_render_texture.dispatchIndirect(_textureShader[shader].indirect_buffer->buffer(), 0);

					_textureShader[shader].output->setLayoutCompute<false>(c.cb_render_texture, vku::ACCESS_READONLY); // so that successive texture shaders (multipass) can use outputs as an input.
				}

				c.cb_render_texture.end();
				
				// for next compute iteration
				if (++temporal_size > 2) {
					// bug - memory referenced for push constants not updating automatically like it should ? 
					// bComputeRecorded = true;
				}
			}
			else {

				// the compute buffer does not need to be recorded, reuse
				// this updates the layout for the output image state after the pre-recorded compute cb is done
				for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {
					_textureShader[shader].output->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
				}
			}
		}
		*/
		return(_OpacityMap.renderCompute(std::forward<vku::compute_pass&& __restrict>(c), render_data));
	}

	void cVoxelWorld::Transfer(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, vku::UniformBuffer& __restrict ubo)
	{
		constinit static vku::double_buffer<UniformDecl::VoxelSharedUniform> current_state{};
		
		// make a copy to static memory, to ensure memory location and data is current for only this frame (this takes a snapshot of the current state and keeps it unique for double buffering)
		current_state[resource_index] = _currentState.Uniform;
		
		// ######################### STAGE 1 - UBO UPDATE (that all subsequent renderpasses require //
		cb.updateBuffer(
			ubo.buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform), (const void*)&current_state[resource_index]
		);

		// *Required* - solves a bug with trailing voxels, Vulkan->cpp has the corresponding "acquire" operation in static command buffer operation
		   // see https://www.khronos.org/registry/vulkan/specs/1.0/html/chap6.html#synchronization-memory-barriers under BufferMemoryBarriers
		ubo.barrier( // ## RELEASE ## //
			cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
			vk::DependencyFlagBits::eByRegion,
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
		);
	}

	void cVoxelWorld::Transfer(uint32_t const resource_index, vk::CommandBuffer& __restrict cb,
		                       vku::DynamicVertexBuffer* const __restrict (&__restrict vbo)[eVoxelVertexBuffer::_size_constant])
	{		
		vk::CommandBufferBeginInfo bi(vk::CommandBufferUsageFlagBits::eOneTimeSubmit); // updated every frame
		cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::DYNAMIC);

		{ // ######################### STAGE 1 - OTHER BUFFERS (that the static renderpass requires)
			{
				_buffers.shared_buffer[resource_index].uploadDeferred(cb, _buffers.reset_shared_buffer);
				_buffers.subgroup_layer_count_max[resource_index].uploadDeferred(cb, _buffers.reset_subgroup_layer_count_max);
			}
		}

		{ // ######################### STAGE 2 - VBO SUBMIT //

			vbo[eVoxelVertexBuffer::VOXEL_TERRAIN]->uploadDeferred(cb, voxels.visibleTerrain.buffer.staging[resource_index]);
			vbo[eVoxelVertexBuffer::VOXEL_STATIC]->uploadDeferred(cb, voxels.visibleStatic.buffer.staging[resource_index]);
			vbo[eVoxelVertexBuffer::VOXEL_DYNAMIC]->uploadDeferred(cb, voxels.visibleDynamic.opaque.buffer.staging[resource_index], voxels.visibleDynamic.trans.buffer.staging[resource_index]);
		}

		{ // ######################### STAGE 3 - BUFFER BARRIERS //		
			
			{ // ## RELEASE ## //
				static constexpr size_t const buffer_count(2ULL);
				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &_buffers.shared_buffer[resource_index], &_buffers.subgroup_layer_count_max[resource_index] };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,  // first usage is in z only pass in voxel_clear.frag
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
				);
			}

			{ // ## RELEASE ## //
				// *Required* - solves a bug with flickering geometry, Vulkan->cpp has the corresponding "acquire" operation in static command buffer operation
				static constexpr size_t const buffer_count(3ULL);
 				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ vbo[eVoxelVertexBuffer::VOXEL_TERRAIN], vbo[eVoxelVertexBuffer::VOXEL_STATIC], vbo[eVoxelVertexBuffer::VOXEL_DYNAMIC] };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
				);
			}
		}

		cb.end();	// ********* command buffer end is called here only //

		// do not believe this is neccessary __streaming_store_fence(); // ensure writes are coherent before queue submission
	}
	void cVoxelWorld::AcquireTransferQueueOwnership(uint32_t const resource_index, vk::CommandBuffer& __restrict cb)
	{		
		// transfer queue ownership of buffers *required* see voxelworld.cpp Transfer() function
		{ // ## ACQUIRE ## //
			static constexpr size_t const buffer_count(2ULL);
			std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &_buffers.shared_buffer[resource_index], &_buffers.subgroup_layer_count_max[resource_index] };
			vku::GenericBuffer::barrier(buffers, // ## ACQUIRE ## // batched 
				cb, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, // first usage is in z only pass in voxel_clear.frag
				vk::DependencyFlagBits::eByRegion,
				vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
			);
		}
	}

	STATIC_INLINE bool const __vectorcall zoomCamera(int32_t const inout)
	{
		if (0 != inout) {

			// setup lerp
			oCamera.PrevZoomFactor = oCamera.ZoomFactor;
			oCamera.TargetZoomFactor = SFM::clamp(oCamera.ZoomFactor + float(inout) * Globals::ZOOM_SPEED, Globals::MAX_ZOOM_FACTOR, Globals::MIN_ZOOM_FACTOR);

			// signal transition
			oCamera.tZoomStart = critical_now();  // closer to exact now results in the lerp not being skipped right to target position

			return(true); // zoom started
		}

		return(false); // zoom cancelled
	}

	bool const __vectorcall cVoxelWorld::zoomCamera(FXMVECTOR const xmExtents)	// to maximize zoom to a world aabb located at center of cameras
	{
		//XMMATRIX const xmViewProj(XMMatrixMultiply(_Visibility.getViewMatrix(), _Visibility.getProjectionMatrix()));
		
		//point2D_t const frameBufferSize(MinCity::getFramebufferSize());
		//XMVECTOR const xmFrameBufferSize(p2D_to_v2(frameBufferSize));

		//XMVECTOR const xmScreenSpaceMax(SFM::WorldToScreen(xmExtents, xmFrameBufferSize, xmViewProj));
		//point2D_t const screenPointMax(v2_to_p2D_rounded(xmScreenSpaceMax));
		
		//XMVECTOR const xmScreenSpaceMin(SFM::WorldToScreen(XMVectorNegate(xmExtents), xmFrameBufferSize, xmViewProj));
		//point2D_t const screenPointMin(v2_to_p2D_rounded(xmScreenSpaceMin));

		//rect2D_t screenSpaceAABB(screenPointMin, screenPointMax);
		
		int const iContainment = _Visibility.AABBIntersectFrustum(XMVectorZero(), xmExtents);
		float zoom_direction(0.0f);

		if (iContainment > 0) { // fully inside
			zoom_direction = 1.0f;

			if (!oCamera.ZoomToExtents) {
				oCamera.ZoomToExtentsInitialDirection = true;
				oCamera.ZoomToExtents = true;
				XMStoreFloat3A(&oCamera.ZoomExtents, xmExtents);
			}
			else if (!oCamera.ZoomToExtentsInitialDirection) { // going in opposite direction ?
				// change in direction, stop auto-zooming
				oCamera.ZoomToExtents = false;
			}
		}
		else { // intersecting or fully outside
			zoom_direction = -1.0f;

			if (!oCamera.ZoomToExtents) {
				oCamera.ZoomToExtentsInitialDirection = false;
				oCamera.ZoomToExtents = true;
				XMStoreFloat3A(&oCamera.ZoomExtents, xmExtents);
			}
			else if (oCamera.ZoomToExtentsInitialDirection) { // going in opposite direction ?
				// change in direction, stop auto-zooming
				oCamera.ZoomToExtents = false;
			}
		}

		if (oCamera.ZoomToExtents) {
			zoomCamera(zoom_direction);
		}
		else {
			//reset
			XMStoreFloat3A(&oCamera.ZoomExtents, XMVectorZero());
			oCamera.ZoomToExtentsInitialDirection = false;
		} 

		return(oCamera.ZoomToExtents);
	}
	
	void __vectorcall cVoxelWorld::zoomCamera(float const inout)
	{
		::zoomCamera(-SFM::round_to_i32(inout)); // is inverted
	}

	STATIC_INLINE bool const __vectorcall rotateCamera(float const anglerelative)
	{
		if (0.0f != anglerelative) {
			// setup lerp
			oCamera.PrevYawAngle = oCamera.Yaw.angle();
			oCamera.TargetYawAngle = oCamera.Yaw.angle() + anglerelative;

			// signal transition
			oCamera.tRotateStart = critical_now();

			return(true); // rotation started
		}

		// rotation cancelled
		return(false);
	}

	void __vectorcall cVoxelWorld::rotateCamera(float const anglerelative)
	{
		::rotateCamera(anglerelative);
	}

	STATIC_INLINE bool const XM_CALLCONV translateCamera(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does not orient in current direction of camera
	{
		uint32_t Result;
		XMVectorEqualR(&Result, XMVectorZero(), xmDisplacement);
		if (XMComparisonAnyFalse(Result)) {

			XMVECTOR const xmOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat3A(&(XMFLOAT3A const&)oCamera.Origin))); // only care about xz components, make it a 2D vector

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, (xmPosition)); // always a clean integral number on completion
			XMStoreFloat2A(&oCamera.PrevPosition, xmOrigin);

			// signal transition
			oCamera.tTranslateStart = now();

			return(true); // translation started
		}

		return(false); // translation cancelled
	}

	STATIC_INLINE bool const XM_CALLCONV translateCameraOrient(XMVECTOR xmDisplacement)	// this one orients in the current direction of the camera
	{
		uint32_t Result;
		XMVectorEqualR(&Result, XMVectorZero(), xmDisplacement);
		if (XMComparisonAnyFalse(Result)) {

			// orient displacement in direction of camera
			xmDisplacement = v2_rotate(xmDisplacement, oCamera.Yaw);

			XMVECTOR const xmOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat3A(&(XMFLOAT3A const&)oCamera.Origin))); // only care about xz components, make it a 2D vector

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, (xmPosition)); // always a clean integral number on completion
			XMStoreFloat2A(&oCamera.PrevPosition, xmOrigin);

			// signal transition
			oCamera.tTranslateStart = now();

			return(true); // translation started
		}

		return(false); // translation cancelled
	}

	void __vectorcall cVoxelWorld::translateCamera(point2D_t const vDir)	// intended for scrolling from mouse hitting screen extents
	{
		XMVECTOR xmIso = p2D_to_v2(point2D_t(-vDir.y, vDir.x)); // must be swapped and 1st component negated (reflected)
		xmIso = XMVector2Normalize(xmIso);
		xmIso = v2_rotate(xmIso, v2_rotation_constants::v225); // 225 degrees is perfect rotation to align isometry to window up/down left/right
		
		// this makes scrolling on either axis the same constant step, otherwise scrolling on xaxis is faster than yaxis
		point2D_t const absDir(p2D_abs(vDir));
		if (absDir.x >= absDir.y) {		 
			xmIso = XMVectorScale(xmIso, XMVectorGetX(XMVector2Length(p2D_to_v2(absDir))));
		}
		else {
			xmIso = XMVectorScale(xmIso, XMVectorGetX(XMVector2Length(p2D_to_v2(absDir))) * cMinCity::getFramebufferAspect());
		}

		::translateCameraOrient(xmIso); // this then scrolls in direction camera is currently facing correctly
	}

	void XM_CALLCONV cVoxelWorld::translateCamera(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does not orient in current direction of camera
	{
		::translateCamera(xmDisplacement);
	}

	void XM_CALLCONV cVoxelWorld::translateCameraOrient(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does orient in current direction of camera
	{
		::translateCameraOrient(xmDisplacement);
	}

	void cVoxelWorld::resetCameraAngleZoom()
	{
		// rotation //
		if (0.0f != oCamera.Yaw.angle()) {
			// setup lerp
			oCamera.PrevYawAngle = oCamera.Yaw.angle();
			oCamera.TargetYawAngle = 0.0f;

			// signal transition
			oCamera.tRotateStart = critical_now();
		}

		// zoom //
		if (Globals::DEFAULT_ZOOM_SCALAR != oCamera.ZoomFactor) {
			// setup lerp
			oCamera.PrevZoomFactor = oCamera.ZoomFactor;
			oCamera.TargetZoomFactor = Globals::DEFAULT_ZOOM_SCALAR;

			// signal transition
			oCamera.tZoomStart = critical_now();
		}
	}

	void cVoxelWorld::resetCamera()
	{
		oCamera.reset();
		_bCameraTurntable = false;
	}

	// pixel perfect mouse picking used instead, leaving this here just in case ray picking is needed in future
	/*
	XMVECTOR const XM_CALLCONV cVoxelWorld::UpdateRayPicking(FXMMATRIX xmView, CXMMATRIX xmProj, CXMMATRIX xmWorld)
	{
		XMFLOAT2A frameWidthHeight;

		XMStoreFloat2A(&frameWidthHeight, p2D_to_v2(MinCity::getFramebufferSize()));

		XMVECTOR xmPosition = XMLoadFloat2A(&XMFLOAT2A(_vMouseHiRes.x, frameWidthHeight.y-_vMouseHiRes.y));  // mouse x,y hiresolution position

		xmPosition = XMVectorSetZ(xmPosition, 0.0f); // MinZ

			XMVECTOR const xmPickRayOrig = XMVector3Unproject(xmPosition,
				0.0f, 0.0f, frameWidthHeight.x, frameWidthHeight.y, 0.0f, 1.0f,
				xmProj, xmView, xmWorld);
			XMStoreFloat3A(&_vPickRayOrigin, xmPickRayOrig);


		xmPosition = XMVectorSetZ(xmPosition, 1.0f); // MaxZ

			XMVECTOR const xmPickRayEnd = XMVector3Unproject(xmPosition,
				0.0f, 0.0f, frameWidthHeight.x, frameWidthHeight.y, 0.0f, 1.0f,
				xmProj, xmView, xmWorld);
			XMStoreFloat3A(&_vPickRayEnd, xmPickRayEnd);


		// Return Mouse Ray Direction Vector
		return(XMVector3Normalize(XMVectorSubtract(xmPickRayEnd, xmPickRayOrig)));
	}
	*/

	

	void XM_CALLCONV cVoxelWorld::HoverVoxel()
	{
#ifdef DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP
		time_point const tStart(high_resolution_clock::now());
#endif
#ifdef DEBUG_MOUSE_HOVER_VOXEL
		{ // emiisive highlight removal
			Iso::Voxel const* const pVoxelHovered = world::getVoxelAt(_voxelIndexHover);
			if (pVoxelHovered) {
				Iso::Voxel oVoxel(*pVoxelHovered);
				Iso::clearEmissive(oVoxel);
				world::setVoxelAt(_voxelIndexHover, oVoxel);
			}
		}
#endif
		{ // get current mouse voxel Index that is hovered, and a copy of the rawData from gpu_read_back
			// pixel perfect mouse picking
			static constexpr int32_t const MAX_VALUE(UINT16_MAX);
			static constexpr float const INV_MAX_VALUE(1.0f / float(MAX_VALUE));

			XMVECTOR const xmMouse(XMLoadFloat2A(&_vMouse));
			point2D_t const rawData[2]{ MinCity::Vulkan->queryMouseBuffer(xmMouse, 1), MinCity::Vulkan->queryMouseBuffer(xmMouse, 0) };

			if (!rawData[0].isZero()) { // bugfix for when mouse hovers out of grid bounds 

				// normalize from [0...65535] to [0.0f....1.0f] //
				XMVECTOR xmVoxelIndex(p2D_to_v2(rawData[0]));
				xmVoxelIndex = XMVectorScale(xmVoxelIndex, INV_MAX_VALUE);

				// change normalized range to voxel grid range [0.0f....1.0f] to [-WORLD_GRID_FHALFSIZE, WORLD_GRID_FHALFSIZE]
				xmVoxelIndex = SFM::__fms(xmVoxelIndex, XMVectorSet(Iso::WORLD_GRID_FWIDTH, Iso::WORLD_GRID_FHEIGHT, 0.0f, 0.0f), XMVectorSet(Iso::WORLD_GRID_FHALF_WIDTH, Iso::WORLD_GRID_FHALF_HEIGHT, 0.0f, 0.0f));

				point2D_t const voxelIndex(v2_to_p2D_rounded(xmVoxelIndex)); // *** MUST BE ROUNDED FOR SELECTION PRECISION *** //

				_voxelIndexHover.v = voxelIndex.v;
				_occlusion.groundVoxelIndex.v = voxelIndex.v;
			}
			if (!rawData[1].isZero()) { // bugfix for when mouse hovers out of grid bounds 

				// normalize from [0...65535] to [0.0f....1.0f] //
				XMVECTOR xmVoxelIndex(p2D_to_v2(rawData[1]));
				xmVoxelIndex = XMVectorScale(xmVoxelIndex, INV_MAX_VALUE);

				// change normalized range to voxel grid range [0.0f....1.0f] to [-WORLD_GRID_FHALFSIZE, WORLD_GRID_FHALFSIZE]
				xmVoxelIndex = SFM::__fms(xmVoxelIndex, XMVectorSet(Iso::WORLD_GRID_FWIDTH, Iso::WORLD_GRID_FHEIGHT, 0.0f, 0.0f), XMVectorSet(Iso::WORLD_GRID_FHALF_WIDTH, Iso::WORLD_GRID_FHALF_HEIGHT, 0.0f, 0.0f));

				point2D_t const voxelIndex(v2_to_p2D_rounded(xmVoxelIndex)); // *** MUST BE ROUNDED FOR SELECTION PRECISION *** //

				if (voxelIndex != _occlusion.occlusionVoxelIndex) {

					uvec4_v const cmpMax(Iso::SCREEN_VOXELS);
					point2D_t absDiff(p2D_abs(p2D_sub(_occlusion.occlusionVoxelIndex, voxelIndex)));
					uvec4_v cmpDiff(absDiff.v);

					if (uvec4_v::all<2>(cmpDiff < cmpMax)) { // filter out large sudden changes
					
						uvec4_v const cmpTwo(2);
						absDiff = p2D_abs(p2D_sub(_occlusion.groundVoxelIndex, voxelIndex));
						cmpDiff.v = absDiff.v;

						if (uvec4_v::all<2>(cmpDiff < cmpTwo)) { // update the hoverindex if within (+-)1 voxels of last recorded *known good* ground voxel index
																 // this improves the latency of input and accuracy.
							_voxelIndexHover.v = voxelIndex.v;
						}

						_occlusion.occlusionVoxelIndex.v = voxelIndex.v;
					}
					
				}
				_lastOcclusionQueryValid = true;

			}
			else {
				_lastOcclusionQueryValid = false;
			}

			// on rawData being zero, no changes to the voxelIndex hovered are made for both buffers

		}
#ifdef DEBUG_MOUSE_HOVER_VOXEL
		{ // emiisive highlight add
			Iso::Voxel const* const pVoxelHovered = world::getVoxelAt(_voxelIndexHover);
			if (pVoxelHovered) {
				Iso::Voxel oVoxel(*pVoxelHovered);
				Iso::setEmissive(oVoxel);
				world::setVoxelAt(_voxelIndexHover, oVoxel);
			}
		}
#endif

#ifdef DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP
		microseconds const tDelta(duration_cast<microseconds>(high_resolution_clock::now() - tStart));

		static microseconds DebugVariable;
		DebugVariable = tDelta;
		setDebugVariable(microseconds, DebugLabel::HOVERVOXEL_US, DebugVariable);
#endif
	}

	void __vectorcall cVoxelWorld::UpdateUniformState(float const tRemainder)  // interpolated sub frame state also containing anything that needs to be updated every frame
	{
		static constexpr float const MAX_DELTA = time_to_float(duration_cast<fp_seconds>(fixed_delta_x2_duration)),		// 66ms
									 MIN_DELTA = MAX_DELTA * 0.125f;														// 8ms
		constinit static float time_last(0.0f), // last interpolated time
							   time_delta_last(0.0f); // last interpolated time delta

		// should be done first
		Interpolator.interpolate(0.5f * Globals::INTERPOLATION_TIME_SCALAR); // *bugfix - using tRemainder here is unstable - motion jitters or oscilates... 0.5f is where time ticks normally in regards to the interpolation of motion between frames, slow-motion is possible with small values (time scalar)

		_currentState.time = SFM::lerp(_lastState.time, _targetState.time, tRemainder);

		// clamp at the 2x step size, don't care or want spurious spikes of time
		float const time_delta = SFM::clamp(_currentState.time - time_last, MIN_DELTA, MAX_DELTA);

		//pack into vector for uniform buffer layout			
		_currentState.Uniform.aligned_data0 = XMVectorSet(oCamera.voxelFractionalGridOffset.x, oCamera.voxelFractionalGridOffset.z, (time_delta + time_delta_last) * 0.5f, _currentState.time);
		                                                                
		_currentState.Uniform.frame = (uint32_t)MinCity::getFrameCount();		// todo check overflow of 32bit frame counter for shaders

		time_last = _currentState.time; // update last interpolated time
		time_delta_last = time_delta;	//   ""    ""       ""      time delta

		// view matrix derived from eyePos
		XMVECTOR xmEyePos(SFM::lerp(_lastState.Uniform.eyePos, _targetState.Uniform.eyePos, tRemainder));

		////////////////////////////////////////////////////////////////////////////////////////////////////////// Fractional Offset (ONLY Location)
		XMVECTOR xmFract(XMLoadFloat3A(&oCamera.voxelFractionalGridOffset));
		//xmFract = XMVectorAdd(xmFract, XMVectorSet(0.0f, Iso::WORLD_MAX_HEIGHT * Iso::VOX_SIZE * 0.5f, 0.0f, 0.0f));
		xmEyePos = XMVectorAdd(xmEyePos, xmFract);  // this all allows fractional movement of the camera *do not change*
		//////////////////////////////////////////////////////////////////////////////////////////////////////////

		_currentState.Uniform.eyePos = xmEyePos;
		_currentState.Uniform.eyeDir = XMVector3Normalize(XMVectorSubtract(xmEyePos, xmFract)); // target is always 0,0,0 this would normally be 0 - eyePos, it's upside down instead to work with Vulkan Coordinate System more easily.
		
		// All positions in game are transformed by the view matrix in all shaders. Fractional Offset does not work with xmEyePos, something internal to the function XMMatrixLookAtLH, xmEyePos must remain the same w/o fractional offset))
		
		XMMATRIX const xmView = XMMatrixLookAtLH(xmEyePos, xmFract, Iso::xmUp); // notice xmUp is positive here (everything is upside down) to get around Vulkan Negative Y Axis see above eyeDirection

		// *bugfix - ***do not change***
		// view matrix is independent of fractional offset, fractional offset is no longer applied to the view matrix. don't fuck with the fractional_offset, it's not required here
		_currentState.Uniform.view = xmView;

		// Update Frustum, which updates projection matrix, which is derived from ZoomFactor
		_currentState.zoom = SFM::lerp(_lastState.zoom, _targetState.zoom, tRemainder);
		_Visibility.UpdateFrustum(xmView, _currentState.zoom, MinCity::getFrameCount());

		// Get current projection matrix after update of frustum and in turn projection
		_currentState.Uniform.proj = _Visibility.getProjectionMatrix();
		// matrices can not be interpolated effectively they must be recalculated
		// from there base components
	}
	void __vectorcall cVoxelWorld::UpdateUniformStateTarget(tTime const& __restrict tNow, bool const bFirstUpdate) // fixed timestep state
	{
		_lastState = _targetState;

		_targetState.time = time_to_float(fp_seconds(tNow - start()));

		// "XMMatrixInverse" found to be imprecise for acquiring vector components (forward, direction)
		// using original values instead (-----precise))
		_targetState.Uniform.eyePos = v3_rotate_yaw(Iso::xmEyePt_Iso, oCamera.Yaw);

		_targetState.zoom = oCamera.ZoomFactor;
		
		// move state to last target
		UpdateUniformState(((float)bFirstUpdate));
	}
	void cVoxelWorld::PreUpdate(bool const bPaused) // *** timing is unreliable in this function, do not use time in this function
	{
		// special input handling (required every frame) //
		HoverVoxel();
	}
	bool const cVoxelWorld::UpdateOnce(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, bool const bPaused)
	{
		bool const bJustLoaded(_onLoadedRequired);
		
		[[unlikely]]  if (bJustLoaded && !_importing) { // defers onloaded event @ startup in case of importing.
			// ### ONLOADED EVENT must be triggered here !! //
			OnLoaded(tNow);
		}
		else {
			
			updateMouseOcclusion(bPaused);

			{ // static instance validation

				using static_unordered_map = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Static*>;
				for (static_unordered_map::const_iterator iter = _hshVoxelModelInstances_Static.cbegin();
					iter != _hshVoxelModelInstances_Static.cend(); ++iter) {

					if (iter->second) {
						iter->second->Validate();
					}
				}
			}
			{ // dynamic instance validation

				using dynamic_unordered_map = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Dynamic*>;
				for (dynamic_unordered_map::const_iterator iter = _hshVoxelModelInstances_Dynamic.cbegin();
					iter != _hshVoxelModelInstances_Dynamic.cend(); ++iter) {

					if (iter->second) {
						iter->second->Validate();
					}
				}
			}
		} // !bJustLoaded
		
		return(bJustLoaded);
	}
	void cVoxelWorld::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, bool const bPaused, bool const bJustLoaded)
	{
		ZoneScopedN("Update");

		// ***** Anything that will affect uniform state BEFORE
		UpdateCamera(tNow, tDelta);

		//###########################################################//
		UpdateUniformStateTarget(tNow, bJustLoaded);
		//###########################################################//

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
		_streamingGrid.OutputDebugStats(tDelta);
#endif
#endif

		// ***** Anything that uses uniform state updates AFTER
		_bMotionDelta = false; // must reset *here*
		
		[[unlikely]] if (bJustLoaded)
			return;
		
		[[unlikely]] if (eExclusivity::DEFAULT != cMinCity::getExclusivity()) // ** Only after this point exists game update related code
			return;															 // ** above is independent, and is compatible with all alternative exclusivity states (LOADING/SAVING/etc.)

		// any operations that do not need to execute while paused should not
		if (!bPaused) {
			
			world::access::update_game_objects(tNow, tDelta);

			MinCity::UserInterface->Update(tNow, tDelta);

			MinCity::Physics->Update(tNow, tDelta); // improving latency apply physics update after all user, user game object updates

			// last etc.
			if (oCamera.ZoomToExtents) {

				// auto zoom to AABB extents feature
				zoomCamera(XMLoadFloat3A(&oCamera.ZoomExtents));
			}

		} // end !Paused //

		// ---------------------------------------------------------------------------//
		CleanUpInstanceQueue(); // *** must be done after all game object updates *** //
		// ---------------------------------------------------------------------------//
	}

	void cVoxelWorld::AsyncClears(uint32_t const resource_index)
	{
		_AsyncClearTaskID = async_long_task::enqueue<background_critical>([&] {

				{ // terrain
					auto const& directBuffer(voxels.visibleTerrain.buffer.direct);
					___memset_threaded_stream<64>(directBuffer, 0, voxels.visibleTerrain.buffer.active_size);
					voxels.visibleTerrain.buffer.active_size = 0;
					voxels.visibleTerrain.bits->clear();
				}
				{ // static
					auto const& directBuffer(voxels.visibleStatic.buffer.direct);
					___memset_threaded_stream<64>(directBuffer, 0, voxels.visibleStatic.buffer.active_size);
					voxels.visibleStatic.buffer.active_size = 0;
					voxels.visibleStatic.bits->clear();
				}
				{ // dynamic - opaque
					auto const& directBuffer(voxels.visibleDynamic.opaque.buffer.direct);
					___memset_threaded_stream<64>(directBuffer, 0, voxels.visibleDynamic.opaque.buffer.active_size);
					voxels.visibleDynamic.opaque.buffer.active_size = 0;
					voxels.visibleDynamic.opaque.bits->clear();
				}
				{ // dynamic - transparent
					auto const& directBuffer(voxels.visibleDynamic.trans.buffer.direct);
					___memset_threaded_stream<64>(directBuffer, 0, voxels.visibleDynamic.trans.buffer.active_size);
					voxels.visibleDynamic.trans.buffer.active_size = 0;
					voxels.visibleDynamic.trans.bits->clear();
				}  
		});

		{
			// *bugfix - having these clears inside of the async thread causes massive flickering of light, not coherent! Moving it outside and simultaneous still works fine.
			getVolumetricOpacity().clear(resource_index); // better distribution of cpu at a later point in time of the frame.
		}
	}
		
	void cVoxelWorld::SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		_OpacityMap.SetSpecializationConstants_ComputeLight(constants);
	}

	void cVoxelWorld::SetSpecializationConstants_DepthResolve_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSz(MinCity::getFramebufferSize());
		point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

		constants.emplace_back(vku::SpecializationConstant(0, ((float)downResFrameBufferSz.x) / ((float)frameBufferSz.x) ));// // half-res frame buffer width / full-res ... width
		constants.emplace_back(vku::SpecializationConstant(1, ((float)downResFrameBufferSz.y) / ((float)frameBufferSz.y) ));// // half-res frame buffer height / "" ...  height
	}

	void cVoxelWorld::SetSpecializationConstants_VolumetricLight_VS(std::vector<vku::SpecializationConstant>& __restrict constants) // all shader variables in vertex shader deal natively with position in xyz form
	{
		// volume dimensions																				 // xyz
		constants.emplace_back(vku::SpecializationConstant(0, (float)Volumetric::voxelOpacity::getSize()));  // should be world volume size
	}
	
	void cVoxelWorld::SetSpecializationConstants_VolumetricLight_FS(std::vector<vku::SpecializationConstant>& __restrict constants) // all shader variables should be swizzled to xzy into fragment shader for texture lookup optimization. (ie varying vertex->fragnent shader variables)
	{
		point2D_t const frameBufferSz(MinCity::getFramebufferSize());
		point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

		constants.emplace_back(vku::SpecializationConstant(0, (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)(downResFrameBufferSz.y)));// // half-res frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)(downResFrameBufferSz.y)));// // half-res frame buffer height

		// volume dimensions //																					// xzy
		constants.emplace_back(vku::SpecializationConstant(4,  (float)Volumetric::voxelOpacity::getSize()));		  // should be world volume size
		constants.emplace_back(vku::SpecializationConstant(5,  1.0f / ((float)Volumetric::voxelOpacity::getSize())));       // should be inverse world volume size
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getVolumeLength() * Iso::MINI_VOX_SIZE)); // should be world volume length scaled by minivoxelsize

		// light volume dimensions //																				// xzy
		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize()));		   // should be light volume size
		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)(Volumetric::voxelOpacity::getLightSize())));   // should be 1.0 / light volume size

		// For depth reconstruction from hardware depth buffer
		// https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
		constexpr double ZFar = Globals::MAXZ_DEPTH;
		constexpr double ZNear = Globals::MINZ_DEPTH * Iso::VOX_MINZ_SCALAR;
		constants.emplace_back(vku::SpecializationConstant(9, (float)ZFar)); 
		constants.emplace_back(vku::SpecializationConstant(10, (float)ZNear));
	}

	void cVoxelWorld::SetSpecializationConstants_Nuklear_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		MinCity::Nuklear->SetSpecializationConstants_FS(constants);
	}

	void cVoxelWorld::SetSpecializationConstants_Resolve(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSz(MinCity::getFramebufferSize());
		point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

		constants.emplace_back(vku::SpecializationConstant(0, (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)(downResFrameBufferSz.y)));// // half-res frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)(downResFrameBufferSz.y)));// // half-res frame buffer height

	}
	void cVoxelWorld::SetSpecializationConstants_Upsample(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height
	}
	void cVoxelWorld::SetSpecializationConstants_PostAA(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x ));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y ));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height
	}
	void cVoxelWorld::SetSpecializationConstants_PostAA_HDR(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(4, (float)MinCity::Vulkan->getMaximumNits()));// // maximum brightness of user monitor in nits, as defined in MinCity.ini
	}

	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size
		constants.emplace_back(vku::SpecializationConstant(5, 1.0f / (float)Volumetric::voxelOpacity::getSize())); // should be inverse world visible volume size

		constants.emplace_back(vku::SpecializationConstant(6, (float)(Volumetric::voxelOpacity::getVolumeLength() * Iso::MINI_VOX_SIZE)));  // should be world volume length * mini voxel size

		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize())); // should be light volume size

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize())); // should be inv light volume size

		constants.emplace_back(vku::SpecializationConstant(9, ((float)_terrainTexture->extent().width))); // _terrainTexture2 matches extents of _terrainTexture
		constants.emplace_back(vku::SpecializationConstant(10, ((float)_terrainTexture->extent().height)));
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_Basic_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, float const voxelSize, float const voxelStep)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)voxelSize)); // VS is dependent on type of voxel for geometry size
		constants.emplace_back(vku::SpecializationConstant(1, (float)voxelStep)); // VS is dependent on type of voxel for geometry step
		constants.emplace_back(vku::SpecializationConstant(2, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size

		XMFLOAT3A transformBias, transformInv;

		XMStoreFloat3A(&transformBias, Volumetric::_xmTransformToIndexBias);
		XMStoreFloat3A(&transformInv, Volumetric::_xmInverseVisible);

		// do not swizzle or change order
		constants.emplace_back(vku::SpecializationConstant(3, (float)transformBias.x));
		constants.emplace_back(vku::SpecializationConstant(4, (float)transformBias.y));
		constants.emplace_back(vku::SpecializationConstant(5, (float)transformBias.z));

		constants.emplace_back(vku::SpecializationConstant(6, (float)transformInv.x));
		constants.emplace_back(vku::SpecializationConstant(7, (float)transformInv.y));
		constants.emplace_back(vku::SpecializationConstant(8, (float)transformInv.z));
	}

	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_Basic_VS_Common(constants, Iso::VOX_SIZE, Iso::VOX_STEP); // should be voxsize & voxstep

		// used for uv -> voxel in vertex shader image store operation for opacity map
		constants.emplace_back(vku::SpecializationConstant(9, (int)MINIVOXEL_FACTOR));	
		constants.emplace_back(vku::SpecializationConstant(10, (float)Iso::TERRAIN_MAX_HEIGHT));
	}
	
	void cVoxelWorld::SetSpecializationConstants_Voxel_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, float const voxelSize, float const voxelStep)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)voxelSize)); // VS is dependent on type of voxel for geometry size
		constants.emplace_back(vku::SpecializationConstant(1, (float)voxelStep)); // VS is dependent on type of voxel for geometry step
		constants.emplace_back(vku::SpecializationConstant(2, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_VS(std::vector<vku::SpecializationConstant>& __restrict constants) // ** also used for roads 
	{
		SetSpecializationConstants_Voxel_VS_Common(constants, Iso::VOX_SIZE, Iso::VOX_STEP); // should be voxsize & voxstep

		constants.emplace_back(vku::SpecializationConstant(3, (int)MINIVOXEL_FACTOR));
		constants.emplace_back(vku::SpecializationConstant(4, (float)Iso::TERRAIN_MAX_HEIGHT));
	}
	
	void cVoxelWorld::SetSpecializationConstants_Voxel_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_VS_Common(constants, Iso::MINI_VOX_SIZE, Iso::VOX_STEP); // should be minivoxsize & voxstep
	}
	void cVoxelWorld::SetSpecializationConstants_Voxel_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_Basic_VS_Common(constants, Iso::MINI_VOX_SIZE, Iso::VOX_STEP); // should be minivoxsize & voxstep
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_GS_Common(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size

		// used for uv creation in geometry shader

		XMFLOAT3A transformBias, transformInv;

		XMStoreFloat3A(&transformBias, Volumetric::_xmTransformToIndexBias);
		XMStoreFloat3A(&transformInv, Volumetric::_xmInverseVisible);

		// do not swizzle or change order
		constants.emplace_back(vku::SpecializationConstant(1, (float)transformBias.x));
		constants.emplace_back(vku::SpecializationConstant(2, (float)transformBias.y));
		constants.emplace_back(vku::SpecializationConstant(3, (float)transformBias.z));

		constants.emplace_back(vku::SpecializationConstant(4, (float)transformInv.x));
		constants.emplace_back(vku::SpecializationConstant(5, (float)transformInv.y));
		constants.emplace_back(vku::SpecializationConstant(6, (float)transformInv.z));
	}
	void cVoxelWorld::SetSpecializationConstants_Voxel_GS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{             
		SetSpecializationConstants_Voxel_GS_Common(constants);
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_GS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_GS_Common(constants);
		
		// used for uv creation in geometry shader 
		constants.emplace_back(vku::SpecializationConstant(7, (0.5f / (float)_terrainTexture->extent().width))); // _terrainTexture2 matches extents of _terrainTexture
		constants.emplace_back(vku::SpecializationConstant(8, (0.5f / (float)_terrainTexture->extent().height)));
	}
	
	void cVoxelWorld::SetSpecializationConstants_Voxel_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size
		constants.emplace_back(vku::SpecializationConstant(5, 1.0f / (float)Volumetric::voxelOpacity::getSize())); // should be inverse world visible volume size

		constants.emplace_back(vku::SpecializationConstant(6, (float)(Volumetric::voxelOpacity::getVolumeLength() * Iso::MINI_VOX_SIZE)));  // should be world volume length * mini voxel size

		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize())); // should be light volume size

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize())); // should be inv light volume size
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_ClearMask_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height
	}
	/*
	[[deprecated]] void cVoxelWorld::SetSpecializationConstants_TextureShader(std::vector<vku::SpecializationConstant>& __restrict constants, uint32_t const shader)
	{
		vk::Extent3D const input_extents(_textureShader[shader].input->extent());
		vk::Extent3D const output_extents(_textureShader[shader].output->extent());
		
		constants.emplace_back(vku::SpecializationConstant(0, (float)output_extents.width));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)output_extents.height));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)output_extents.width));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)output_extents.height));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(4, (float)(input_extents.depth - 1)));// // number of frames - 1 (has to be from input texture extents
	}
	*/
	void cVoxelWorld::UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& __restrict samplerLinearClamp)
	{
		_OpacityMap.UpdateDescriptorSet_ComputeLight(dsu, samplerLinearClamp);
	}
	/*
	[[deprecated]] void cVoxelWorld::UpdateDescriptorSet_TextureShader(vku::DescriptorSetUpdater& __restrict dsu, uint32_t const shader, SAMPLER_SET_STANDARD_POINT)
	{
		// customization of descriptor sets (used samplers) for texture shaders here:
		vk::Sampler sampler;

		switch (shader)
		{
		case eTextureShader::WIND_FBM:
			sampler = samplerPointRepeat;  // input texture (blue noise - requires point sampling)
			break;
		default:
			sampler = samplerLinearRepeat; // default for a texture shaders that don't explicitly define sampler like above.
			break;
		}

		dsu.beginImages(0U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(sampler, _textureShader[shader].input->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);  // input texture (blue noise - requires point sampling)

		dsu.beginImages(1U, 0, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, _textureShader[shader].output->imageView(), vk::ImageLayout::eGeneral); // output texture
	}
	*/
	void cVoxelWorld::UpdateDescriptorSet_VolumetricLight(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict fullnormalImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_LINEAR_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, halfdepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);  // depth
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);  // raymarch uses 2 channels, all slices of blue noise (*bugfix - raymarch jittered offset now also uses blue noise over time, with no visible noisyness in reflections!)
		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, fullnormalImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		//dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		//dsu.image(samplerLinearRepeat, _textureShader[eTextureShader::WIND_DIRECTION].output->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // too difficult does not allow world origin to have or not have the fractional offset properly, texture shaders no longer in use - deprecated
		dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->DistanceDirection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->Color->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 2, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().OpacityMap->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(5U, 0, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, halfreflectionImageView, vk::ImageLayout::eGeneral);
		dsu.beginImages(5U, 1, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, halfvolumetricImageView, vk::ImageLayout::eGeneral);
		
#ifdef DEBUG_VOLUMETRIC
		dsu.beginBuffers(10U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(DebugStorageBuffer->buffer(), 0, sizeof(UniformDecl::DebugStorageBuffer));
#endif
	}

	void cVoxelWorld::UpdateDescriptorSet_VolumetricLightResolve(vku::DescriptorSetUpdater& __restrict dsu, 
																 vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, 
																 vk::ImageView const& __restrict fullvolumetricImageView, vk::ImageView const& __restrict fullreflectionImageView,
																 SAMPLER_SET_LINEAR_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // resolve uses single channel, all slices of blue noise
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfvolumetricImageView, vk::ImageLayout::eGeneral);
		dsu.beginImages(2U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfreflectionImageView, vk::ImageLayout::eGeneral);
		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, fullvolumetricImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, fullreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
	}
	void cVoxelWorld::UpdateDescriptorSet_VolumetricLightUpsample(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu,
		vk::ImageView const& __restrict fulldepthImageView, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView,
		SAMPLER_SET_LINEAR_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, fulldepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointClamp, halfdepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // upsample uses single channel, all slices of blue noise
		dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfvolumetricImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(5U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginBuffers(6U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer[resource_index].buffer(), 0, _buffers.shared_buffer[resource_index].maxsizebytes());
	}
	void cVoxelWorld::UpdateDescriptorSet_PostAA_Post(vku::DescriptorSetUpdater& __restrict dsu,
		vk::ImageView const& __restrict colorImageView, vk::ImageView const& __restrict lastFrameView,
		SAMPLER_SET_LINEAR_POINT)
	{
		// 1 - colorview (backbuffer)
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		// 2 - bluenoise
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // presentation uses single channel, all slices of blue noise

		MinCity::PostProcess->UpdateDescriptorSet_PostAA_Post(dsu, lastFrameView, samplerLinearClamp);
	}
	void cVoxelWorld::UpdateDescriptorSet_PostAA_Final(vku::DescriptorSetUpdater& __restrict dsu,
		vk::ImageView const& __restrict colorImageView, vk::ImageView const& __restrict guiImageView,
		SAMPLER_SET_LINEAR_POINT)
	{
		// 1 - colorview (backbuffer)
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		// 2 - bluenoise
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // presentation uses single channel, all slices of blue noise

		MinCity::PostProcess->UpdateDescriptorSet_PostAA_Final(dsu, guiImageView, samplerLinearClamp);
	}
	void cVoxelWorld::UpdateDescriptorSet_VoxelCommon(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict fullreflectionImageView, vk::ImageView const& __restrict lastColorImageView, SAMPLER_SET_LINEAR_POINT_ANISO, SAMPLER_SET_BORDER)
	{
		dsu.beginBuffers(1U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer[resource_index].buffer(), 0, _buffers.shared_buffer[resource_index].maxsizebytes());
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearBorder, _OpacityMap.getVolumeSet().LightMap->DistanceDirection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(2U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearBorder, _OpacityMap.getVolumeSet().LightMap->Color->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, fullreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _spaceCubemapTexture->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		// finalize texture array and commit to descriptor set #######################################################################
		tbb::concurrent_vector<vku::TextureImage2DArray const*> const& rTextures(MinCity::TextureBoy->lockTextureArray());
		
		dsu.beginImages(5U, TEX_NOISE, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_NOISE_SAMPLER, rTextures[TEX_NOISE]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_BLUE_NOISE, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_BLUE_NOISE_SAMPLER, rTextures[TEX_BLUE_NOISE]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_TERRAIN, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_TERRAIN_SAMPLER, rTextures[TEX_TERRAIN]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_TERRAIN2, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_TERRAIN2_SAMPLER, rTextures[TEX_TERRAIN2]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_GRID, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_GRID_SAMPLER, rTextures[TEX_GRID]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_BLACKBODY, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_BLACKBODY_SAMPLER, rTextures[TEX_BLACKBODY]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		// #############################################################################################################################
		
		dsu.beginImages(6U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, lastColorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(7U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerAnisoRepeat, _terrainDetail->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
	}
	void cVoxelWorld::UpdateDescriptorSet_Voxel_ClearMask(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, SAMPLER_SET_LINEAR)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, _OpacityMap.getVolumeSet().OpacityMap->imageView(), vk::ImageLayout::eGeneral);	// used to clear opacity
		dsu.beginBuffers(2U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.subgroup_layer_count_max[resource_index].buffer(), 0, _buffers.subgroup_layer_count_max[resource_index].maxsizebytes());
		dsu.beginBuffers(3U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer[resource_index].buffer(), 0, _buffers.shared_buffer[resource_index].maxsizebytes());
	}
	
	// queries if voxel model instance of type intersects a voxel. does not have to be owner voxel.
	uint32_t const cVoxelWorld::hasVoxelModelInstanceAt(point2D_t const voxelIndex, int32_t const modelGroup, uint32_t const modelIndex) const
	{
		Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

		for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {

			// get hash, which should be the voxel model instance ID
			uint32_t const hash(Iso::getHash(oVoxel, i));

			if (Iso::STATIC_HASH == i) {
				auto const instance = lookupVoxelModelInstance<false>(hash);

				if (instance) {
					auto const ident = instance->getModel().identity();
					if (modelGroup == ident._modelGroup && modelIndex == ident._index) {
						return(hash);
					}
				}
			}
			else {
				auto const instance = lookupVoxelModelInstance<true>(hash);

				if (instance) {
					auto const ident = instance->getModel().identity();
					if (modelGroup == ident._modelGroup && modelIndex == ident._index) {
						return(hash);
					}
				}
			}
		}
		
		return(0);
	}
	// queries if voxel model instance of type intersects a voxel area. has to be the owner voxel of the instance for this to succeed.
	uint32_t const cVoxelWorld::hasVoxelModelInstanceAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex) const
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				uint32_t const hash(hasVoxelModelInstanceAt(voxelIterate, modelGroup, modelIndex));

				if (hash) {
					return(hash);
				}

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(0);
	}

	// hides (unsets root/owner) so that instance of the specific model type is hidden. has to be the owner voxel of the instance for this to succeed.
	bool const cVoxelWorld::hideVoxelModelInstanceAt(point2D_t const voxelIndex, int32_t const modelGroup, uint32_t const modelIndex, vector<Iso::voxelIndexHashPair>* const pRecordHidden)
	{
		uint32_t existing(0);

		Iso::Voxel oVoxel(world::getVoxelAt(voxelIndex));

		if (Iso::isOwnerAny(oVoxel)) {
			uint32_t hash(0);

			for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {

				if (Iso::isOwner(oVoxel, i)) {

					// get hash, which should be the voxel model instance ID
					hash = Iso::getHash(oVoxel, i);

					if (Iso::STATIC_HASH == i) {
						auto const instance = lookupVoxelModelInstance<false>(hash);

						if (instance) {
							auto const ident = instance->getModel().identity();
							if (modelGroup == ident._modelGroup && modelIndex == ident._index) {
								existing = i;
								break;
							}
						}
					}
					else {
						auto const instance = lookupVoxelModelInstance<true>(hash);

						if (instance) {
							auto const ident = instance->getModel().identity();
							if (modelGroup == ident._modelGroup && modelIndex == ident._index) {
								existing = i;
								break;
							}
						}
					}
				}
			}

			if (existing) {
				Iso::clearAsOwner(oVoxel, existing);
				world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&&>(oVoxel));

				if (pRecordHidden) {
					pRecordHidden->emplace_back(voxelIndex, hash);
				}
			}
		}
		

		return(existing);
	}

	bool const cVoxelWorld::hideVoxelModelInstancesAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex, vector<Iso::voxelIndexHashPair>* const pRecordHidden)
	{
		bool bExisting(false);

		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				bExisting |= hideVoxelModelInstanceAt(voxelIterate, modelGroup, modelIndex, pRecordHidden);

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(bExisting);
	}

	// destroy instances if they exist, does not modify state of voxel grid
	// once the instance finishes its destruction sequence, it will be queued for deletion in a concurrent safe manner
	// upon actual deletion the voxel grid is then modified to remove any references to the model instances hash for its local area.
	// when a model instance is released any associated game object will then be released on its update cycle.
	bool const cVoxelWorld::destroyVoxelModelInstanceAt(Iso::Voxel const& oVoxel, uint32_t const hashTypes)
	{
		static constexpr uint32_t const INSTANCE_COUNT(8);

		bool bExisting(false);

		Volumetric::voxelModelInstanceBase* FoundModelInstance[INSTANCE_COUNT]{ nullptr };

		if (Iso::STATIC_HASH == (hashTypes & Iso::STATIC_HASH) && Iso::hasStatic(oVoxel)) { // static
			// get hash, which should be the voxel model instance ID
			uint32_t const hash(Iso::getHash(oVoxel, Iso::STATIC_HASH));

			if (0 != hash) {

				// resolve model instance
				FoundModelInstance[0] = static_cast<Volumetric::voxelModelInstanceBase* const>(lookupVoxelModelInstance<false>(hash));
			}
		}
		if (Iso::DYNAMIC_HASH == (hashTypes & Iso::DYNAMIC_HASH) && Iso::hasDynamic(oVoxel)) { // dynamic

			for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
				// get hash, which should be the voxel model instance ID
				uint32_t const hash(Iso::getHash(oVoxel, i));

				if (0 != hash) {

					// resolve model instance
					FoundModelInstance[1 + (i - Iso::DYNAMIC_HASH)] = static_cast<Volumetric::voxelModelInstanceBase* const>(lookupVoxelModelInstance<true>(hash));
				}
			}
		}

		for (uint32_t i = 0 ; i < INSTANCE_COUNT; ++i) {
			if (FoundModelInstance[i]) {

				// the instance will delete itself after destruction sequence
				// cleanup of instance from map will happen at that time in a thread safe manner
				// the grid voxels for the area defined by the voxel model instance will also be cleaned up
				// at that time
				FoundModelInstance[i]->destroy(); //its ok if we call destroy multiple times on the same instance, only the first call matters
				bExisting = true;
			}
		}

		// if no such model exists at the location passed in, silently ignore
		return(bExisting);
	}

	bool const cVoxelWorld::destroyVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const hashTypes) // concurrency safe //
	{
		return(destroyVoxelModelInstanceAt(getVoxelAt(voxelIndex), hashTypes));
	}

	bool const cVoxelWorld::destroyVoxelModelInstancesAt(rect2D_t voxelArea, uint32_t const hashTypes)
	{
		bool bExisting(false);

		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, point2D_t(Iso::MIN_VOXEL_COORD_U, Iso::MIN_VOXEL_COORD_V), point2D_t(Iso::MAX_VOXEL_COORD_U, Iso::MAX_VOXEL_COORD_V));

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				if (destroyVoxelModelInstanceAt(voxelIterate, hashTypes)) {  // this will eventually (after destruction sequence)
																	// destroy all voxels for the voxel model instance's own area
					bExisting = true;								// that match the hash type(s)
				}

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(bExisting);
	}

	void cVoxelWorld::destroyVoxelModelInstance(uint32_t const hash) // concurrency safe //
	{
		// Get root voxel world coords
		auto const FoundIndex = lookupVoxelModelInstanceRootIndex(hash);

		if (nullptr != FoundIndex) {

			point2D_t const rootVoxel(*FoundIndex);

			Iso::Voxel const oVoxel(getVoxelAt(rootVoxel));

			if (Iso::getHash(oVoxel, Iso::STATIC_HASH) == hash) {
				// resolve model instance
				Volumetric::voxelModelInstance_Static const* DeleteModelInstance_Static = lookupVoxelModelInstance<Volumetric::voxB::STATIC>(hash);

				if (DeleteModelInstance_Static) { // if found static instance first remove from the lookup map

					_hshVoxelModelInstances_Static[hash] = nullptr; // release ownership

					rect2D_t const vLocalArea(DeleteModelInstance_Static->getModel()._LocalArea);

					_queueCleanUpInstances.emplace(std::forward<hashArea&&>(hashArea{ hash,
																						r2D_add(vLocalArea, rootVoxel)}));

					// *** Actual deletion of voxel model instance happens *** //
					SAFE_DELETE(DeleteModelInstance_Static);
#ifndef NDEBUG
					FMT_LOG(VOX_LOG, "Static Model Instance Deleted");
#endif
				}
			}
			else {

				for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i)
				{
					if (Iso::getHash(oVoxel, i) == hash) {
						// resolve model instance
						Volumetric::voxelModelInstance_Dynamic const* DeleteModelInstance_Dynamic = lookupVoxelModelInstance<Volumetric::voxB::DYNAMIC>(hash);

						if (DeleteModelInstance_Dynamic) {

							_hshVoxelModelInstances_Dynamic[hash] = nullptr; // release ownership

							rect2D_t const vLocalArea(DeleteModelInstance_Dynamic->getModel()._LocalArea);

							_queueCleanUpInstances.emplace(std::forward<hashArea&&>(hashArea{ hash,
																								r2D_add(vLocalArea, rootVoxel),
																								DeleteModelInstance_Dynamic->getYaw() }));

							// *** Actual deletion of voxel model instance happens *** //
							SAFE_DELETE(DeleteModelInstance_Dynamic);
#ifndef NDEBUG
							FMT_LOG(VOX_LOG, "Dynamic Model Instance Deleted");
#endif
						}
					}
				}

			}
		}
	}

	void cVoxelWorld::destroyImmediatelyVoxelModelInstance(uint32_t const hash) // not concurrency safe (public) //
	{
		destroyVoxelModelInstance(hash);
		CleanUpInstanceQueue();
	}

	void cVoxelWorld::CleanUpInstanceQueue() // not concurrency safe (private) //
	{
		while (!_queueCleanUpInstances.empty()) {

			hashArea info{};
				
			if (_queueCleanUpInstances.try_pop(info)) {

				if (0 != info.hash) { // sanity check

					// erased // **** can only be done serially, no concurrent operations on maps can be happening ****
					_hshVoxelModelRootIndex.unsafe_erase(info.hash); // does another search but is safer as iterators can become invalid if concurrent operations exist (they should NOT exist when calling this function)

					if (info.dynamic) {
						// erased // **** can only be done serially, no concurrent operations on maps can be happening ****
						_hshVoxelModelInstances_Dynamic.unsafe_erase(info.hash); // does another search but is safer as iterators can become invalid if concurrent operations exist (they should NOT exist when calling this function)

						// clear old area of the hash id only, this will also clear the owner/root voxel
						world::resetVoxelsHashAt(info.area, info.hash, info.vR);
#ifndef NDEBUG
						FMT_LOG_OK(VOX_LOG, "Dynamic Instance Cleaned Up");
#endif
					}
					else {
						// erased // **** can only be done serially, no concurrent operations on maps can be happening ****
						_hshVoxelModelInstances_Static.unsafe_erase(info.hash); // does another search but is safer as iterators can become invalid if concurrent operations exist (they should NOT exist when calling this function)

						// clear old area of the hash id only, this will also clear the owner/root voxel
						world::resetVoxelsHashAt(info.area, info.hash);
#ifndef NDEBUG
						FMT_LOG_OK(VOX_LOG, "Static Instance Cleaned Up");
#endif
					}
				}
			}
		}
	}

	void cVoxelWorld::CleanUp()
	{
		// cleanup special instances
		
		// _currentVoxelIndexPixMap is released auto-magically - virtual memory

		// cleanup all registered instances
		for (auto& Instance : _hshVoxelModelInstances_Dynamic) {

			auto pDel = Instance.second;
			Instance.second = nullptr;
			SAFE_DELETE(pDel);
		}
		for (auto& Instance : _hshVoxelModelInstances_Static) {

			auto pDel = Instance.second;
			Instance.second = nullptr;
			SAFE_DELETE(pDel);
		}
	
		Volumetric::CleanUpAllVoxelModels();
		SAFE_DELETE(Volumetric::VolumetricLink);

		// Grid 
		((StreamingGrid* const)::grid)->CleanUp();

		_OpacityMap.release();

		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
			voxels.visibleDynamic.opaque.buffer.staging[i].release();
			voxels.visibleDynamic.trans.buffer.staging[i].release();
			voxels.visibleStatic.buffer.staging[i].release();
			voxels.visibleTerrain.buffer.staging[i].release();
		}
		if (voxels.visibleDynamic.opaque.bits) {
			bit_row_atomic<Volumetric::dynamic_direct_buffer_size>::destroy(voxels.visibleDynamic.opaque.bits);
		}
		if (voxels.visibleDynamic.trans.bits) {
			bit_row_atomic<Volumetric::dynamic_direct_buffer_size>::destroy(voxels.visibleDynamic.trans.bits);
		}
		if (voxels.visibleStatic.bits) {
			bit_row_atomic<Volumetric::static_direct_buffer_size>::destroy(voxels.visibleStatic.bits);
		}
		if (voxels.visibleTerrain.bits) {
			bit_row_atomic<Volumetric::terrain_direct_buffer_size>::destroy(voxels.visibleTerrain.bits);
		}

		SAFE_RELEASE_DELETE(_terrainDetail);
		SAFE_DELETE(_terrainTexture);
		SAFE_DELETE(_terrainTexture2);
		SAFE_DELETE(_gridTexture);
		SAFE_DELETE(_blackbodyTexture);
		SAFE_DELETE(_spaceCubemapTexture);

		//[[deprecated]] 
		/*for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader) {
			SAFE_DELETE(_textureShader[shader].indirect_buffer);
			SAFE_DELETE(_textureShader[shader].output);
			if (!_textureShader[shader].referenced) {
				SAFE_DELETE(_textureShader[shader].input);
			}
			else {
				_textureShader[shader].input = nullptr;
			}
		}
		*/

		if (_heightmap) {
			ImagingDelete(_heightmap); _heightmap = nullptr;
		}
		if (_blackbodyImage) {
			ImagingDelete(_blackbodyImage); _blackbodyImage = nullptr;
		}
		
		_buffers.reset_subgroup_layer_count_max.release();
		_buffers.reset_shared_buffer.release();

		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			
			_buffers.subgroup_layer_count_max[resource_index].release();
			_buffers.shared_buffer[resource_index].release();
		}

		supernoise::blue.Release();		

#ifdef DEBUG_STORAGE_BUFFER
		SAFE_RELEASE_DELETE(DebugStorageBuffer);
#endif
	}
} // end ns world