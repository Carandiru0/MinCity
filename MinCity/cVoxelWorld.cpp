#include "pch.h"
#include "sBatched.h"
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

#define V2_ROTATION_IMPLEMENTATION
#include "voxelAlloc.h"
#include "voxelModel.h"
#include "eVoxelModels.h"
#include <Random/superrandom.hpp>
#include "cUserInterface.h"
#include "cCity.h"

#include "rain.h"
#include "cLightGameObject.h"
#include "cImportGameObject.h"
#include "cLevelSetGameObject.h"
#include "cExplosionGameObject.h"

#ifdef GIF_MODE
#include "cVideoScreenGameObject.h"
#include "cRockStageGameObject.h"
#endif

#include "cImportGameObject.h"
#include "cRemoteUpdateGameObject.h"
#include "cBuildingGameObject.h"
#include "cTrafficSignGameObject.h"
#include "cTrafficControlGameObject.h"
#include "cPoliceCarGameObject.h"
#include "cCarGameObject.h"
#include "cCopterGameObject.h"
#include "cTestGameObject.h"
#include "cSignageGameObject.h"
#include "ImageAnimation.h"
#include "Adjacency.h"
#include "eDirection.h"

#include <queue>

// for the texture indices used in large texture array
#include "../Data/Shaders/texturearray.glsl"

#pragma intrinsic(_InterlockedExchangePointer)
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

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
	static inline struct CameraEntity
	{
		static constexpr fp_seconds const TRANSITION_TIME = fp_seconds(milliseconds(32));

		XMFLOAT3A Origin;  // always stored swizzled to x,z coordinates
		XMFLOAT3A voxelFractionalGridOffset; // always stored negated and swizzled to x,z coordinates

		point2D_t
			voxelIndex_TopLeft,
			voxelIndex_Center;
		
		v2_rotation_t Azimuth;
		float PrevAzimuthAngle, TargetAzimuthAngle;	

		XMFLOAT2A PrevPosition, TargetPosition, Displacement;
		XMFLOAT2A Velocity;

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
			Origin(0, 0, 0), // virtual coordinates
			voxelFractionalGridOffset{},
			ZoomExtents{},
			ZoomFactor(Globals::DEFAULT_ZOOM_SCALAR),
			tTranslateStart{ zero_time_point },
			tRotateStart{ zero_time_point },
			tZoomStart{ zero_time_point },
			Motion(false), 
			ZoomToExtentsInitialDirection(false),
			ZoomToExtents(false)
		{}

		void reset()
		{
			XMStoreFloat3A(&voxelFractionalGridOffset, XMVectorZero());
			XMStoreFloat3A(&Origin, XMVectorZero());
			Azimuth.zero();

			ZoomFactor = Globals::DEFAULT_ZOOM_SCALAR;
			tTranslateStart = zero_time_point;
			tRotateStart = zero_time_point;
			tZoomStart = zero_time_point;

			Motion = false;
		}
	} oCamera;

} // end ns

namespace // private to this file (anonymous)
{
	namespace the
	{
		static inline tbb::queuing_rw_mutex lock; // for the::grid

		static inline constinit struct // purposely anonymous union, protected pointer implementation for the::grid
		{
			Iso::Voxel* __restrict		 _protected = nullptr;

			__declspec(safebuffers) __forceinline operator Iso::Voxel* const __restrict() const {
				return(_protected);
			}
		} grid{};
		static inline constinit struct // purposely anonymous union, protected pointer implementation for the::temp
		{
			Iso::mini::Voxel const** __restrict	 _protected = nullptr;

			__declspec(safebuffers) __forceinline operator Iso::mini::Voxel const** const __restrict() const {
				return(_protected);
			}
		} temp{};

	} // end ns
} // end ns

namespace // private to this file (anonymous)
{
	namespace mini
	{
		static inline tbb::concurrent_vector<Iso::mini::Voxel> voxels;	// actual memory that is pointed to by a Iso::mini::voxel

		using volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;

		static inline constinit
			volume* __restrict bits{ nullptr };

	} // end ns
}

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

// ####### Private Init methods
static void ComputeGroundAdjacency()
{
	// entire grid
	recomputeGroundAdjacency(rect2D_t(Iso::MIN_VOXEL_COORD, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD, Iso::MAX_VOXEL_COORD));
}

static uint32_t const RenderNoiseImagePixel(float const u, float const v, supernoise::interpolator::functor const& interp)
{
	static constexpr float const NOISE_SCALAR_HEIGHT = 13.0f * (Iso::WORLD_GRID_FSIZE / 512.0f); // fixed so scale of terrain height is constant irregardless of width/depth of map
																								 // 512 happens to be the right number / base number *do not change*
																								 // *can change the first number only*
																								 // less = lower frequency of change in elevation
																								 // more = higher   ""          ""          ""
	static constexpr int32_t const EDGE_DETECTION_THRESHOLD = 236;

	// Get perlin noise for this voxel
	float const fNoiseHeight = 
		(
		supernoise::getSimplexNoise2D(NOISE_SCALAR_HEIGHT * u, NOISE_SCALAR_HEIGHT * v)
		* supernoise::getPerlinNoise(NOISE_SCALAR_HEIGHT * 0.5f * u, NOISE_SCALAR_HEIGHT * 0.5f * v, 0.0f,
			interp)
		+ supernoise::blue.get2D(NOISE_SCALAR_HEIGHT * u, NOISE_SCALAR_HEIGHT * v) * (1.0f/float(NUM_DISTINCT_GROUND_HEIGHTS + 1)) // NUM_DISTINCT_GROUND_HEIGHTS does not include the height 0 (for maximizing precision elsewhere) so this should be + 1
		) //^^^^^^^^^ the bluenoise add alot of realism to the terrain countours etc are not so flat and gradual
		* 2.0f * 255.0f;

	float const fLog = 255.0f - SFM::lerp(0.0f, 255.0f, logf(fNoiseHeight) / logf(255.0f));

	float const fLerp = SFM::lerp(0.0f, 255.0f, fNoiseHeight / 255.0f);

	int32_t const opXOR = ((int32_t)fLerp ^ (uint32_t)fLog);
	int32_t const opSUB = (int32_t)(fLerp - fLog);
	int32_t const opEDGES = (opSUB ^ opXOR);

	int32_t const opShadedEDGES = (opEDGES > EDGE_DETECTION_THRESHOLD ? GROUND_HEIGHT_NOISE[NUM_DISTINCT_GROUND_HEIGHTS-1] + 1 : opXOR);

#ifdef DEBUG_FLAT_GROUND
	uNoiseHeight = 0;
#endif
	return(SFM::saturate_to_u8(opShadedEDGES));
}

void cVoxelWorld::GenerateGround()
{
	constinit static bool bInitialRun(true);

	supernoise::NewNoisePermutation();

	// Generate Image
	Imaging imageNoise = MinCity::Procedural->GenerateNoiseImage(&RenderNoiseImagePixel, Iso::WORLD_GRID_SIZE, supernoise::interpolator::SmoothStep() );

	MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(15));

	// Create and Upload Texture //
	{
		// must be BGRX into resampling (4 channels)
		Imaging resampledImg = ImagingResample(imageNoise, Iso::WORLD_GRID_SIZE << 1, Iso::WORLD_GRID_SIZE << 1, IMAGING_TRANSFORM_BICUBIC);
		ImagingDelete(imageNoise); imageNoise = resampledImg;

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(20));

		Imaging const filteredImg = MinCity::Procedural->BilateralFilter(imageNoise);
		ImagingDelete(imageNoise); imageNoise = filteredImg;

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(30));

		resampledImg = ImagingResample(imageNoise, Iso::WORLD_GRID_SIZE, Iso::WORLD_GRID_SIZE, IMAGING_TRANSFORM_BICUBIC);
		ImagingDelete(imageNoise); imageNoise = resampledImg;

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(35));

		// terrain teture is multiple of voxel grid size, power of 2 and not exceeding 16384
		// should be placed in dedicated memory on gpu
		resampledImg = ImagingResample(imageNoise, world::TERRAIN_TEXTURE_SZ, world::TERRAIN_TEXTURE_SZ, IMAGING_TRANSFORM_BICUBIC);
		ImagingDelete(imageNoise); imageNoise = resampledImg;

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(40));

		if (_terrainTempImage) {

			ImagingDelete(_terrainTempImage); _terrainTempImage = nullptr;
		}

		_terrainTempImage = ImagingCompressBGRAToBC7(imageNoise);

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(50));

		if (bInitialRun) { // first run at program load requires these things to be setup for texture array and is always run from the main thread

			MinCity::TextureBoy->ImagingToTexture_BC7<false>(_terrainTempImage, _terrainTexture);	// generated texture is in linear colorspace
			ImagingDelete(_terrainTempImage); _terrainTempImage = nullptr; // don't require reloading in onLoaded method.

			MinCity::TextureBoy->AddTextureToTextureArray(_terrainTexture, TEX_TERRAIN);

			// imageNoise is released at end of function

			// terrain grid mipmapped texture
			MinCity::TextureBoy->LoadKTXTexture(_gridTexture, TEXTURE_DIR L"grid.ktx");
			MinCity::TextureBoy->AddTextureToTextureArray(_gridTexture, TEX_GRID);
			bInitialRun = false;
		}
	}

	// Traverse Grid

	struct { // avoid lambda heap
		uint8_t* const* const __restrict image;
	} const p = { imageNoise->image };

	tbb::parallel_for(int(0), imageNoise->ysize, [&p](int yVoxel) {

		int32_t xVoxel(0);
																							// Y is inverted to match world texture uv
		uint32_t const* __restrict pIn(reinterpret_cast<uint32_t const* __restrict>(p.image[Iso::WORLD_GRID_SIZE-1-yVoxel]));
		do
		{	
			
			uint32_t const uNoiseHeight = ((*pIn & 0xFF000000) >> 24); // height/noise remains in alpha channel
			++pIn;

			Iso::Voxel oVoxel;
			oVoxel.Desc = Iso::TYPE_GROUND;
			oVoxel.MaterialDesc = 0;		// Initially visibility is set off on all voxels until ComputeGroundOcclusion()
			Iso::clearColor(oVoxel);		// *bugfix - must clear to default color used for ground on generation.

			if (uNoiseHeight > GROUND_HEIGHT_NOISE[0]) {
				Iso::setHeightStep(oVoxel, 15); 
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[1]) {
				Iso::setHeightStep(oVoxel, 14);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[2]) {
				Iso::setHeightStep(oVoxel, 13);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[3]) {
				Iso::setHeightStep(oVoxel, 12);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[4]) {
				Iso::setHeightStep(oVoxel, 11);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[5]) {
				Iso::setHeightStep(oVoxel, 10);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[6]) {
				Iso::setHeightStep(oVoxel, 9);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[7]) {
				Iso::setHeightStep(oVoxel, 8);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[8]) {
				Iso::setHeightStep(oVoxel, 7);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[9]) {
				Iso::setHeightStep(oVoxel, 6);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[10]) {
				Iso::setHeightStep(oVoxel, 5);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[11]) {
				Iso::setHeightStep(oVoxel, 4);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[12]) {
				Iso::setHeightStep(oVoxel, 3);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[13]) {
				Iso::setHeightStep(oVoxel, 2);
			}
			else if (uNoiseHeight > GROUND_HEIGHT_NOISE[14]) {
				Iso::setHeightStep(oVoxel, 1);
			}
			else {
				Iso::setHeightStep(oVoxel, Iso::DESC_HEIGHT_STEP_0);
			}

			// Save Voxel to Grid SRAM
			*(the::grid + ((yVoxel * Iso::WORLD_GRID_SIZE) + xVoxel)) = oVoxel;

		} while (++xVoxel < Iso::WORLD_GRID_SIZE);

	});

	ImagingDelete(imageNoise);

	// Compute adjacency based on neighbour occupancy
	ComputeGroundAdjacency();
}

static void UpdateFollow(tTime const& __restrict tNow)
{
	/*
	static constexpr float const FOLLOW_SPEED = 0.0003f,
								 FOLLOW_DAMPING = 0.01f;

	// sample target position only every n ms defined by user in follow func
	if ((tNow - oCamera.tLastFollowUpdate) > oCamera.tFollowUpdateInterval) {
		oCamera.vTarget = *oCamera.FollowTarget;
		oCamera.tLastFollowUpdate = tNow;
	}
	XMVECTOR const xmOrigin(XMLoadFloat2A(&oCamera.Origin));
	XMVECTOR const xmDelta = XMVectorSubtract(XMLoadFloat2A(&oCamera.vTarget), xmOrigin);

	XMVECTOR xmVelocity = XMVectorMultiplyAdd(_mm_set1_ps(FOLLOW_SPEED), xmDelta, XMLoadFloat2A(&oCamera.vFollowVelocity));

	xmVelocity = XMVectorScale(xmVelocity, 1.0f - FOLLOW_DAMPING);

	XMStoreFloat2A(&oCamera.Origin, XMVectorAdd(xmOrigin, xmVelocity));
	*/
}

XMVECTOR const XM_CALLCONV cVoxelWorld::UpdateCamera(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
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
			XMStoreFloat3A(&oCamera.Origin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W >(targetPosition));
		}
		else {
			float const tInvDelta = SFM::saturate(time_to_float(tDelta / tSmooth));
			XMVECTOR const xmInterpPosition(SFM::lerp(XMLoadFloat2A(&oCamera.PrevPosition), targetPosition, tInvDelta));
			XMStoreFloat3A(&oCamera.Origin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W >(xmInterpPosition));
			oCamera.Azimuth = v2_rotation_t();
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
			oCamera.Azimuth = oCamera.TargetAzimuthAngle;
		}
		else {
			float const tInvDelta = SFM::saturate(time_to_float(tDelta / tSmooth));
			oCamera.Azimuth = v2_rotation_t( SFM::lerp(oCamera.PrevAzimuthAngle, oCamera.TargetAzimuthAngle, tInvDelta) );
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

				float const len = XMVectorGetX(XMVector2Length(p2D_to_v2(vScroll)));
				_tBorderScroll -= delta() / len;
				_tBorderScroll = fp_seconds(SFM::max(0.0, _tBorderScroll.count()));

				if (zero_time_duration == _tBorderScroll) {
					translateCamera(p2D_shiftl(vScroll, Iso::CAMERA_SCROLL_DISTANCE_MULTIPLIER));
					_bIsEdgeScrolling = true;
					_bMotionDelta = true; // override so that dragging continue
					_tBorderScroll = Iso::CAMERA_SCROLL_DELAY; // reset
				}
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

					//  fXDelta      angle
					//----------- = --------
					// max width     XM_2PI
					rotateCamera((SFM::sgn(fXDelta) * SFM::saturate(abs(fXDelta) / SFM::__sqrt(Iso::SCREEN_VOXELS_XZ*Iso::SCREEN_VOXELS_XZ + Iso::SCREEN_VOXELS_XZ * Iso::SCREEN_VOXELS_XZ))) * XM_2PI * 3.0f);	// bugfix: must be constant delta, not the difference between now() and dragstart

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
	XMVECTOR const xmOrigin = SFM::clamp(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat3A(&oCamera.Origin)), _mm_set1_ps(Iso::MIN_VOXEL_FCOORD), _mm_set1_ps(Iso::MAX_VOXEL_FCOORD));
	
	point2D_t voxelIndex(v2_to_p2D(xmOrigin));

	point2D_t const visibleRadius(p2D_half(point2D_t(Iso::OVER_SCREEN_VOXELS_X, Iso::OVER_SCREEN_VOXELS_Z))); // want radius, hence the half value

	// get starting voxel in TL corner of screen
	voxelIndex = p2D_sub(voxelIndex, visibleRadius);
	
	// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
	point2D_t const voxelIndex_TopLeft = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE));
	oCamera.voxelIndex_TopLeft = p2D_sub(voxelIndex_TopLeft, Iso::GRID_OFFSET);

	point2D_t voxelIndex_Center( SFM::clamp(oCamera.voxelIndex_TopLeft.v, _mm_setzero_si128(), _mm_set1_epi32(Iso::WORLD_GRID_SIZE - 1) ));
	// Change from  (0,0) => (x,y) to  (-x,-y) => (x,y)
	oCamera.voxelIndex_Center = p2D_sub(voxelIndex_Center, point2D_t(Iso::WORLD_GRID_HALFSIZE));

	// Convert Fractional component from GridSpace
	XMVECTOR const xmFract(XMVectorNegate(SFM::sfract(xmOrigin)));  // FOR REALLY SMOOTH SCROLLING //

	XMStoreFloat3A(&oCamera.voxelFractionalGridOffset, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmFract)); // store fractional offset always negated and swizzled to x,z components
	
	XMStoreFloat3A(&oCamera.Origin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin)); // store origin always swizzled to x,z components

#ifndef NDEBUG
	static XMVECTOR DebugVariable;
	DebugVariable = XMLoadFloat3A(&oCamera.voxelFractionalGridOffset);
	setDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET, DebugVariable);
#endif
	
	// MOTION DELTA TRACKING //
	{
		oCamera.Motion = (zero_time_point != oCamera.tZoomStart) | (zero_time_point != oCamera.tRotateStart) | (zero_time_point != oCamera.tTranslateStart);
	}

	// test 
	{
		/*
		static tTime tLast(tNow);
		static v2_rotation_t vAngle;

		if (tNow - tLast > milliseconds(16)) {

			XMVECTOR xmDisplacement = XMVectorSet(0.1f, 0.1f, 0.0f, 0.0f);

			vAngle += fp_seconds(tNow - tLast).count();
			xmDisplacement = v2_rotate(xmDisplacement, vAngle);

			//xmDisplacement = XMVectorSetY(xmDisplacement, 0.0f);
			translateCamera(xmDisplacement, tDelta);

			tLast = tNow;
		}
		*/
		//rotateCamera(tDelta.count() * -4.0f); // *** temporary turntable rotation
	}

	if ((MinCity::isPaused() | _bCameraTurntable) && MinCity::isFocused()) {

		rotateCamera(time_to_float(fp_seconds(MinCity::critical_delta())));
	}
	return(xmOrigin);
}
void cVoxelWorld::setCameraTurnTable(bool const enable)
{
	_bCameraTurntable = enable;
}

void cVoxelWorld::OnKey(int32_t const key, bool const down, bool const ctrl)
{
	if (eInputEnabledBits::KEYS != (_inputEnabledBits & eInputEnabledBits::KEYS))
		return; // input disabled!

	switch (key)
	{
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
	case GLFW_KEY_S:
		if (!down) { // on released
			cMinCity::UserInterface->setActivatedTool(eTools::SELECT);
		}
		break;
	default: // further processing delegated to user interface and the tools it consists of
		cMinCity::UserInterface->KeyAction(key, down, ctrl);
		break;
	}
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
			Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
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
			Iso::Voxel const* const pVoxelHovered = world::getVoxelAt(voxelIndex);

			if (pVoxelHovered) {
				Iso::Voxel const oHoveredVoxel(*pVoxelHovered);

				// occlusion voxel not ground
				voxelIndex = _occlusion.occlusionVoxelIndex;
				Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

				if (pVoxel) {

					Iso::Voxel const oVoxel(*pVoxel);

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
				}

				_occlusion.tToOcclude -= delta();
				_occlusion.tToOcclude = fp_seconds(SFM::max(0.0, _occlusion.tToOcclude.count()));

				if (zero_time_duration == _occlusion.tToOcclude) {

					// adjust instances to faded
					setOcclusionInstances();

				}
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
		return(XMLoadFloat3A(&oCamera.Origin));
	}																										
	XMVECTOR const __vectorcall getFractionalOffset()
	{	// voxelFractionalGridOffset is always stored negated, so just add it!
		return(XMLoadFloat3A(&oCamera.voxelFractionalGridOffset));
	}
	v2_rotation_t const& getAzimuth()
	{
		return(oCamera.Azimuth);
	}
	
	static Iso::mini::Voxel const* const __restrict __vectorcall getMiniVoxelAt(point2D_t voxelIndex) // no bounds checking required! (bounds checking is done prior)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				return(*(the::temp + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)));
			}
		}

		return(nullptr);
	}
	static bool const __vectorcall setMiniVoxelAt(point2D_t voxelIndex, Iso::mini::Voxel const* const&& __restrict newData)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				// Update Voxel
				*(the::temp + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)) = std::move(newData);
				return(true);
			}
		}

		return(false);
	}

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	bool const __vectorcall isVoxelVisible(point2D_t const voxelIndex)
	{
		Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));
		if (pVoxel) {

			XMVECTOR xmLocation(p2D_to_v2(voxelIndex));
			xmLocation = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLocation);
			xmLocation = XMVectorSetY(xmLocation, -Iso::getRealHeight(*pVoxel));

			// need to transform grid space coordinates to world space coordinates
			xmLocation = XMVectorSubtract(xmLocation, world::getOrigin());

			return(Volumetric::VolumetricLink->Visibility.SphereTestFrustum<true>(xmLocation, Iso::VOX_RADIUS));
		}
		
		return(false);
	}
	// World Space Coordinates Only
	bool const __vectorcall isVoxelVisible(FXMVECTOR const xmLocation, float const voxelRadius)
	{
		return(Volumetric::VolumetricLink->Visibility.SphereTestFrustum<true>(xmLocation, voxelRadius));
	}

	bool const __vectorcall addVoxel(FXMVECTOR const xmLocation, point2D_t const voxelIndex, uint32_t const color, uint32_t const flags) // for external usage only. if inside cVoxelWorld.cpp/.h use the member of the volumetricQueue class instead.
	{
		XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmLocation, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

		[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
					   && XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
		{
			uvec4_v const uvIndex = SFM::floor_to_u32(XMVectorAdd(xmIndex, _mm_set1_ps(0.5f)));
			uvec4_t uiIndex;
			uvIndex.xyzw(uiIndex);

			size_t const index(mini::volume::get_index(uiIndex.x, uiIndex.y, uiIndex.z));

			if (!mini::bits->read_bit(index)) {  // filter out, if already set, nothing get added

				if (Volumetric::VolumetricLink->Visibility.SphereTestFrustum<true>(xmLocation, Iso::MINI_VOX_RADIUS)) { // visibility check //

					mini::bits->set_bit(index);

					auto const* const miniVoxel = getMiniVoxelAt(voxelIndex);
					setMiniVoxelAt(voxelIndex, std::forward<Iso::mini::Voxel const* const&&>(
						&(*mini::voxels.emplace_back(xmLocation, uvIndex, color, flags, miniVoxel))));

					return(true); // visible
				}
			}
		}

		return(false); // not visible
	}

	bool const __vectorcall addLight(point2D_t const voxelIndex, uint32_t const color, float const height)
	{
		Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));
		if (pVoxel) {

			XMVECTOR xmLocation(p2D_to_v2(voxelIndex));
			xmLocation = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLocation);
			xmLocation = XMVectorSetY(xmLocation, -(Iso::getRealHeight(*pVoxel) + height));

			// need to transform grid space coordinates to world space coordinates
			xmLocation = XMVectorSubtract(xmLocation, world::getOrigin());

			return(world::addVoxel(xmLocation, voxelIndex, color, Iso::mini::hidden | Iso::mini::emissive)); // light only   - flags this space as occupied, only one light can be added per space (minivoxel scale)
		}																									 //				 - *will only add light if coordinates are currently visible*
																											 // returns true (added light, visible), false (not added, not visible)

		return(false); // out of world bounds
	}


	namespace zoning
	{
		void zoneArea(rect2D_t voxelArea, uint32_t const zone_type) // supports zoning-zoning overwrite, excludes any area that is not strictly ground, 
																	// synchronizes residential, commercial, industrial area exclusivity.
																	// exclusivity is strictly maintained here, so the other zoning operations work without having to worry about it
		{															// as from this point on it's an impossible state to have bits set for the same voxel in more than one zoning type.
			// clamp to world/minmax coords
			voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

			// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
			voxelArea = r2D_add(voxelArea, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

			uint32_t const zoning(Iso::MASK_ZONING & (zone_type + 1));
			point2D_t voxelIterate;

			for (voxelIterate.y = voxelArea.top; voxelIterate.y < voxelArea.bottom; ++voxelIterate.y) {

				for (voxelIterate.x = voxelArea.left; voxelIterate.x < voxelArea.right; ++voxelIterate.x) {

					Iso::Voxel const* const __restrict pVoxel(world::getVoxelAtLocal(voxelIterate)); // pre-transformed to [0,0 to X,Y]

					if (pVoxel) {
						Iso::Voxel oVoxel(*pVoxel);

						if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel) ) { // only apply to ground area excluding the static & dynamic instances

							Iso::setZoning(oVoxel, zoning);
							Iso::setColor(oVoxel, world::ZONING_COLOR[zoning]);
							
							world::setVoxelAtLocal(voxelIterate, std::forward<Iso::Voxel const&&>(oVoxel));
						}
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
			voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

			// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
			voxelArea = r2D_add(voxelArea, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

			point2D_t voxelIterate;

			for (voxelIterate.y = voxelArea.top; voxelIterate.y < voxelArea.bottom; ++voxelIterate.y) {

				for (voxelIterate.x = voxelArea.left; voxelIterate.x < voxelArea.right; ++voxelIterate.x) {

					Iso::Voxel const* const __restrict pVoxel(world::getVoxelAtLocal(voxelIterate)); // pre-transformed to [0,0 to X,Y]

					if (pVoxel) {
						Iso::Voxel oVoxel(*pVoxel);

						if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) { // only apply to ground area excluding the static & dynamic instances

							Iso::clearZoning(oVoxel);
							Iso::clearColor(oVoxel);
							Iso::clearEmissive(oVoxel);
							
							world::setVoxelAtLocal(voxelIterate, std::forward<Iso::Voxel const&&>(oVoxel));
						}
					}
				}
			}
		}
	} // end ns

	Iso::Voxel const * const __restrict __vectorcall getVoxelAt(point2D_t voxelIndex)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				return((the::grid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)));
			}
		}

		return(nullptr);
	}
	Iso::Voxel const * const __restrict __vectorcall getVoxelAt(FXMVECTOR const Location)
	{	//          	equal same voxel	
		// still in Grid Space (-x,-y) to (X, Y) Coordinates 
		return(getVoxelAt(v2_to_p2D(Location)));
	}

	Iso::Voxel const* const __restrict __vectorcall getVoxelAtLocal(point2D_t const voxelIndex)
	{
		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				return((the::grid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)));
			}
		}

		return(nullptr);
	}

	uint32_t const getVoxelsAt_AverageHeight(rect2D_t voxelArea)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		uint32_t uiSumHeight(0), uiNumSamples(0);

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				Iso::Voxel const* const __restrict pVoxelTest = getVoxelAt(voxelIterate);
				if (pVoxelTest) {

					// for any voxel still has ground underneath
					uiSumHeight += Iso::getHeightStep(*pVoxelTest);
					++uiNumSamples;
				}
				++voxelIterate.x;
			}

			++voxelIterate.y;
		}

		return(SFM::round_to_u32((float)uiSumHeight / (float)uiNumSamples));
	}

	void setVoxelHeightAt(point2D_t const voxelIndex, uint32_t const heightstep)
	{
		Iso::Voxel const* const __restrict pVoxel(getVoxelAt(voxelIndex));

		if (pVoxel) {
			Iso::Voxel oVoxel(*pVoxel);

			Iso::setHeightStep(oVoxel, heightstep);

			setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		}
	}

	void setVoxelsHeightAt(rect2D_t voxelArea, uint32_t const heightstep)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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

	bool const __vectorcall setVoxelAt(point2D_t voxelIndex, Iso::Voxel const&& __restrict newData)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				// Update Voxel
				*(the::grid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)) = std::move(newData);
				return(true);
			}
		}

		return(false);
	}
	bool const __vectorcall setVoxelAt(FXMVECTOR const Location, Iso::Voxel const&& __restrict newData)
	{
		point2D_t const voxelIndex(v2_to_p2D(Location));		// this always floors, fractional part between 0.00001 and 0.99999 would
																																																			//          	equal same voxel	
		// still in Grid Space (-x,-y) to (X, Y) Coordinates 
		return(setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(newData)));
	}

	bool const __vectorcall setVoxelAtLocal(point2D_t const voxelIndex, Iso::Voxel const&& __restrict newData)
	{
		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				// Update Voxel
				*(the::grid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)) = std::move(newData);
				return(true);
			}
		}

		return(false);
	}

	void __vectorcall setVoxelsAt(rect2D_t voxelArea, Iso::Voxel const&& __restrict voxelReference)
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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
				p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					setVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
					Iso::Voxel const* const pVoxel = getVoxelAt(point2D_t(p.x, p0.y));
					if (pVoxel) {
						Iso::Voxel oVoxel(*pVoxel);
						if (!Iso::isRoad(oVoxel)) // not accidentally highlighting roads
						{
							Iso::setEmissive(oVoxel);
							setVoxelAt(point2D_t(p.x, p0.y), std::forward<Iso::Voxel const&&>(oVoxel));
						}
					}
#endif
#endif
				}

				// center 
				setVoxelHashAt<true>(p, hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
				Iso::Voxel const* const pVoxel = getVoxelAt(p);
				if (pVoxel) {
					Iso::Voxel oVoxel(*pVoxel);
					if (!Iso::isRoad(oVoxel)) // not accidentally highlighting roads
					{
						Iso::setEmissive(oVoxel);
						setVoxelAt(p, std::forward<Iso::Voxel const&&>(oVoxel));
					}
				}
#endif
#endif

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	static void __vectorcall clearVoxelsAt(rect2D_t voxelArea) // resets to "ground only"
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				Iso::Voxel const* const __restrict pVoxel(getVoxelAt(voxelIterate));

				if (pVoxel) {

					Iso::Voxel oVoxel(*pVoxel);

					// Reset everything EXCEPT [ Height, Occlusion, ... ]
					Iso::resetAsGroundOnly(oVoxel);

					setVoxelAt(voxelIterate, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}
				

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	void __vectorcall resetVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash) // static only
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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
				p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

				// back
				if (0 == (SFM::abs(p.y - p0.y) - 1)) {
					resetVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
					Iso::Voxel const* const pVoxel = getVoxelAt(point2D_t(p.x, p0.y));
					if (pVoxel) {
						Iso::Voxel oVoxel(*pVoxel);
						Iso::clearEmissive(oVoxel);
						setVoxelAt(point2D_t(p.x, p0.y), std::forward<Iso::Voxel const&&>(oVoxel));
					}
#endif
#endif
				}

				// center 
				resetVoxelHashAt<true>(p, hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
				Iso::Voxel const* const pVoxel = getVoxelAt(p);
				if (pVoxel) {
					Iso::Voxel oVoxel(*pVoxel);
					Iso::clearEmissive(oVoxel);
					setVoxelAt(p, std::forward<Iso::Voxel const&&>(oVoxel));
				}
#endif
#endif

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict getNeighbour(point2D_t voxelIndex, point2D_t const relativeOffset)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		point2D_t const voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

		// this function will also return the owning voxel
		// if zero,zero is passed in for the relative offset

		// Check bounds
		if ((voxelNeighbour.x | voxelNeighbour.y) >= 0) {

			if (voxelNeighbour.x < Iso::WORLD_GRID_SIZE && voxelNeighbour.y < Iso::WORLD_GRID_SIZE) {

				return((the::grid + ((voxelNeighbour.y * Iso::WORLD_GRID_SIZE) + voxelNeighbour.x)));
			}
		}

		return(nullptr);
	}
	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict getNeighbourLocal(point2D_t const voxelIndex, point2D_t const relativeOffset)
	{
		point2D_t const voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

		// this function will also return the owning voxel
		// if zero,zero is passed in for the relative offset

		// Check bounds
		if ((voxelNeighbour.x | voxelNeighbour.y) >= 0) {

			if (voxelNeighbour.x < Iso::WORLD_GRID_SIZE && voxelNeighbour.y < Iso::WORLD_GRID_SIZE) {

				return((the::grid + ((voxelNeighbour.y * Iso::WORLD_GRID_SIZE) + voxelNeighbour.x)));
			}
		}

		return(nullptr);
	}
	
	static void smoothRow(point2D_t start, int32_t width)  // iterates from start.x to start.x + width (Left 2 Right)
	{
		int32_t const yMin(start.y - 1), yMax(start.y + 1);

		while (--width >= 0) {

			uint32_t uiWidthSum(0);
			float fNumSamples(0.0f);

			Iso::Voxel oVoxel;
			{ // Middle //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(start);
				if (pVoxel) {
					oVoxel = *pVoxel;
					uiWidthSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
				else
					break;
			}

			{ // Left //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(point2D_t(start.x, yMin));
				if (pVoxel) {
					uiWidthSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
			}

			{ // Right //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(point2D_t(start.x, yMax));
				if (pVoxel) {
					uiWidthSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
			}

			Iso::setHeightStep(oVoxel, SFM::round_to_u32((float)uiWidthSum / fNumSamples));

			setVoxelAt(start, std::forward<Iso::Voxel const&& __restrict>(oVoxel));

			++start.x;
		}
	}
	static void smoothColumn(point2D_t start, int32_t height)  // iterates from start.y to start.y + height (Top 2 Bottom)
	{
		int32_t const xMin(start.x - 1), xMax(start.x + 1);

		while (--height >= 0) {

			uint32_t uiHeightSum(0);
			float fNumSamples(0.0f);

			Iso::Voxel oVoxel;
			{ // Middle //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(start);
				if (pVoxel) {
					oVoxel = *pVoxel;
					uiHeightSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
				else
					break;
			}

			{ // Left //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(point2D_t(xMin, start.y));
				if (pVoxel) {
					uiHeightSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
			}

			{ // Right //
				Iso::Voxel const* const __restrict pVoxel = getVoxelAt(point2D_t(xMax, start.y));
				if (pVoxel) {
					uiHeightSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
				}
			}

			Iso::setHeightStep(oVoxel, SFM::round_to_u32((float)uiHeightSum / fNumSamples));

			setVoxelAt(start, std::forward<Iso::Voxel const&& __restrict>(oVoxel));

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
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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

	void __vectorcall recomputeGroundAdjacency(rect2D_t voxelArea)
	{
		// adjust voxelArea to include a "border" around the passed in area of voxels, as they should be recomputed always
		voxelArea = voxelArea_grow(voxelArea, point2D_t(1, 1));

		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

		point2D_t voxelIterate(voxelArea.left_top());
		point2D_t const voxelEnd(voxelArea.right_bottom());

		while (voxelIterate.y <= voxelEnd.y) {

			voxelIterate.x = voxelArea.left;
			while (voxelIterate.x <= voxelEnd.x) {

				Iso::Voxel const* const __restrict pVoxelTest = getVoxelAt(voxelIterate);
				if (pVoxelTest) {

					Iso::Voxel oVoxel(*pVoxelTest);

					Iso::Voxel const* __restrict pNeighbour(nullptr);
					uint32_t const curVoxelHeightStep(Iso::getHeightStep(oVoxel));
					uint8_t Adjacency(0);

					// Therefore the neighbours of a voxel, follow same isometric layout/order //
					/*

											   NBR_TR
								   [NBR_T]				   [NBR_R]
						NBR_TL					VOXEL					NBR_BR
									[NBR_L]				   [NBR_B]
												NBR_BL
					*/

					// Side Back = NBR_T
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_T]);
					if (nullptr != pNeighbour) {
						uint32_t const neighbourHeightStep(Iso::getHeightStep(*pNeighbour));

						// adjacency
						if (neighbourHeightStep >= curVoxelHeightStep) {

							Adjacency |= (1 << Volumetric::adjacency::back);
						}
					}

					// Side Right = NBR_R
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_R]);
					if (nullptr != pNeighbour) {
						uint32_t const neighbourHeightStep(Iso::getHeightStep(*pNeighbour));

						// adjacency
						if (neighbourHeightStep >= curVoxelHeightStep) {

							Adjacency |= (1 << Volumetric::adjacency::right);
						}
					}

					// Side Left = NBR_L
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_L]);
					if (nullptr != pNeighbour) {
						uint32_t const neighbourHeightStep(Iso::getHeightStep(*pNeighbour));

						// adjacency
						if (neighbourHeightStep >= curVoxelHeightStep) {

							Adjacency |= (1 << Volumetric::adjacency::left);
						}
					}

					// Side Front = NBR_B
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_B]);
					if (nullptr != pNeighbour) {
						uint32_t const neighbourHeightStep(Iso::getHeightStep(*pNeighbour));

						// adjacency
						if (neighbourHeightStep >= curVoxelHeightStep) {

							Adjacency |= (1 << Volumetric::adjacency::front);
						}
					}

					if (0 == Adjacency) {
						Iso::clearAdjacency(oVoxel);
					}
					else {
						Iso::setAdjacency(oVoxel, Adjacency);
					}

					setVoxelAt(voxelIterate, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	void __vectorcall recomputeGroundAdjacency(point2D_t const voxelIndex)
	{
		recomputeGroundAdjacency(rect2D_t{ voxelIndex, voxelIndex });
	}

	// Random //
	point2D_t const __vectorcall getRandomVoxelIndexInArea(rect2D_t const area)
	{
		return(point2D_t(PsuedoRandomNumber32(area.left, area.right),
						 PsuedoRandomNumber32(area.top, area.bottom)));
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
			selected_area = r2D_min(selected_area, Iso::MAX_VOXEL_COORD);
			selected_area = r2D_max(selected_area, Iso::MIN_VOXEL_COORD);

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

	// ROADS -------------------------------------------------------------------------------------------------------------------------
	namespace roads
	{
		template<bool const bFindNodeOrEdge> // false = edge, true = node
		STATIC_INLINE bool const __vectorcall breadthFirstSearchRoad(rect2D_t const area, point2D_t const voxelIndexStart, point2D_t&& voxelIndexFound)
		{
			/*

											  [NBR_TR]
								   [NBR_T]				   [NBR_R]
						NBR_TL					VOXEL					NBR_BR
									NBR_L					NBR_B
												NBR_BL
			*/
			vector<point2D_t> history; // this is an optimization, rather than using std::unordered_set and finding "dups" every iteration
																							// just an iteratable vector to be used when search has finished to reset "discovered" state of any visited voxels
																							// algorithm complexity is linear in case of vector (no find (due to "discoverable" flag being used, clean up at end of search)
																							//						is exponential in case of unordered_set (find during search for every iteration + clean up at end of search)
			if (r2D_contains(area, voxelIndexStart)) { // flag root of search as disovered //
				Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(voxelIndexStart));
				if (pVoxel) {
					Iso::Voxel oVoxel(*pVoxel);

					Iso::setDiscovered(oVoxel);

					world::setVoxelAt(voxelIndexStart, std::forward<Iso::Voxel const&& __restrict>(oVoxel));

					// reserve memory - worst case scenario
					history.reserve(area.width() * area.height());
					history.emplace_back(voxelIndexStart);
				}
				else {
#ifndef NDEBUG
					FMT_LOG_FAIL(GAME_LOG, "Invalid search starting/root voxel index, out of bounds of grid, search cancelled!");
#endif
					return(false); // invalid search start voxelIndex, exception case - returns input vopxel index, representing no road node / edge found!
				}
			}
			else {
#ifndef NDEBUG
				FMT_LOG_FAIL(GAME_LOG, "Invalid search starting/root voxel index, out of bounds of area, search cancelled!");
#endif
				return(false); // invalid search start voxelIndex, exception case - returns input vopxel index, representing no road node / edge found!
			}

			std::queue<point2D_t> frontier;
			frontier.emplace(voxelIndexStart);

			bool bFound(false);
			point2D_t voxelIndex{};

			while (!frontier.empty())
			{
				voxelIndex = frontier.front();
				frontier.pop();

				{ // early exit test
					Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(voxelIndex));
					if (pVoxel) {
						Iso::Voxel const oVoxel(*pVoxel);

						if (Iso::isRoad(oVoxel)) {
							if constexpr (bFindNodeOrEdge) {  // node
								if (Iso::isRoadNode(*pVoxel)) {
									bFound = true;
									voxelIndexFound = voxelIndex;
									break;
								}
							}
							else {  // edge
								if (Iso::isRoadEdge(*pVoxel)) {
									bFound = true;
									voxelIndexFound = voxelIndex;
									break;
								}
							}
						}
					}
				}

				for (uint32_t neighbour = 0; neighbour < ADJACENT_NEIGHBOUR_COUNT; ++neighbour) {

					point2D_t const voxelNeighbour = p2D_add(voxelIndex, ADJACENT[neighbour]);

					if (r2D_contains(area, voxelNeighbour)) {

						Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(voxelNeighbour));
						if (pVoxel && !Iso::isDiscovered(*pVoxel)) {

							Iso::Voxel oVoxel(*pVoxel);

							Iso::setDiscovered(oVoxel);

							world::setVoxelAt(voxelNeighbour, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
							history.emplace_back(voxelNeighbour);

							frontier.emplace(voxelNeighbour);
						}
					}
				}
			}

			// reset all visited voxels back to "undiscovered"
			size_t history_count(history.size() + 1);
			point2D_t const* __restrict pVoxelIndexReset = history.data();
			while (0 != --history_count) {
				point2D_t const voxelIndexReset(*pVoxelIndexReset);

				Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(voxelIndexReset));
				if (pVoxel) {
					Iso::Voxel oVoxel(*pVoxel);

					Iso::clearDiscovered(oVoxel);

					world::setVoxelAt(voxelIndexReset, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}

				++pVoxelIndexReset;
			}

			return(bFound);
		}

		bool const __vectorcall searchForClosestRoadNode(rect2D_t const area, point2D_t const voxelIndexStart, point2D_t&& voxelIndexFound)
		{
			return(breadthFirstSearchRoad<true>(area, voxelIndexStart, std::forward<point2D_t&&>(voxelIndexFound)));
		}
		bool const __vectorcall searchForClosestRoadEdge(rect2D_t const area, point2D_t const voxelIndexStart, point2D_t&& voxelIndexFound)
		{
			return(breadthFirstSearchRoad<false>(area, voxelIndexStart, std::forward<point2D_t&&>(voxelIndexFound)));
		}

		bool const directions_are_perpindicular(uint32_t const encoded_direction_a, uint32_t const encoded_direction_b)
		{
			if (encoded_direction_a != encoded_direction_b) {
				switch (encoded_direction_a)
				{
				case Iso::ROAD_DIRECTION::N:
					return(Iso::ROAD_DIRECTION::S != encoded_direction_b);
				case Iso::ROAD_DIRECTION::S:
					return(Iso::ROAD_DIRECTION::N != encoded_direction_b);
				case Iso::ROAD_DIRECTION::E:
					return(Iso::ROAD_DIRECTION::W != encoded_direction_b);
				case Iso::ROAD_DIRECTION::W:
					return(Iso::ROAD_DIRECTION::E != encoded_direction_b);
				} // end switch
			}

			return(false);
		}

		bool const __vectorcall search_point_for_road(point2D_t const origin)
		{
			Iso::Voxel const* const pVoxel = world::getVoxelAt(origin);
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

				return(Iso::isRoad(oVoxel));
			}
			return(false);
		}

		bool const __vectorcall search_neighbour_for_road(point2D_t& __restrict found_road_point, point2D_t const origin, point2D_t const offset)
		{
			Iso::Voxel const* const pVoxel = world::getNeighbour(origin, offset);
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isRoad(oVoxel)) {

					found_road_point = p2D_add(origin, offset);
					return(true);
				}
			}
			return(false);
		}
		bool const __vectorcall search_neighbour_for_road(point2D_t const origin, point2D_t const offset)
		{
			Iso::Voxel const* const pVoxel = world::getNeighbour(origin, offset);
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

				return(Iso::isRoad(oVoxel));
			}
			return(false);
		}

		point2D_t const __vectorcall search_road_intersect(point2D_t const origin, point2D_t const axis,
														   int32_t const begin, int32_t const end) // begin / end are offsets from origin
		{
			point2D_t found_road_point(0);
			for (int32_t i = begin; i != end; ++i) {

				if (world::roads::search_neighbour_for_road(found_road_point, origin, p2D_muls(axis, i))) {

					return(found_road_point);
				}
			}

			return(point2D_t(0));
		}

	} // end ns world::roads

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

namespace // private to this file (anonymous)
{
	constinit static struct voxelRender // ** static container, all methods and members must be static inline ** //
	{
		constinit static struct
		{
			uint32_t road_tiles_visible = 0;

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
			size_t	numDynamicVoxelsRendered = 0,
					numStaticVoxelsRendered = 0,
					numTerrainVoxelsRendered = 0;

			size_t  numLightVoxelsRendered = 0;
#endif
#endif
		} inline render_state{};

		// this construct significantly improves throughput of voxels, by batching the streaming stores //
		// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)
		using miniVoxelLocalBatch = sBatched<VertexDecl::VoxelDynamic, eStreamingBatchSize::GROUND>;
		using miniVoxelThreadBatch = tbb::enumerable_thread_specific<
			miniVoxelLocalBatch,
			tbb::cache_aligned_allocator<miniVoxelLocalBatch>,
			tbb::ets_key_per_instance >;

		static inline miniVoxelThreadBatch batchedMini, batchedMiniTrans;

		STATIC_INLINE_PURE uint32_t const __vectorcall encode_adjacency(uvec4_v const xmIndex)
		{
			ivec4_t iIndex;
			ivec4_v(xmIndex).xyzw(iIndex);

			//BETTER_ENUM(adjacency, uint32_t const,  // matching the same values to voxelModel.h values
			//	left = voxB::BIT_ADJ_LEFT,
			//	right = voxB::BIT_ADJ_RIGHT,
			//	front = voxB::BIT_ADJ_FRONT,
			//	back = voxB::BIT_ADJ_BACK,
			//	above = voxB::BIT_ADJ_ABOVE

			uint32_t adjacent(0);

			if (iIndex.x - 1 >= 0) {
				adjacent |= mini::bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z) << Volumetric::adjacency::left;
			}
			if (iIndex.x + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X) {
				adjacent |= mini::bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z) << Volumetric::adjacency::right;
			}
			if (iIndex.z - 1 >= 0) {
				adjacent |= mini::bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1) << Volumetric::adjacency::front;
			}
			if (iIndex.z + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z) {
				adjacent |= mini::bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1) << Volumetric::adjacency::back;
			}
			if (iIndex.y + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y) {
				adjacent |= mini::bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z) << Volumetric::adjacency::above;
			}
			return(adjacent);
		}

		STATIC_INLINE void XM_CALLCONV RenderMiniVoxel(point2D_t const& __restrict voxelIndex,
			Iso::mini::Voxel const&& __restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans,
			miniVoxelLocalBatch& __restrict localMini,
			miniVoxelLocalBatch& __restrict localMiniTrans)
		{
			// required for voxels w/light, voxels w/o light & lights (hidden voxel)
			XMVECTOR const xmLocation(XMLoadFloat3A(&oVoxel.position));
			XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmLocation, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));
			uint32_t const alpha_color(oVoxel.index.w);

			bool const emissive(Iso::mini::emissive == ((oVoxel.flags & Iso::mini::emissive) & (bool)alpha_color)); // if color is true black it's not emissive

			if (Iso::mini::hidden != (oVoxel.flags & Iso::mini::hidden)) {

				uint8_t const alpha((alpha_color >> 24) & 0xFF);
				bool const transparent(0xFF != alpha);

				uint32_t const adjacency(encode_adjacency(uvec4_v(oVoxel.index)));

				// Build hash //
				uint32_t hash(0);
				// ** see uniforms.vert for definition of constants used here **

				hash |= adjacency;									//           0000 0000 0001 1111
				hash |= (emissive << 5);							//           0000 0000 001x xxxx
				// unsupported hash |= (voxel.Metallic << 6);		// 0000 0000 0000 xxxx x1xx xxxx
				// unsupported hash |= (voxel.Roughness << 8);		// 0000 0000 0000 1111 Uxxx xxxx
				hash |= (uint32_t(alpha >> 6) << 13);				// 0000 0000 011U xxxx xxxx xxxx

				XMVECTOR const xmUV(XMVectorScale(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmIndex), Iso::INVERSE_WORLD_GRID_FSIZE)); // in format xzy (optional, now that us are deried soley in shader
				XMVECTOR const xmUVs(XMVectorSetW(xmUV, (float)(alpha_color & 0x00FFFFFF)/*remove alpha*/)); // srgb is passed to vertex shader which converts it to linear; which is faster than here with cpu

				if (transparent) {

					localMiniTrans.emplace_back(
						voxels_trans,

						xmLocation, xmUVs, XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f), hash
					);
				}
				else {

					localMini.emplace_back(
						voxels_dynamic,

						xmLocation, xmUVs, XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f), hash
					);
				}

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
				++render_state.numDynamicVoxelsRendered;
#endif
#endif
			}

			// if voxel is hidden then its just a light
			if (emissive) {

				Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, alpha_color & 0x00FFFFFF);

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
				++render_state.numLightVoxelsRendered;
#endif
#endif
			}
		}
		STATIC_INLINE void XM_CALLCONV RenderMiniVoxels(point2D_t const& __restrict voxelIndex,
			Iso::mini::Voxel const&& __restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans,
			miniVoxelLocalBatch& __restrict localMini,
			miniVoxelLocalBatch& __restrict localMiniTrans)
		{
			RenderMiniVoxel(voxelIndex, std::forward<Iso::mini::Voxel const&& __restrict>(oVoxel), voxels_dynamic, voxels_trans, localMini, localMiniTrans);

			static constexpr uint32_t const MAX_COUNT(Iso::SCREEN_VOXELS_Y);
			
			uint32_t count(MAX_COUNT);
			Iso::mini::Voxel const* next(oVoxel.next);
			while (next && 0 != --count) {

				RenderMiniVoxel(voxelIndex, std::forward<Iso::mini::Voxel const&& __restrict>(*next), voxels_dynamic, voxels_trans, localMini, localMiniTrans);

				next = next->next;
			}

#ifndef NDEBUG
			[[unlikely]] if (0 == count) {
				FMT_LOG_WARN(VOX_LOG, "Performance issue. Too many minivoxels @ ( {:d} , {:d} )", voxelIndex.x, voxelIndex.y);
			}
#endif
		}

		// this construct significantly improves throughput of voxels, by batching the streaming stores //
		// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)
		using VoxelLocalBatch = sBatched<VertexDecl::VoxelNormal, eStreamingBatchSize::GROUND>;
		using VoxelThreadBatch = tbb::enumerable_thread_specific<
			VoxelLocalBatch,
			tbb::cache_aligned_allocator<VoxelLocalBatch>,
			tbb::ets_key_per_instance >;

		static inline VoxelThreadBatch batchedGround, batchedRoad, batchedRoadTrans;


		STATIC_INLINE_PURE void XM_CALLCONV RenderGround(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
			Iso::Voxel const& __restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelGround,
			VoxelLocalBatch& __restrict localGround)
		{
			XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmVoxelOrigin, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));
			
			[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						   && XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
			{
				// **** HASH FORMAT 32bits available //
				uint32_t const color(0x00FFFFFF & Iso::getColor(oVoxel));
				bool const emissive(Iso::isEmissive(oVoxel) & (bool)color); // if color is true black it's not emissive
								
				// Build hash //
				uint32_t groundHash(0);
				groundHash |= Iso::getAdjacency(oVoxel);			//				0001 1111
																	//				RRRx xxxx
				groundHash |= (emissive << 8);						// 0000 0001	xxxx xxxx
				groundHash |= (Iso::getHeightStep(oVoxel) << 12);	// 1111 RRRx	xxxx xxxx

				XMVECTOR xmUVs(XMVectorScale(p2D_to_v2(voxelIndex), Iso::INVERSE_WORLD_GRID_FSIZE));
				
				xmUVs = XMVectorSetW(xmUVs, ((float)color));

				localGround.emplace_back(
					voxelGround,

					xmVoxelOrigin,
					xmUVs,
					groundHash
				);

				if (emissive) {

					// add light, offset to the height of this ground voxel
					Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(XMVectorSetY(xmIndex, Iso::getRealHeight(oVoxel) * 2.0f), color); // cheating on transform, so its on the top effectively applies proper height for light
				}
#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
				++render_state.numTerrainVoxelsRendered;
#endif
#endif
			}
		}


		STATIC_INLINE void XM_CALLCONV RenderRoad(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
			Iso::Voxel const& __restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoad,
			VoxelLocalBatch& __restrict localRoad)
		{
			XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmVoxelOrigin, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

			[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						   && XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
			{
				// **** HASH FORMAT 32bits available //

				// Build hash //
				uint32_t roadHash(0);
				// not used roadHash |= Iso::getHeightStep(oVoxel);									//			     	                      000R 1111   // msb is reserved (5th bit)
				// not used roadHash |= (Iso::getOcclusion(oVoxel) << 5);							//		     		                      111x xxxx
				roadHash |= (Iso::isEmissive(oVoxel) << 8);											//                           0000 0001	  xxxx xxxx
				roadHash |= (uint32_t(Iso::getHash(oVoxel, Iso::GROUND_HASH)) << 9);				// 0000 0001    1111 1111    1111 111x    xxxx xxxx 

				XMVECTOR const xmUVs(XMVectorScale(p2D_to_v2(voxelIndex), Iso::INVERSE_WORLD_GRID_FSIZE));

				localRoad.emplace_back(
					voxelRoad,

					xmVoxelOrigin,
					xmUVs,
					roadHash
				);

				++render_state.road_tiles_visible; // *this state is only valid when RenderGrid() is complete.
			}
		}


		template<bool const Dynamic>
		STATIC_INLINE bool const XM_CALLCONV RenderModel(uint32_t const index, FXMVECTOR xmVoxelOrigin, point2D_t const& __restrict voxelIndex,
			Iso::Voxel const& __restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans)
		{
			bool bVisible(true);

			auto const ModelInstanceHash = Iso::getHash(oVoxel, index);
			auto const FoundModelInstance = MinCity::VoxelWorld->lookupVoxelModelInstance<Dynamic>(ModelInstanceHash);

			if (FoundModelInstance) {

				bool const emission_only(FoundModelInstance->isEmissionOnly());

				if (!Volumetric::VolumetricLink->Visibility.AABBTestFrustum(xmVoxelOrigin, XMLoadFloat3A(&FoundModelInstance->getModel()._Extents))) {
					FoundModelInstance->setEmissionOnly(true); // lighting from instance is still "rendered/added to light buffer" but no voxels are rendered.
					bVisible = false; // voxels of model are not rendered. it is not currently visible. the light emitted from the model may still be visible - so the ^^^^above is done.
				}

				FoundModelInstance->Render(xmVoxelOrigin, voxelIndex, oVoxel, voxels_static, voxels_dynamic, voxels_trans);

				FoundModelInstance->setEmissionOnly(emission_only); // restore temporary state change

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

		template<uint32_t const MAX_VISIBLE_X, uint32_t const MAX_VISIBLE_Y>
		static void XM_CALLCONV RenderGrid(point2D_t const voxelStart,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelGround,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoad,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoadTrans,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelStatic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelTrans)
		{
			render_state.road_tiles_visible = 0; // reset - visible road tile count very useful eg.) adding cars //

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
			render_state.numDynamicVoxelsRendered = 0,
			render_state.numStaticVoxelsRendered = 0,
			render_state.numTerrainVoxelsRendered = 0;
			render_state.numLightVoxelsRendered = 0;
#endif
#endif
			point2D_t voxelReset(p2D_add(voxelStart, Iso::GRID_OFFSET));

			// Traverse Grid
			point2D_t iterations(MAX_VISIBLE_X, MAX_VISIBLE_Y);

#ifdef DEBUG_TEST_FRONT_TO_BACK
			static uint32_t lines_missing = MAX_VISIBLE_Y - 1;
			{
				static tTime tLast;

				if (0 != lines_missing) {
					tTime tNow = high_resolution_clock::now();
					if (tNow - tLast > milliseconds(50)) {
						--lines_missing;
						tLast = tNow;
					}
				}
				else {
					lines_missing = MAX_VISIBLE_Y - 1;
				}
			}
#endif

			if ((voxelReset.y < 0)) {
				iterations.y += voxelReset.y;
				voxelReset.y = 0;
			}

			if ((voxelReset.x < 0)) {
				iterations.x += voxelReset.x;
				voxelReset.x = 0;
			}

			point2D_t const voxelEnd = p2D_min(p2D_add(voxelReset, iterations), point2D_t(Iso::WORLD_GRID_SIZE));

#ifdef DEBUG_TEST_FRONT_TO_BACK
			voxelEnd.y -= lines_missing;
#endif
			typedef struct no_vtable sRenderFuncBlockChunk {

			private:
				point2D_t const 								voxelStart;
				Iso::Voxel const* const __restrict				theGrid;
				Iso::mini::Voxel const** const __restrict		theTemp;

				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict	voxelGround;
				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict	voxelRoad;
				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict	voxelRoadTrans;
				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict	voxelStatic;
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict	voxelDynamic;
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict	voxelTrans;
				
				sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
			public:
				__forceinline explicit __vectorcall sRenderFuncBlockChunk(
					point2D_t const voxelStart_,
					Iso::Voxel const* const __restrict theGrid_,
					Iso::mini::Voxel const** const __restrict theTemp_,
					tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelGround_,
					tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoad_,
					tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoadTrans_,
					tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelStatic_,
					tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic_,
					tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelTrans_)
					: voxelStart(voxelStart_), theGrid(theGrid_), theTemp(theTemp_),
					voxelGround(voxelGround_), voxelRoad(voxelRoad_), voxelRoadTrans(voxelRoadTrans_), voxelStatic(voxelStatic_), voxelDynamic(voxelDynamic_), voxelTrans(voxelTrans_)
				{}

				void __vectorcall operator()(tbb::blocked_range2d<int32_t, int32_t> const& r) const {

					static constexpr size_t const WORLD_GRID_SIZE_BITS = size_t(Iso::WORLD_GRID_SIZE_BITS);

					miniVoxelLocalBatch& __restrict localMini(batchedMini.local());
					miniVoxelLocalBatch& __restrict localMiniTrans(batchedMiniTrans.local());

					VoxelLocalBatch& __restrict localGround(batchedGround.local());
					VoxelLocalBatch& __restrict localRoad(batchedRoad.local());
					VoxelLocalBatch& __restrict localRoadTrans(batchedRoadTrans.local());

					int32_t const	// pull out into registers from memory
						y_begin(r.rows().begin()),
						y_end(r.rows().end()),
						x_begin(r.cols().begin()),
						x_end(r.cols().end());

					point2D_t voxelIndex; // *** range is [0...WORLD_GRID_SIZE] for voxelIndex here *** //

					for (voxelIndex.y = y_begin; voxelIndex.y < y_end; ++voxelIndex.y) 
					{
						voxelIndex.x = x_begin;

						Iso::Voxel const* __restrict pVoxel = theGrid + ((voxelIndex.y << WORLD_GRID_SIZE_BITS) + voxelIndex.x);
						_mm_prefetch((const CHAR*)pVoxel, _MM_HINT_T0);

						Iso::mini::Voxel const** __restrict pMiniVoxels = theTemp + ((voxelIndex.y << WORLD_GRID_SIZE_BITS) + voxelIndex.x);

						for (; voxelIndex.x < x_end; ++voxelIndex.x)
						{
							// Make index (row,col)relative to starting index voxel
							// calculate origin from relative row,col
							// Add the offset of the world origin calculated	// *bugfix - this trickles down thru the voxel output position, to uv's, to light emitter position in the lightmap. All is the same. this is the 1st place the fractional offset is used (removal). 2nd is in uniforms.vert
							XMVECTOR const xmVoxelOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(p2D_sub(voxelIndex, voxelStart)))); // make relative to render start position
							bool bRenderVisible(false);

							// *bugfix: Rendering is FRONT to BACK only (roughly), to optimize usage of visibility checking, and get more correct visibility. (less pop-in) //
							Iso::Voxel const oVoxel(*pVoxel);
							++pVoxel; // sequentially access for best cache prediction

							if (Iso::isOwner(oVoxel, Iso::STATIC_HASH))	// only roots actually do rendering work. skip all children
							{
#ifndef DEBUG_NO_RENDER_STATIC_MODELS
								bRenderVisible |= RenderModel<false>(Iso::STATIC_HASH, xmVoxelOrigin, voxelIndex, oVoxel, voxelStatic, voxelDynamic, voxelTrans);
#endif
							} // root
							// a voxel in the grid can have a static model and dynamic model simultaneously
							if (Iso::isOwnerAny(oVoxel, Iso::DYNAMIC_HASH)) { // only if there are dynamic hashes which this voxel owns
								for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
									if (Iso::isOwner(oVoxel, i)) {

										bRenderVisible |= RenderModel<true>(i, xmVoxelOrigin, voxelIndex, oVoxel, voxelStatic, voxelDynamic, voxelTrans);
									}
								}
							}

							if (Iso::isOwner(oVoxel, Iso::GROUND_HASH) && isExtended(oVoxel))
							{
								switch (getExtendedType(oVoxel))
								{
								case Iso::EXTENDED_TYPE_ROAD:

									if (bRenderVisible || (bRenderVisible |= Volumetric::VolumetricLink->Visibility.SphereTestFrustum<true>(xmVoxelOrigin, Iso::VOX_RADIUS * Iso::ROAD_SEGMENT_RADIUS))) { // if already visible will skip frustum check
										if (Iso::isEmissive(oVoxel)) { // any emissive road is transparent
											RenderRoad(xmVoxelOrigin, voxelIndex, oVoxel, voxelRoadTrans, localRoadTrans); // trans road
										}
										RenderRoad(xmVoxelOrigin, voxelIndex, oVoxel, voxelRoad, localRoad); // opaque road
									}
									break;
								case Iso::EXTENDED_TYPE_WATER:
									// todo
									break;
									// default should not exist //
								}
							} // extended

							// ***Ground always exists*** // *bugfix: frustum culling ground leaves empty space in an undefined state. for example, raymarched reflections disappear if camera is too close *do not change*
							RenderGround(xmVoxelOrigin, voxelIndex, oVoxel, voxelGround, localGround);

							Iso::mini::Voxel const* const pMiniVoxel(*pMiniVoxels);
							
							if (nullptr != pMiniVoxel) { // mini-voxels, if they exist for this grid cell, must be rendered. The minivoxels are dynamic and cleared / added every frame. The frustum check happens at a different location (addVoxel). All added mini voxels have already benn frustum checked.
								RenderMiniVoxels(voxelIndex, std::forward<Iso::mini::Voxel const&& __restrict>(*pMiniVoxel), voxelDynamic, voxelTrans, localMini, localMiniTrans);
								// clear minivoxel after render is complete - saves clearing the whole grid - only clears what is used!
								const_cast<Iso::mini::Voxel* const>(pMiniVoxel)->next = nullptr; // *bugfix - required to reset linked list, the first next pointer from head of list is set to nullptr, which removes all child elements.
								*pMiniVoxels = nullptr; // clears a pointer to the data, no need to clear the actual data
							}

							++pMiniVoxels; // sequentially access for best cache prediction
						} // for

					} // for

				} // operation

			} const RenderFuncBlockChunk;

			// *****************************************************************************************************************//
																																
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION																					
			tTime const tGridStart(high_resolution_clock::now());																
#endif																														
			{
				tbb::queuing_rw_mutex::scoped_lock lock(the::lock, false); // read-only grid access

				tbb::auto_partitioner part; /*load balancing - do NOT change - adapts to variance of whats in the voxel grid*/
				tbb::parallel_for(tbb::blocked_range2d<int32_t, int32_t>(voxelReset.y, voxelEnd.y, eThreadBatchGrainSize::GRID_RENDER_2D,
					voxelReset.x, voxelEnd.x, eThreadBatchGrainSize::GRID_RENDER_2D), // **critical loop** // debug will slow down to 100ms+ / frame if not parallel //
					RenderFuncBlockChunk(voxelStart, the::grid, the::temp, voxelGround, voxelRoad, voxelRoadTrans, voxelStatic, voxelDynamic, voxelTrans), part
				);
			}
			// ####################################################################################################################
			// ensure all batches are output and cleared for next trip
			for (auto i = batchedMini.begin(); i != batchedMini.end(); ++i) {
				i->out(voxelDynamic);
			}
			batchedMini.clear();
			for (auto i = batchedMiniTrans.begin(); i != batchedMiniTrans.end(); ++i) {
				i->out(voxelTrans);
			}
			batchedMiniTrans.clear();

			for (auto i = batchedGround.begin(); i != batchedGround.end(); ++i) {
				i->out(voxelGround);
			}
			batchedGround.clear();
			for (auto i = batchedRoad.begin(); i != batchedRoad.end(); ++i) {
				i->out(voxelRoad);
			}
			batchedRoad.clear();
			for (auto i = batchedRoadTrans.begin(); i != batchedRoadTrans.end(); ++i) {
				i->out(voxelRoadTrans);
			}
			batchedRoadTrans.clear();
			// ####################################################################################################################

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			PerformanceResult& result(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
			result.grid_duration = high_resolution_clock::now() - tGridStart;
			++result.grid_count;
#endif
		}
	} inline voxelRender; // end struct voxelRender
} // end ns

template<typename VertexDeclaration>
struct voxelBuffer {
	vku::double_buffer<vku::GenericBuffer> stagingBuffer;
	inline static constexpr VertexDeclaration const type;

	constexpr voxelBuffer() = default; // allow constinit optimization.

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
		struct {
			voxelBuffer<VertexDecl::VoxelDynamic>
				opaque;
			voxelBuffer<VertexDecl::VoxelDynamic>
				trans;

		} visibleDynamic;

		voxelBuffer<VertexDecl::VoxelNormal>
			visibleStatic;

		struct {
			voxelBuffer<VertexDecl::VoxelNormal>
				opaque;
			voxelBuffer<VertexDecl::VoxelNormal>
				trans;
		} visibleRoad;

		voxelBuffer<VertexDecl::VoxelNormal>
			visibleTerrain;

	} voxels;
}

namespace Volumetric
{
	constinit inline voxLink* VolumetricLink{ nullptr };
}
namespace world
{
	cVoxelWorld::cVoxelWorld()
		:
		_lastState{}, _currentState{}, _targetState{}, _occlusion{}, _sim(nullptr),
		_vMouse{}, _lastOcclusionQueryValid(false), _inputEnabledBits{},
		_terrainTempImage(nullptr),
		_onLoadedRequired(true),
		_terrainTexture(nullptr), _gridTexture(nullptr), _roadTexture(nullptr), _blackbodyTexture(nullptr),
		_blackbodyImage(nullptr), _tBorderScroll(Iso::CAMERA_SCROLL_DELAY),
		_sequence(GenerateVanDerCoruptSequence<30, 2>()),
		_activeRain(nullptr),
		_AsyncClearTaskID(0)
#ifdef DEBUG_STORAGE_BUFFER
		, DebugStorageBuffer(nullptr)
#endif
	{
		Volumetric::VolumetricLink = new Volumetric::voxLink{ *this, _OpacityMap, _Visibility };
		_occlusion.tToOcclude = Volumetric::Konstants::OCCLUSION_DELAY;
	}

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
		
		// other textures:
		MinCity::TextureBoy->LoadKTXTexture<true>(_roadTexture, TEXTURE_DIR L"road_array.ktx");  // remeber for loading straight from ktx, colorspace is defined by file. In this case the file should have been saved srgb.
		MinCity::TextureBoy->AddTextureToTextureArray(_roadTexture, TEX_ROAD);

		Imaging const blackbodyImage(ImagingLoadRawBGRA(TEXTURE_DIR "blackbody.data", BLACKBODY_IMAGE_WIDTH, 1));
		MinCity::TextureBoy->ImagingToTexture<false>(blackbodyImage, _blackbodyTexture);
		MinCity::TextureBoy->AddTextureToTextureArray(_blackbodyTexture, TEX_BLACKBODY);

		_blackbodyImage = blackbodyImage; // save image to be used for blackbody radiation light color lookup

		// other textures, not part of common texture array:

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

		// create world grid memory
		{
			the::grid._protected = (Iso::Voxel* const __restrict)scalable_aligned_malloc(sizeof(Iso::Voxel) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE, CACHE_LINE_BYTES);
			the::temp._protected = (Iso::mini::Voxel const** const __restrict)scalable_aligned_malloc(sizeof(Iso::mini::Voxel const* const) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE, CACHE_LINE_BYTES);
			 
			memset(the::grid._protected, 0, sizeof(Iso::Voxel) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
			memset(the::temp._protected, 0, sizeof(Iso::mini::Voxel const*) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
			
			mini::voxels.reserve(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
			using volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;
			mini::bits = volume::create();
			clearMiniVoxels(true);

			FMT_LOG(VOX_LOG, "world grid voxel allocation: {:n} bytes", (size_t)(sizeof(the::grid[0]) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE));
			FMT_LOG(VOX_LOG, "mini grid voxel allocation: {:n} bytes", (size_t)(sizeof(the::temp[0]) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE));
		}
		
		GenerateGround();
#ifndef NDEBUG
		OutputVoxelStats();
#endif
		_OpacityMap.create(MinCity::Vulkan->getDevice(), MinCity::Vulkan->computePool(0), MinCity::Vulkan->computeQueue(0), MinCity::getFramebufferSize(), MinCity::hardware_concurrency());
		MinCity::PostProcess->create(MinCity::Vulkan->getDevice(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue(), MinCity::getFramebufferSize());
		createAllBuffers(MinCity::Vulkan->getDevice(), MinCity::Vulkan->transientPool(), MinCity::Vulkan->graphicsQueue());
		
		Volumetric::LoadAllVoxelModels();

		if (!Volumetric::isNewModelQueueEmpty()) {
			MinCity::DispatchEvent(eEvent::SHOW_IMPORT_WINDOW);
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
		// safe to mix dynamic and static //
		switch (gameobject_type)
		{
		case types::game_object_t::BuildingGameObject:
			cBuildingGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]);
			break;
		case types::game_object_t::TestGameObject:
			cTestGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]);
			break;
		case types::game_object_t::TrafficSignGameObject:
			cTrafficSignGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]);
			break;
		case types::game_object_t::TrafficControlGameObject:
			cTrafficControlGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]);
			break;
		case types::game_object_t::SignageGameObject:
			cSignageGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]);
			break;
		case types::game_object_t::LightGameObject:
			cLightGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]);
			break;
		}

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
							pInstance->setElevation(data_models_static[i].elevation);
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
							pInstance->setElevation(data_models_dynamic[i].elevation);
							pInstance->setPitch(v2_rotation_t(data_models_dynamic[i].pitch.x, data_models_dynamic[i].pitch.y, data_models_dynamic[i].pitch.z));
							pInstance->setLocationAzimuth(XMLoadFloat2(&data_models_dynamic[i].location), v2_rotation_t(data_models_dynamic[i].azimuth.x, data_models_dynamic[i].azimuth.y, data_models_dynamic[i].azimuth.z));
						}
					}

				}
			}
		}


	}

	// private specializations of placeXXXInstanceAt
	cCopterGameObject* const cVoxelWorld::placeCopterInstanceAt(point2D_t const voxelIndex)
	{
		// body
		auto const [hash_parent, instance_parent] = placeVoxelModelInstanceAt<Volumetric::eVoxelModels_Dynamic::MISC>(voxelIndex, Volumetric::eVoxelModel::DYNAMIC::MISC::COPTER_BODY, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE);

		if (instance_parent) {

			auto const [hash_prop, instance_prop] = placeVoxelModelInstanceAt<Volumetric::eVoxelModels_Dynamic::MISC>(voxelIndex, Volumetric::eVoxelModel::DYNAMIC::MISC::COPTER_PROP, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE | Volumetric::eVoxelModelInstanceFlags::CHILD_ONLY);

			if (instance_prop) {
				instance_parent->setChild(instance_prop);

				return(&cCopterGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash_parent]));
			}
		}

#ifndef NDEBUG
		{
			FMT_LOG_WARN(VOX_LOG, "placeCopterInstanceAt<{:s}> failed. modelIndex({:d}) of modelGroup({:d}) at voxelIndex({:d},{:d})",
				"dynamic", Volumetric::eVoxelModel::DYNAMIC::MISC::COPTER_BODY, Volumetric::eVoxelModels_Dynamic::MISC, voxelIndex.x, voxelIndex.y);
		}
#endif
		return(nullptr);
	}

#ifdef GIF_MODE
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_guitarer(XMVECTOR xmLocation, v2_rotation_t vAzimuth, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
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

			vAzimuth = v2_rotation_t(REFERENCE_ANGLE + swing);
		}
		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vAzimuth));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_singer(XMVECTOR xmLocation, v2_rotation_t vAzimuth, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
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
		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vAzimuth));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_keyboarder(XMVECTOR xmLocation, v2_rotation_t vAzimuth, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
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

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vAzimuth));
	}
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_drummer(XMVECTOR xmLocation, v2_rotation_t vAzimuth, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
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

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vAzimuth));
	}
	
	static std::pair<XMVECTOR const, v2_rotation_t const> const do_update_crowd(XMVECTOR xmLocation, v2_rotation_t vAzimuth, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, uint32_t const hash)
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

		return(std::pair<XMVECTOR const, v2_rotation_t const>(xmLocation, vAzimuth));
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
		cImportGameObject_Dynamic::clear();
		cImportGameObject_Static::clear();
		cBuildingGameObject::clear();
		cCarGameObject::clear();
		cPoliceCarGameObject::clear();
		cCopterPropGameObject::clear();
		cCopterBodyGameObject::clear();
		cRemoteUpdateGameObject::clear();
		cTestGameObject::clear();
		cTrafficControlGameObject::clear();
		cTrafficSignGameObject::clear();
		cSignageGameObject::clear();

		// reset world / camera *bugfix important
		resetCamera();
	}
	void cVoxelWorld::NewWorld()
	{
		Clear();
		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(10));

		GenerateGround(); // generate new ground
		
		// must be last
		_onLoadedRequired = true; // trigger onloaded() inside Update of VoxelWorld
	}

	void cVoxelWorld::OnLoaded(tTime const& __restrict tNow)
	{
		if (_terrainTempImage) {
			MinCity::TextureBoy->ImagingToTexture_BC7<false>(_terrainTempImage, _terrainTexture);	// generated texture is in linear colorspace
			ImagingDelete(_terrainTempImage); _terrainTempImage = nullptr; // don't require reloading in onLoaded method.
		}

		SAFE_DELETE(_sim);
		_sim = new cSimulation();

		// rain!!!
#ifndef DEBUG_NO_RAIN
		SAFE_DELETE(_activeRain);
		_activeRain = new sRainInstance(XMVectorZero(), 128.0f);
#endif

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
				pGameObject->getModelInstance()->setAzimuth(v2_rotation_t(-1.3333f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::SINGER,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_singer);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(1.0f, 5.31f, 3.0f, 0.0f));
				pGameObject->getModelInstance()->setAzimuth(v2_rotation_t(-1.4666f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::MUSICIAN,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_keyboarder);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(7.0f, 5.31f, -7.0f, 0.0f));
				pGameObject->getModelInstance()->setAzimuth(v2_rotation_t(-1.5999f));
			}

			pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cRemoteUpdateGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(p2D_add(start, point2D_t(-100, 0)),
				Volumetric::eVoxelModels_Indices::MUSICIAN,
				flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);
			if (pGameObject) {
				pGameObject->setUpdateFunction(&do_update_drummer);
				pGameObject->getModelInstance()->setLocation3D(XMVectorSet(26.0f, 9.316f, 0.0f, 0.0f));
				pGameObject->getModelInstance()->setAzimuth(v2_rotation_t(-1.6666f));
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
						pGameObject->getModelInstance()->setAzimuth(v2_rotation_t(1.5999f));
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
			cTestGameObject* pTstGameObj(nullptr);
			cExplosionGameObject* pExplosionGameObj(nullptr);
			cLevelSetGameObject* pSphereGameObj(nullptr);

			//pSphereGameObj = placeProceduralInstanceAt<cLevelSetGameObject, true>(p2D_add(getVisibleGridCenter(), point2D_t(10, 10)));

			//pExplosionGameObj = placeUpdateableInstanceAt<cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(getVisibleGridCenter(),
			//	Volumetric::eVoxelModel::DYNAMIC::NAMED::GROUND_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
			
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
	// GridSnapshot is protected for read-only access
	auto cVoxelWorld::GridSnapshot() const -> std::pair<Iso::Voxel const* const __restrict, uint32_t const> const
	{
		static constexpr uint32_t const voxel_count(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
		static constexpr size_t const gridSz(sizeof(Iso::Voxel) * size_t(voxel_count));

		Iso::Voxel* __restrict gridSnapshot = (Iso::Voxel * __restrict)scalable_malloc(gridSz);

		{
			tbb::queuing_rw_mutex::scoped_lock lock(the::lock, false); // read-only access
			memcpy(gridSnapshot, the::grid, gridSz);
		}

		return(std::make_pair( gridSnapshot, voxel_count ));
	}

	// *** no simultaneous !writes! to grid or grid data can occur while calling this function ***
	// simultaneous reads (read-only) is NOT OK - unless protected
	// RenderGrid is protected only for read-only access
	// GridSnapshotLoad is protected for write access
	void cVoxelWorld::GridSnapshotLoad(Iso::Voxel const* const __restrict new_grid)
	{
		static constexpr uint32_t const voxel_count(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
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

			tbb::queuing_rw_mutex::scoped_lock lock(the::lock, true); // write access
			oldGrid = (Iso::Voxel*)_InterlockedExchangePointer((PVOID * __restrict)&the::grid._protected, theGrid);
		}
		// free the old grid's large allocation
		if (oldGrid) {
			scalable_aligned_free(oldGrid);
		}
	}

} // end ns world

namespace world
{
	uint32_t const cVoxelWorld::getRoadVisibleCount() const // *do not call inside of RenderGrid(), state is undefined! Ok for game logic, etc.
	{
		return(voxelRender::render_state.road_tiles_visible);
	}

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
		point2D_t const gridBounds(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z);
		rect2D_t area(r2D_set_by_width_height(point2D_t{}, gridBounds));

		area = r2D_add(area, p2D_sub(oCamera.voxelIndex_Center, p2D_half(gridBounds)));

		// clamp to world grid
		area = r2D_min(area, Iso::MAX_VOXEL_COORD);
		area = r2D_max(area, Iso::MIN_VOXEL_COORD);

		return(area);
	}
	point2D_t const __vectorcall cVoxelWorld::getVisibleGridCenter() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		return(oCamera.voxelIndex_Center);
	}
	v2_rotation_t const& cVoxelWorld::getAzimuth() const
	{
		return(oCamera.Azimuth);
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

		// these are cpu side "staging" buffers, matching in size to the gpu buffers that are allocated for them in cVulkan
		// *** these staging buffers are dynamic, the active size is reset to zero for a reason here.
		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
			voxels.visibleDynamic.opaque.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleDynamic.opaque.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleDynamic.opaque.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleDynamic.trans.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleDynamic.trans.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleDynamic.trans.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleStatic.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL * sizeof(voxels.visibleStatic.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleStatic.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleTerrain.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(voxels.visibleTerrain.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleTerrain.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleRoad.opaque.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(voxels.visibleRoad.opaque.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleRoad.opaque.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleRoad.trans.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(voxels.visibleRoad.trans.type), vku::eMappedAccess::Random, true, true);
			voxels.visibleRoad.trans.stagingBuffer[i].setActiveSizeBytes(0);
		}


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

	void cVoxelWorld::RenderTask_Normal(uint32_t const resource_index) const // OPACITY is enabled for update, and reources are mapped during normal rendering
	{
		// mapping //
		VertexDecl::VoxelNormal* const __restrict MappedVoxels_Terrain_Start = (VertexDecl::VoxelNormal* const __restrict)voxels.visibleTerrain.stagingBuffer[resource_index].map();
		tbb::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Terrain(MappedVoxels_Terrain_Start);

		VertexDecl::VoxelNormal* const __restrict MappedVoxels_Road_Start[Volumetric::eVoxelType::_size()] = {
			(VertexDecl::VoxelNormal* const __restrict)voxels.visibleRoad.opaque.stagingBuffer[resource_index].map(),
			(VertexDecl::VoxelNormal* const __restrict)voxels.visibleRoad.trans.stagingBuffer[resource_index].map(),
		};
		tbb::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Road[Volumetric::eVoxelType::_size()]{
			MappedVoxels_Road_Start[Volumetric::eVoxelType::opaque],
			MappedVoxels_Road_Start[Volumetric::eVoxelType::trans]
		};

		VertexDecl::VoxelNormal* const __restrict MappedVoxels_Static_Start = (VertexDecl::VoxelNormal* const __restrict)voxels.visibleStatic.stagingBuffer[resource_index].map();
		tbb::atomic<VertexDecl::VoxelNormal*> MappedVoxels_Static(MappedVoxels_Static_Start);

		VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::_size()] = {
			(VertexDecl::VoxelDynamic* const __restrict)voxels.visibleDynamic.opaque.stagingBuffer[resource_index].map(),
			(VertexDecl::VoxelDynamic* const __restrict)voxels.visibleDynamic.trans.stagingBuffer[resource_index].map(),
		};
		tbb::atomic<VertexDecl::VoxelDynamic*> MappedVoxels_Dynamic[Volumetric::eVoxelType::_size()]{
			MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque],
			MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans]
		};

		// ensure the async clears are done
		async_long_task::wait<background_critical>(_AsyncClearTaskID, "async clears");
		___streaming_store_fence(); // ensure "streaming" clears are coherent

		_OpacityMap.map(); // (maps, should be done once clear for lights has completed, and before any lights are added)

		// GRID RENDER //
		voxelRender::RenderGrid<Iso::OVER_SCREEN_VOXELS_X, Iso::OVER_SCREEN_VOXELS_Z>(
			oCamera.voxelIndex_TopLeft,
			MappedVoxels_Terrain,
			MappedVoxels_Road[Volumetric::eVoxelType::opaque], MappedVoxels_Road[Volumetric::eVoxelType::trans],
			MappedVoxels_Static,
			MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque], MappedVoxels_Dynamic[Volumetric::eVoxelType::trans]);

		// game related asynchronous methods
		// physics can be "cleared" as early as here. corresponding wait is in Update() of VoxelWorld.
		MinCity::Physics->AsyncClear();
		
		{ // voxels //
			vku::VertexBufferPartition* const __restrict& __restrict dynamic_partition_info_updater(MinCity::Vulkan->getDynamicPartitionInfo(resource_index));

			VertexDecl::VoxelDynamic* running_offset_start(MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque]);
			size_t running_offset_size(0);

			{
				// begin main (opaques only)
				VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
				size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque];
				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].active_vertex_count = (uint32_t const)current_dynamic_size;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += current_dynamic_size;
				running_offset_start = MappedVoxels_Dynamic_Current;
			}

			// Update Dynamic VertexBuffer Offsets for "Custom Voxel Shader Children"
			// ########### only use [Volumetric::eVoxelType::opaque] even if the child is a transparent pipeline
			// ########### the [Volumetric::eVoxelType::trans] is reserved for voxels belong to model instances on the grid and is soley for PARENT_TRANS usage

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
			// todo add other children here for added partitions of dynamic voxel parent


			// append "parent" transparents  ***MUST BE LAST***
			{
				// this is treated as if the transparents were being added to the opaque buffer the whole time
				// this must be done LAST after all voxel children partitions as the data is actually appended at the end.
				VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::trans];
				size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans];
				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_TRANS].active_vertex_count = (uint32_t const)current_dynamic_size;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::PARENT_TRANS].vertex_start_offset = (uint32_t const)running_offset_size;  // special handling injecting transparents into opaque / main part
			}
		}

		{ // roads
			vku::VertexBufferPartition* const __restrict& __restrict road_partition_info_updater(MinCity::Vulkan->getRoadPartitionInfo(resource_index));

			size_t running_offset_size(0);

			{
				// begin main (opaques only)
				VertexDecl::VoxelNormal* const __restrict MappedVoxels_Road_Current = MappedVoxels_Road[Volumetric::eVoxelType::opaque];
				size_t const current_dynamic_size = MappedVoxels_Road_Current - MappedVoxels_Road_Start[Volumetric::eVoxelType::opaque];
				// set the parent / main partition info
				road_partition_info_updater[eVoxelRoadVertexBufferPartition::PARENT_MAIN].active_vertex_count = (uint32_t const)current_dynamic_size;
				road_partition_info_updater[eVoxelRoadVertexBufferPartition::PARENT_MAIN].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += current_dynamic_size;
			}

			// append "parent" transparents  ***MUST BE LAST***
			{
				// this is treated as if the transparents were being added to the opaque buffer the whole time
				// this must be done LAST after all voxel children partitions as the data is actually appended at the end.
				VertexDecl::VoxelNormal* const __restrict MappedVoxels_Road_Current = MappedVoxels_Road[Volumetric::eVoxelType::trans];
				size_t const current_dynamic_size = MappedVoxels_Road_Current - MappedVoxels_Road_Start[Volumetric::eVoxelType::trans];
				// set the parent / main partition info
				road_partition_info_updater[eVoxelRoadVertexBufferPartition::PARENT_TRANS].active_vertex_count = (uint32_t const)current_dynamic_size;
				road_partition_info_updater[eVoxelRoadVertexBufferPartition::PARENT_TRANS].vertex_start_offset = (uint32_t const)running_offset_size;  // special handling injecting transparents into opaque / main part
			}
		}
		
		_OpacityMap.commit(); // (commits the new bounds, unmaps)

		///
		{
			VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_End = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
			size_t const activeSize = MappedVoxels_Dynamic_End - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque];
			voxels.visibleDynamic.opaque.stagingBuffer[resource_index].unmap();

			voxels.visibleDynamic.opaque.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelDynamic));
		}
		{
			VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_End = MappedVoxels_Dynamic[Volumetric::eVoxelType::trans];
			size_t const activeSize = MappedVoxels_Dynamic_End - MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans];
			voxels.visibleDynamic.trans.stagingBuffer[resource_index].unmap();

			voxels.visibleDynamic.trans.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelDynamic));
		}
		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_Static_End = MappedVoxels_Static;
			size_t const activeSize = MappedVoxels_Static_End - MappedVoxels_Static_Start;
			voxels.visibleStatic.stagingBuffer[resource_index].unmap();

			voxels.visibleStatic.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}

		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_Road_End = MappedVoxels_Road[Volumetric::eVoxelType::opaque];
			size_t const activeSize = MappedVoxels_Road_End - MappedVoxels_Road_Start[Volumetric::eVoxelType::opaque];
			voxels.visibleRoad.opaque.stagingBuffer[resource_index].unmap();

			voxels.visibleRoad.opaque.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}
		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_Road_End = MappedVoxels_Road[Volumetric::eVoxelType::trans];
			size_t const activeSize = MappedVoxels_Road_End - MappedVoxels_Road_Start[Volumetric::eVoxelType::trans];
			voxels.visibleRoad.trans.stagingBuffer[resource_index].unmap();

			voxels.visibleRoad.trans.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}

		{
			VertexDecl::VoxelNormal* const __restrict MappedVoxels_Terrain_End = MappedVoxels_Terrain;
			size_t const activeSize = MappedVoxels_Terrain_End - MappedVoxels_Terrain_Start;
			voxels.visibleTerrain.stagingBuffer[resource_index].unmap();

			voxels.visibleTerrain.stagingBuffer[resource_index].setActiveSizeBytes(activeSize * sizeof(VertexDecl::VoxelNormal));
		}
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
		vku::DynamicVertexBuffer* const* const& __restrict vbo)
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

			vbo[eVoxelVertexBuffer::VOXEL_TERRAIN]->uploadDeferred(cb, voxels.visibleTerrain.stagingBuffer[resource_index]);
			vbo[eVoxelVertexBuffer::VOXEL_ROAD]->uploadDeferred(cb, voxels.visibleRoad.opaque.stagingBuffer[resource_index], voxels.visibleRoad.trans.stagingBuffer[resource_index]);
			vbo[eVoxelVertexBuffer::VOXEL_STATIC]->uploadDeferred(cb, voxels.visibleStatic.stagingBuffer[resource_index]);
			vbo[eVoxelVertexBuffer::VOXEL_DYNAMIC]->uploadDeferred(cb, voxels.visibleDynamic.opaque.stagingBuffer[resource_index], voxels.visibleDynamic.trans.stagingBuffer[resource_index]);
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
				static constexpr size_t const buffer_count(4ULL);
 				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ vbo[eVoxelVertexBuffer::VOXEL_TERRAIN], vbo[eVoxelVertexBuffer::VOXEL_ROAD], vbo[eVoxelVertexBuffer::VOXEL_STATIC], vbo[eVoxelVertexBuffer::VOXEL_DYNAMIC] };
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
			oCamera.PrevAzimuthAngle = oCamera.Azimuth.angle();
			oCamera.TargetAzimuthAngle = oCamera.Azimuth.angle() + anglerelative;

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

			XMVECTOR const xmOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat3A(&oCamera.Origin))); // only care about xz components, make it a 2D vector

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, XMVectorRound(xmPosition));
			XMStoreFloat2A(&oCamera.PrevPosition, xmOrigin);

			// signal transition
			oCamera.tTranslateStart = now();  // closer to exact now results in the lerp not being skipped right to target position

			return(true); // translation started
		}

		return(false); // translation cancelled
	}

	STATIC_INLINE bool const XM_CALLCONV translateCameraOrient(XMVECTOR xmDisplacement)	// this one orients in the current direction of the camera
	{
		uint32_t Result;
		XMVectorEqualR(&Result, XMVectorZero(), xmDisplacement);
		if (XMComparisonAnyFalse(Result)) {

			XMVECTOR const xmOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat3A(&oCamera.Origin))); // only care about xz components, make it a 2D vector

			// orient displacement in direction of camera
			xmDisplacement = v2_rotate(xmDisplacement, oCamera.Azimuth);

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, XMVectorRound(xmPosition));// XMVectorAdd(xmPosition, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(getFractionalOffset())));
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
		if (absDir.x > absDir.y) {		 
			xmIso = XMVectorScale(xmIso, 1.0f * XMVectorGetX(XMVector2Length(p2D_to_v2(absDir))));
		}
		else {
			xmIso = XMVectorScale(xmIso, 1.0f * XMVectorGetX(XMVector2Length(p2D_to_v2(absDir)) * cMinCity::getFramebufferAspect()));
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
		if (0.0f != oCamera.Azimuth.angle()) {
			// setup lerp
			oCamera.PrevAzimuthAngle = oCamera.Azimuth.angle();
			oCamera.TargetAzimuthAngle = 0.0f;

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

			point2D_t const rawData[2]{ MinCity::Vulkan->queryMouseBuffer(XMLoadFloat2A(&_vMouse), 1), MinCity::Vulkan->queryMouseBuffer(XMLoadFloat2A(&_vMouse), 0) };

			if (!rawData[0].isZero()) { // bugfix for when mouse hovers out of grid bounds 

				// normalize from [0...65535] to [0.0f....1.0f] //
				XMVECTOR xmVoxelIndex(p2D_to_v2(rawData[0]));
				xmVoxelIndex = XMVectorScale(xmVoxelIndex, INV_MAX_VALUE);

				// change normalized range to voxel grid range [0.0f....1.0f] to [-WORLD_GRID_FHALFSIZE, WORLD_GRID_FHALFSIZE]
				xmVoxelIndex = SFM::__fms(xmVoxelIndex, _mm_set1_ps(Iso::WORLD_GRID_FSIZE), _mm_set1_ps(Iso::WORLD_GRID_FHALFSIZE));

				point2D_t const voxelIndex(v2_to_p2D_rounded(xmVoxelIndex)); // *** MUST BE ROUNDED FOR SELECTION PRECISION *** //

				_voxelIndexHover.v = voxelIndex.v;
				_occlusion.groundVoxelIndex.v = voxelIndex.v;
			}
			if (!rawData[1].isZero()) { // bugfix for when mouse hovers out of grid bounds 

				// normalize from [0...65535] to [0.0f....1.0f] //
				XMVECTOR xmVoxelIndex(p2D_to_v2(rawData[1]));
				xmVoxelIndex = XMVectorScale(xmVoxelIndex, INV_MAX_VALUE);

				// change normalized range to voxel grid range [0.0f....1.0f] to [-WORLD_GRID_FHALFSIZE, WORLD_GRID_FHALFSIZE]
				xmVoxelIndex = SFM::__fms(xmVoxelIndex, _mm_set1_ps(Iso::WORLD_GRID_FSIZE), _mm_set1_ps(Iso::WORLD_GRID_FHALFSIZE));

				point2D_t const voxelIndex(v2_to_p2D_rounded(xmVoxelIndex)); // *** MUST BE ROUNDED FOR SELECTION PRECISION *** //

				if (voxelIndex != _occlusion.occlusionVoxelIndex) {

					uvec4_v const cmpMax(Iso::SCREEN_VOXELS_XZ);
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
									 MIN_DELTA = MAX_DELTA * 0.125;														// 8ms
		constinit static float time_last(0.0f), // last interpolated time
							   time_delta_last(0.0f); // last interpolated time delta

		_currentState.time = SFM::lerp(_lastState.time, _targetState.time, tRemainder);

		// clamp at the 2x step size, don't care or want spurious spikes of time
		float const time_delta = SFM::clamp(_currentState.time - time_last, MIN_DELTA, MAX_DELTA);

		XMFLOAT2A vPush{};
#ifndef NDEBUG
		XMStoreFloat2A(&vPush, getDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR));
#endif
		//pack into vector for uniform buffer layout								                                                 // z = frame time delta (average of this frame and last frames delta to smooth out large changes between frames)											   
		_currentState.Uniform.aligned_data0 = XMVectorSet(oCamera.voxelFractionalGridOffset.x, oCamera.voxelFractionalGridOffset.z, (time_delta + time_delta_last) * 0.5f, _currentState.time); // w = time
		                                                                
		_currentState.Uniform.frame = (uint32_t)MinCity::getFrameCount();		// todo check overflow of 32bit frame counter for shaders

		time_last = _currentState.time; // update last interpolated time
		time_delta_last = time_delta;	//   ""    ""       ""      time delta

		// view matrix derived from eyePos
		XMVECTOR const xmEyePos(SFM::lerp(_lastState.Uniform.eyePos, _targetState.Uniform.eyePos, tRemainder));
		
		// In the ening these must be w/o fractional offset - no jitter on lighting and everything else that uses these uniform variables (normal usage via uniform buffer in shader)
		_currentState.Uniform.eyePos = xmEyePos;
		_currentState.Uniform.eyeDir = XMVector3Normalize(_currentState.Uniform.eyePos); // target is always 0,0,0 this would normally be 0 - eyePos, it's upside down instead to work with Vulkan Coordinate System more easily.
		
		// All positions in game are transformed by the view matrix in all shaders. Fractional Offset MUST be added here for jitter free movement of camera -in a matrix. does not work with xmEyePos, something internal to the function XMMatrixLookAtLH, xmEyePos must remain the same w/o fractional offset))
		
		XMMATRIX xmView = XMMatrixLookAtLH(xmEyePos,  // normal view matrix
										   XMVectorZero(), Iso::xmUp); // notice xmUp is positive here (everything is upside down) to get around Vulkan Negative Y Axis see above eyeDirection

		// *bugfix - ***do not change***
		// view matrix is independent of fractional offset, fractional offset is no longer applied to the view matrix. There are 2 locations in the voxel geometry and volumetric raymarch shaders where the fractional offset is applied.
		// *bugfix - moved fractional offset to shaders, increasing precision of voxel alignment with map/view scrolling
		// fractional offset needs to be the last mathematical operation before the final value of (x) is used - in the shader geometry this happens right before the output vertex transform is output. in the volumetric shader it is also the same.
		// fractional offset should not be applied in the "light" compute shader (testing light alignment still), this is however sampled...
		// sampling the light volume with uv's in generated in the geometry shader for voxels are derived from the output vertex world position...which just had the fractional offset added and was output to the vertex transform
		//xmView = XMMatrixMultiply(XMMatrixTranslationFromVector(getFractionalOffset()), xmView);
		_currentState.Uniform.view = xmView;
		// not yet required
		//_currentState.Uniform.inv_view = XMMatrixInverse(nullptr, xmView);

		// Update Frustum, which updates projection matrix, which is derived from ZoomFactor
		_currentState.zoom = SFM::lerp(_lastState.zoom, _targetState.zoom, tRemainder);
		_Visibility.UpdateFrustum(xmView, _currentState.zoom, MinCity::getFrameCount());

		// Get current projection matrix after update of frustum and in turn projection
		_currentState.Uniform.proj = _Visibility.getProjectionMatrix();
		_currentState.Uniform.viewproj = XMMatrixMultiply(xmView, _currentState.Uniform.proj);			// matrices can not be interpolated effectively they must be recalculated
																										// from there base components

	}
	void __vectorcall cVoxelWorld::UpdateUniformStateTarget(tTime const& __restrict tNow, tTime const& __restrict tStart, bool const bFirstUpdate) // fixed timestep state
	{
		_lastState = _targetState;

		_targetState.time = time_to_float(fp_seconds(tNow - tStart));

		// "XMMatrixInverse" found to be imprecise for acquiring vector components (forward, direction)
		// using original values instead (-----precise))
		_targetState.Uniform.eyePos = v3_rotate_azimuth(XMVectorAdd(Iso::xmEyePt_Iso, getFractionalOffset()), oCamera.Azimuth);

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
		
		[[unlikely]]  if (bJustLoaded) {
			// ### ONLOADED EVENT must be triggered here !! //
			OnLoaded(tNow);
		}
		else {
			
			updateMouseOcclusion(bPaused);

			_sim->run(tNow, tDelta, the::grid);

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
		tTime const tStart(start());

		// ***** Anything that will affect uniform state BEFORE
		/*XMVECTOR const xmOrigin =*/ UpdateCamera(tNow, tDelta);

		//###########################################################//
		UpdateUniformStateTarget(tNow, tStart, bJustLoaded);
		//###########################################################//
		
		// ***** Anything that uses uniform state updates AFTER
		_bMotionDelta = false; // must reset *here*
		
		[[unlikely]] if (bJustLoaded)
			return;
		
		[[unlikely]] if (eExclusivity::DEFAULT != cMinCity::getExclusivity()) // ** Only after this point exists game update related code
			return;															 // ** above is independent, and is compatible with all alternative exclusivity states (LOADING/SAVING/etc.)

		// any operations that do not need to execute while paused should not
		if (!bPaused) {

			MinCity::Physics->Update(tNow, tDelta);

			if (!UpdateRain(tNow, _activeRain)) {  // ### todo - rain pours while paused if rain is not updated while paused
				SAFE_DELETE(_activeRain);
			}
			
			if (oCamera.ZoomToExtents) {

				// auto zoom to AABB extents feature
				zoomCamera(XMLoadFloat3A(&oCamera.ZoomExtents));
			}

			// update all image animations //
			{
				auto it = ImageAnimation::begin();
				while (ImageAnimation::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			// update all dynamic/updateable game objects //
			{
				{
					auto it = cExplosionGameObject::begin();
					while (cExplosionGameObject::end() != it) {

						it->OnUpdate(tNow, tDelta);
						++it;
					}
				}
						
				// import //
				{
					auto it = cImportGameObject_Dynamic::begin();
					while (cImportGameObject_Dynamic::end() != it) {

						it->OnUpdate(tNow, tDelta);
						++it;
					}
				}
				{
					auto it = cImportGameObject_Static::begin();
					while (cImportGameObject_Static::end() != it) {

						it->OnUpdate(tNow, tDelta);
						++it;
					}
				}
			}
			{
				// select buildings only update
				::cBuildingGameObject::UpdateAll(tNow, tDelta);
			}
			{
				// traffic controllers - *should be done b4 cars* //
				auto it = cTrafficControlGameObject::begin();
				while (cTrafficControlGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
				::cCarGameObject::UpdateAll(tNow, tDelta);
				::cPoliceCarGameObject::UpdateAll(tNow, tDelta);
			}
			{
				// copter //
				auto it = cCopterGameObject::begin();
				while (cCopterGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cRemoteUpdateGameObject::begin();
				while (cRemoteUpdateGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cTestGameObject::begin();
				while (cTestGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}

			// last
			{
				auto it = cLevelSetGameObject::begin();
				while (cLevelSetGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}

		} // end !Paused //

		// ---------------------------------------------------------------------------//
		CleanUpInstanceQueue(); // *** must be done after all game object updates *** //
		// ---------------------------------------------------------------------------//
	}

	void cVoxelWorld::AsyncClears(uint32_t const resource_index)
	{
		_AsyncClearTaskID = async_long_task::enqueue<background_critical>([&, resource_index] {

			{
				auto const& stagingBuffer(voxels.visibleTerrain.stagingBuffer[resource_index]);
				___memset_threaded<32>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			{
				auto const& stagingBuffer(voxels.visibleRoad.opaque.stagingBuffer[resource_index]);
				___memset_threaded<32>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			{
				auto const& stagingBuffer(voxels.visibleRoad.trans.stagingBuffer[resource_index]);
				___memset_threaded<32>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			{
				auto const& stagingBuffer(voxels.visibleStatic.stagingBuffer[resource_index]);
				___memset_threaded<32>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			{
				auto const& stagingBuffer(voxels.visibleDynamic.opaque.stagingBuffer[resource_index]);
				___memset_threaded<16>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			{
				auto const& stagingBuffer(voxels.visibleDynamic.trans.stagingBuffer[resource_index]);
				___memset_threaded<16>(stagingBuffer.map(), 0, stagingBuffer.activesizebytes());
				stagingBuffer.unmap();
			}
			
			/*
			{
				
			}
			{

			}
			{
				
			}
			{
				
			}
			{
				
			}
			{
				
			}
			*/
		});

		// *bugfix - having these clears inside of the async thread causes massive flickering of light, not coherent! Moving it outside and simultaneous still works fine.
		getVolumetricOpacity().clear(resource_index); // better distribution of cpu at a later point in time of the frame.
	}
	
	// voxel painting
	void cVoxelWorld::clearMiniVoxels(bool const bPaused)
	{
		[[unlikely]] if (bPaused) {
			// **bugfix - because nothing is rendered while the window is not active (user alt-tabbed, etc) the "reset" that each minivoxel does for it self after being rendered never happens.
			// this results in an evergrowing linked list, resulting in undefined behaviour. So while paused this works around that by clearing the pointers to the vector of minivoxels.
			// so there is no separate clear while not paused - which is the normal path.
			___memset_threaded<CACHE_LINE_BYTES>(the::temp, 0, sizeof(Iso::mini::Voxel const*) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE); // set all pointers to nullptr
			// clearing the vector must follow
		}

		mini::voxels.clear(); // this cleverely only clears the count, not the actual data in the vector. so any pointers to the data, which exist, are invalid and point to undefined data.
							  // the bug actually still pointed to the old data (not zeroed) and resulted in undefined behaviour. That behaviour resulted in voxels being added to existing voxels every frame, boom was an ~indefinite loop of voxels to traverse in the linked list.
		mini::bits->clear();
	}
	
	static constexpr float const VOXEL_WORLD_SCALAR = Iso::MINI_VOX_SIZE; // should be minivox size
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
		constants.emplace_back(vku::SpecializationConstant(5,  1.0f / (float)Volumetric::voxelOpacity::getSize()));       // should be inverse world volume size
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getVolumeLength())); // should be world volume length (1:1 ratio here, do not scale by voxel size)

		// light volume dimensions //																				// xzy
		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize()));		   // should be light volume size
		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize()));   // should be 1.0 / light volume size

		// For depth reconstruction from hardware depth buffer
		// https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
		constexpr double ZFar = Globals::MAXZ_DEPTH;
		constexpr double ZNear = Globals::MINZ_DEPTH;
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

		constants.emplace_back(vku::SpecializationConstant(6, (float)(Volumetric::voxelOpacity::getVolumeLength() * VOXEL_WORLD_SCALAR)));  // should be world volume length * voxel size

		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize())); // should be light volume size

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize())); // should be inv light volume size
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelRoad_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size
		constants.emplace_back(vku::SpecializationConstant(5, 1.0f / (float)Volumetric::voxelOpacity::getSize())); // should be inverse world visible volume size

		constants.emplace_back(vku::SpecializationConstant(6, (float)(Volumetric::voxelOpacity::getVolumeLength() * VOXEL_WORLD_SCALAR)));  // should be world volume length * voxel size

		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize())); // should be light volume size

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize())); // should be inv light volume size
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_Basic_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, bool const bMiniVoxel)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)(bMiniVoxel ? (Iso::MINI_VOX_SIZE) : Iso::VOX_SIZE))); // VS is dependent on type of voxel for geometry size
		// used for uv -> voxel in vertex shader image store operation for opacity map
		constants.emplace_back(vku::SpecializationConstant(1, (float)Volumetric::voxelOpacity::getSize())); // should be world visible volume size

		XMFLOAT3A transformBias, transformInv;

		XMStoreFloat3A(&transformBias, Volumetric::_xmTransformToIndexBias);
		XMStoreFloat3A(&transformInv, Volumetric::_xmInverseVisible);

		// do not swizzle or change order
		constants.emplace_back(vku::SpecializationConstant(2, (float)transformBias.x));
		constants.emplace_back(vku::SpecializationConstant(3, (float)transformBias.y));
		constants.emplace_back(vku::SpecializationConstant(4, (float)transformBias.z));

		constants.emplace_back(vku::SpecializationConstant(5, (float)transformInv.x));
		constants.emplace_back(vku::SpecializationConstant(6, (float)transformInv.y));
		constants.emplace_back(vku::SpecializationConstant(7, (float)transformInv.z));
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_Basic_VS_Common(constants, false);

		// used for uv -> voxel in vertex shader image store operation for opacity map

		constants.emplace_back(vku::SpecializationConstant(8, (float)1.0f / Iso::MAX_HEIGHT_STEP));
		constants.emplace_back(vku::SpecializationConstant(9, (float)Iso::HEIGHT_SCALE));
		constants.emplace_back(vku::SpecializationConstant(10, (int)MINIVOXEL_FACTOR));		
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelRoad_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_VoxelTerrain_Basic_VS(constants);

		constants.emplace_back(vku::SpecializationConstant(11, (float)Iso::ROAD_SEGMENT_WIDTH));
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, bool const bMiniVoxel)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)(bMiniVoxel ? (Iso::MINI_VOX_SIZE) : Iso::VOX_SIZE))); // VS is dependent on type of voxel for geometry size
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_VS(std::vector<vku::SpecializationConstant>& __restrict constants) // ** also used for roads 
	{
		SetSpecializationConstants_Voxel_VS_Common(constants, false);

		// used for uv -> voxel in vertex shader image store operation for opacity map
		constants.emplace_back(vku::SpecializationConstant(1, (float)Volumetric::voxelOpacity::getSize())); // should be world volume size
		constants.emplace_back(vku::SpecializationConstant(2, (float)1.0f / Iso::MAX_HEIGHT_STEP));
		constants.emplace_back(vku::SpecializationConstant(3, (float)Iso::HEIGHT_SCALE));
		constants.emplace_back(vku::SpecializationConstant(4, (int)MINIVOXEL_FACTOR));
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelRoad_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_VoxelTerrain_VS(constants);

		constants.emplace_back(vku::SpecializationConstant(5, (float)Iso::ROAD_SEGMENT_WIDTH));
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_VS_Common(constants, true);
	}
	void cVoxelWorld::SetSpecializationConstants_Voxel_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_Basic_VS_Common(constants, true);
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_GS_Common(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		// used for uv creation in geometry shader

		XMFLOAT3A transformBias, transformInv;

		XMStoreFloat3A(&transformBias, Volumetric::_xmTransformToIndexBias);
		XMStoreFloat3A(&transformInv, Volumetric::_xmInverseVisible);

		// do not swizzle or change order
		constants.emplace_back(vku::SpecializationConstant(0, (float)transformBias.x));
		constants.emplace_back(vku::SpecializationConstant(1, (float)transformBias.y));
		constants.emplace_back(vku::SpecializationConstant(2, (float)transformBias.z));

		constants.emplace_back(vku::SpecializationConstant(3, (float)transformInv.x));
		constants.emplace_back(vku::SpecializationConstant(4, (float)transformInv.y));
		constants.emplace_back(vku::SpecializationConstant(5, (float)transformInv.z));
	}
	void cVoxelWorld::SetSpecializationConstants_Voxel_GS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_GS_Common(constants);
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_GS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_GS_Common(constants);
		
		// used for uv creation in geometry shader  // terrain texture is generated (not available yet)
		constants.emplace_back(vku::SpecializationConstant(6, 0.5f / ((float)world::TERRAIN_TEXTURE_SZ)));// (0.5f / ((float)Volumetric::Allocation::VOXEL_GRID_VISIBLE_XZ)))); // should be width/depth (width==depth)
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelRoad_GS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_GS_Common(constants);
		
		// used for uv creation in geometry shader  // texture loaded
		constants.emplace_back(vku::SpecializationConstant(6, 0.5f / ((float)world::TERRAIN_TEXTURE_SZ)));// (0.5f / ((float)Volumetric::Allocation::VOXEL_GRID_VISIBLE_XZ)))); // should be width/depth (width==depth)
		constants.emplace_back(vku::SpecializationConstant(7, 0.5f / ((float)world::TERRAIN_TEXTURE_SZ) * (float)Iso::ROAD_SEGMENT_WIDTH));
		constants.emplace_back(vku::SpecializationConstant(8, (float)Iso::ROAD_SEGMENT_WIDTH));
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

		constants.emplace_back(vku::SpecializationConstant(6, (float)(Volumetric::voxelOpacity::getVolumeLength() * VOXEL_WORLD_SCALAR)));  // should be world volume length * voxel size

		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightSize())); // should be light volume size

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightSize())); // should be inv light volume size
	}

	void cVoxelWorld::SetSpecializationConstants_VoxelRain_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)(Iso::MINI_VOX_SIZE * Volumetric::Konstants::VOXEL_RAIN_SCALE))); // VS is dependent on type of voxel for geometry size
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
	void cVoxelWorld::UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& __restrict samplerLinearBorder)
	{
		_OpacityMap.UpdateDescriptorSet_ComputeLight(dsu, samplerLinearBorder);
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
	void cVoxelWorld::UpdateDescriptorSet_VolumetricLight(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_STANDARD_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, halfdepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);  // depth
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);  // raymarch uses 2 channels, all slices of blue noise (*bugfix - raymarch jittered offset now also uses blue noise over time, with no visible noisyness in reflections!)
		//dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		//dsu.image(samplerLinearRepeat, _textureShader[eTextureShader::WIND_DIRECTION].output->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // too difficult does not allow world origin to have or not have the fractional offset properly, texture shaders no longer in use - deprecated
		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->DistanceDirection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->Color->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 2, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->Reflection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 3, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().OpacityMap->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 0, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, halfreflectionImageView, vk::ImageLayout::eGeneral);
		dsu.beginImages(4U, 1, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, halfvolumetricImageView, vk::ImageLayout::eGeneral);
		
#ifdef DEBUG_VOLUMETRIC
		dsu.beginBuffers(10U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(DebugStorageBuffer->buffer(), 0, sizeof(UniformDecl::DebugStorageBuffer));
#endif
	}

	void cVoxelWorld::UpdateDescriptorSet_VolumetricLightResolve(vku::DescriptorSetUpdater& __restrict dsu, 
																 vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, 
																 vk::ImageView const& __restrict fullvolumetricImageView, vk::ImageView const& __restrict fullreflectionImageView,
																 SAMPLER_SET_STANDARD_POINT)
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
		SAMPLER_SET_STANDARD_POINT)
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
		SAMPLER_SET_STANDARD_POINT)
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
		SAMPLER_SET_STANDARD_POINT)
	{
		// 1 - colorview (backbuffer)
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		// 2 - bluenoise
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2DArray()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal); // presentation uses single channel, all slices of blue noise

		MinCity::PostProcess->UpdateDescriptorSet_PostAA_Final(dsu, guiImageView, samplerLinearClamp);
	}
	void cVoxelWorld::UpdateDescriptorSet_VoxelCommon(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict fullreflectionImageView, vk::ImageView const& __restrict lastColorImageView, SAMPLER_SET_STANDARD_POINT_ANISO)
	{
		dsu.beginBuffers(1U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer[resource_index].buffer(), 0, _buffers.shared_buffer[resource_index].maxsizebytes());

		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->DistanceDirection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->Color->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 2, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().OpacityMap->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, fullreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);

		// finalize texture array and commit to descriptor set #######################################################################
		tbb::concurrent_vector<vku::TextureImage2DArray const*> const& rTextures( MinCity::TextureBoy->lockTextureArray() );

		dsu.beginImages(5U, TEX_NOISE, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_NOISE_SAMPLER, rTextures[TEX_NOISE]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_BLUE_NOISE, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_BLUE_NOISE_SAMPLER, rTextures[TEX_BLUE_NOISE]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_TERRAIN, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_TERRAIN_SAMPLER, rTextures[TEX_TERRAIN]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_GRID, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_GRID_SAMPLER, rTextures[TEX_GRID]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_ROAD, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_ROAD_SAMPLER, rTextures[TEX_ROAD]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_BLACKBODY, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_BLACKBODY_SAMPLER, rTextures[TEX_BLACKBODY]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		// #############################################################################################################################
		
		dsu.beginImages(6U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, lastColorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
	}
	void cVoxelWorld::UpdateDescriptorSet_Voxel_ClearMask(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu)
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
		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {

			Iso::Voxel const oVoxel(*pVoxel);

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
		}
		
		return(0);
	}
	// queries if voxel model instance of type intersects a voxel area. has to be the owner voxel of the instance for this to succeed.
	uint32_t const cVoxelWorld::hasVoxelModelInstanceAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex) const
	{
		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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

		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {

			Iso::Voxel oVoxel(*pVoxel);

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
		}

		return(existing);
	}

	bool const cVoxelWorld::hideVoxelModelInstancesAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex, vector<Iso::voxelIndexHashPair>* const pRecordHidden)
	{
		bool bExisting(false);

		// clamp to world/minmax coords
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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
	bool const cVoxelWorld::destroyVoxelModelInstanceAt(Iso::Voxel const* const pVoxel, uint32_t const hashTypes)
	{
		static constexpr uint32_t const INSTANCE_COUNT(8);

		bool bExisting(false);

		if (pVoxel) {

			Iso::Voxel const oVoxel(*pVoxel);

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
		voxelArea = r2D_clamp(voxelArea, Iso::MIN_VOXEL_COORD, Iso::MAX_VOXEL_COORD);

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

			Iso::Voxel const* const pVoxel(getVoxelAt(rootVoxel));
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

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
																								  DeleteModelInstance_Dynamic->getAzimuth() }));

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
		async_long_task::wait<background_critical>(_AsyncClearTaskID, "async clears"); // ensure the async clears are done

		SAFE_DELETE(_sim);

		// cleanup special instances
		SAFE_DELETE(_activeRain);
		

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

		_OpacityMap.release();

		for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
			voxels.visibleDynamic.opaque.stagingBuffer[i].release();
			voxels.visibleDynamic.trans.stagingBuffer[i].release();
			voxels.visibleStatic.stagingBuffer[i].release();
			voxels.visibleTerrain.stagingBuffer[i].release();
			voxels.visibleRoad.opaque.stagingBuffer[i].release();
			voxels.visibleRoad.trans.stagingBuffer[i].release();
		}

		if (_terrainTempImage) {

			ImagingDelete(_terrainTempImage); _terrainTempImage = nullptr;
		}

		SAFE_DELETE(_terrainTexture);
		SAFE_DELETE(_gridTexture);
		SAFE_DELETE(_roadTexture);
		SAFE_DELETE(_blackbodyTexture);

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

		if (_blackbodyImage) {
			ImagingDelete(_blackbodyImage);
		}

		_buffers.reset_subgroup_layer_count_max.release();
		_buffers.reset_shared_buffer.release();

		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			
			_buffers.subgroup_layer_count_max[resource_index].release();
			_buffers.shared_buffer[resource_index].release();
		}

		supernoise::blue.Release();

		using volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;

		if (mini::bits) {
			volume::destroy(mini::bits);
		}
		if (the::temp._protected) {
			scalable_aligned_free(the::temp._protected);
		}

		if (the::grid._protected) {
			scalable_aligned_free(the::grid._protected);
		}
		

#ifdef DEBUG_STORAGE_BUFFER
		SAFE_RELEASE_DELETE(DebugStorageBuffer);
#endif
	}
} // end ns world