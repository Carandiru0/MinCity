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
#include "cNuklear.h"
#include "cPostProcess.h"

#define V2_ROTATION_IMPLEMENTATION
#include "voxelAlloc.h"
#include "voxelModel.h"
#include "eVoxelModels.h"
#include <Random/superrandom.hpp>
#include "cUserInterface.h"
#include "cCity.h"

#include "explosion.h"
#include "tornado.h"
#include "shockwave.h"
#include "rain.h"

#include "cTrafficControlGameObject.h"
#include "cPoliceCarGameObject.h"
#include "cCarGameObject.h"
#include "cCopterGameObject.h"
#include "cTestGameObject.h"
#include "ImageAnimation.h"
#include "Adjacency.h"
#include "eDirection.h"

#include <queue>

// for the texture indices used in large texture array
#include "../Data/Shaders/texturearray.glsl"

#pragma intrinsic(_InterlockedExchangePointer)

using namespace world;

// From default point of view (isometric):
/*
										      x
							  [  N  ]<--             -->[  E  ]
					  x       			------x------ 	             x  
							  [  W  ]<--             -->[  S  ]
											  x  

*/

static struct CameraEntity
{
	static constexpr fp_seconds const TRANSITION_TIME = fp_seconds(milliseconds(32));

	point2D_t 
		voxelIndex_TopLeft,
		voxelIndex_Center;
	XMFLOAT2A
		voxelFractionalGridOffset;

	XMFLOAT2A
		Pan;

	v2_rotation_t Azimuth;
	float PrevAzimuthAngle, TargetAzimuthAngle;

	XMFLOAT2A Velocity, Origin;

	XMFLOAT2A TargetPosition, Displacement;

	float InitialDistanceToTarget;

	float ZoomFactor;

	float PrevZoomFactor,
		  TargetZoomFactor;

	tTime tTranslateStart,
		tRotateStart,
		tZoomStart;

	bool Motion;

	CameraEntity()
		: //Origin(Iso::MIN_VOXEL_COORD >> 1, Iso::MAX_VOXEL_COORD >> 1), // virtual coordinates
		Origin(0, 0), // virtual coordinates
		Pan{},
		voxelFractionalGridOffset{},
		ZoomFactor(Globals::DEFAULT_ZOOM_SCALAR),
		tTranslateStart{ zero_time_point },
		tRotateStart{ zero_time_point },
		tZoomStart{ zero_time_point },
		Motion(false)
	{}
} oCamera;

static alignas(CACHE_LINE_BYTES) struct // purposely anonymous union, protected pointer implementation for _theGrid
{
	Iso::Voxel* __restrict		 _protected;
	tbb::queuing_rw_mutex		 _lock;

	__declspec(safebuffers) __forceinline operator Iso::Voxel* const __restrict() const {
		return(_protected);
	}
} _theGrid;

static uint32_t const GROUND_HEIGHT_NOISE[NUM_DISTINCT_GROUND_HEIGHTS] = {
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

// ####### Private Init methods

// Grid Space Coordinates (0,0) to (X,Y) Only		
STATIC_INLINE Iso::Voxel const* const __restrict getNeighbour(point2D_t const& __restrict voxelIndex, point2D_t const& __restrict relativeOffset)
{
	// at this point, function expects voxelIndex to be in (0,0) => (WORLD_GRID_SIZE, WORLD_GRID_SIZE) range
	point2D_t const voxelNeighbour(p2D_add(voxelIndex, relativeOffset));

	// this function will also return the owning voxel
	// if zero,zero is passed in for the relative offset

	// Check bounds
	if ((voxelNeighbour.x | voxelNeighbour.y) >= 0) {

		if (voxelNeighbour.x < Iso::WORLD_GRID_SIZE && voxelNeighbour.y < Iso::WORLD_GRID_SIZE) {

			return((_theGrid + ((voxelNeighbour.y * Iso::WORLD_GRID_SIZE) + voxelNeighbour.x)));
		}
	}

	return(nullptr);
}

static void ComputeGroundOcclusion()
{
	point2D_t voxelIndex(Iso::WORLD_GRID_SIZE - 1, Iso::WORLD_GRID_SIZE - 1);

	// Traverse Heigh Generated Grid	

	while (voxelIndex.y >= 0)
	{
		voxelIndex.x = Iso::WORLD_GRID_SIZE - 1;

		while (voxelIndex.x >= 0)
		{
			Iso::Voxel* const theGrid = (_theGrid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x));

			// Get / Copy current voxel from external SRAM
			Iso::Voxel oVoxel(*theGrid);

			//if (isGroundOnly(oVoxel))
			{
				Iso::Voxel const* __restrict pNeighbour(nullptr);
				uint32_t const curVoxelHeightStep(Iso::getHeightStep(oVoxel));
				uint8_t OcclusionShading(0);
				
				// old layout: //
				/*

										  [NBR_TL]
							   [NBR_L]				   [NBR_T]
					NBR_BL					VOXEL					NBR_TR
								NBR_B					NBR_R
											NBR_BR

				*/
				// Therefore the neighbours of a voxel, follow same isometric layout/order //
				/*

										  [NBR_TR]
							   [NBR_T]				   [NBR_R]
					NBR_TL					VOXEL					NBR_BR
								NBR_L					NBR_B
											NBR_BL
				*/

				// Side Left = NBR_T (ao.x)
				pNeighbour = ::getNeighbour(voxelIndex, ADJACENT[NBR_T]);
				if (nullptr != pNeighbour) {
					
					if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
						OcclusionShading |= Iso::OCCLUSION_SHADING_SIDE_LEFT;
					}
				}
				
				
				// Corner = NBR_TR (ao.y)
				pNeighbour = ::getNeighbour(voxelIndex, ADJACENT[NBR_TR]);
				if (nullptr != pNeighbour) {
					
					if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
						OcclusionShading |= Iso::OCCLUSION_SHADING_CORNER;
					}
				}
				
				// Side Right = NBR_R (ao.z)
				pNeighbour = ::getNeighbour(voxelIndex, ADJACENT[NBR_R]);
				if (nullptr != pNeighbour) {
					
					if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
						OcclusionShading |= Iso::OCCLUSION_SHADING_SIDE_RIGHT;
					}
				}
				

				// finally, if no flags were set on occlusion, flag as such
				if (0 == OcclusionShading) {
					Iso::clearOcclusion(oVoxel);
				}
				else {	// other wise save computed ao shading
					Iso::setOcclusion(oVoxel, OcclusionShading);
				}
			}

			// Update current voxel in external SRAM
			*theGrid = oVoxel;

			--voxelIndex.x;
		}
		--voxelIndex.y;
	}
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
	supernoise::NewNoisePermutation();

	// Generate Image
	ImagingMemoryInstance* imageNoise = MinCity::Procedural.GenerateNoiseImage(&RenderNoiseImagePixel, Iso::WORLD_GRID_SIZE, supernoise::interpolator::SmoothStep() );

	// Create and Upload Texture //
	{
		// must be BGRX into resampling (4 channels)
		Imaging resampledImg = ImagingResample(imageNoise, Iso::WORLD_GRID_SIZE << 1, Iso::WORLD_GRID_SIZE << 1, IMAGING_TRANSFORM_BICUBIC);
		
		Imaging filteredImg = MinCity::Procedural.BilateralFilter(resampledImg);
		ImagingDelete(resampledImg);

		resampledImg = ImagingResample(filteredImg, Iso::WORLD_GRID_SIZE, Iso::WORLD_GRID_SIZE, IMAGING_TRANSFORM_BICUBIC);
		ImagingDelete(filteredImg);
		ImagingDelete(imageNoise); imageNoise = resampledImg; // final image for voxel grid heights

		// terrain teture is multiple of voxel grid size, power of 2 and not exceeding 16384
		// should be placed in dedicated memory on gpu
		resampledImg = ImagingResample(imageNoise, world::TERRAIN_TEXTURE_SZ, world::TERRAIN_TEXTURE_SZ, IMAGING_TRANSFORM_BICUBIC);
		
		//resampledImg = MinCity::Procedural.Colorize_TestPattern(resampledImg);
#ifndef DEBUG_LOADTIME_BC7_COMPRESSION_DISABLED
		Imaging imgCompressedBC7 = ImagingCompressBGRAToBC7(resampledImg);
		
		MinCity::TextureBoy.ImagingToTexture_BC7(imgCompressedBC7, _terrainTexture);
		
		ImagingDelete(imgCompressedBC7);
#else
		MinCity::TextureBoy.ImagingToTexture(resampledImg, _terrainTexture);
#endif
		ImagingDelete(resampledImg);

		MinCity::TextureBoy.AddTextureToTextureArray(_terrainTexture, TEX_TERRAIN);
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

			// Set the height based on perlin noise
			// There are only 8 distinct height levels used 
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
			*(_theGrid + ((yVoxel * Iso::WORLD_GRID_SIZE) + xVoxel)) = oVoxel;

		} while (++xVoxel < Iso::WORLD_GRID_SIZE);

	});

	ImagingDelete(imageNoise);

	// Compute visibility and ambient occlusion based on neighbour occupancy
	ComputeGroundOcclusion();
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
		SMOOTH_MS_ZOOM{ 241 },
		SMOOTH_MS_ROTATE{ 361 };

	// **** camera designed to allow rotation and zooming (no scrolling) while paused

	
	// **** NICE FUCKING SCROLLING ******** BEGIN // frame-rate independent and Very nice fine smooth control of camera heading from the translation distance delta	uint32_t Comp(0);
	static tTime tLastDistance{ tNow }, tLastVelocity{ tNow };
	
	if (zero_time_point != oCamera.tTranslateStart) {

		XMVECTOR const xmTargetDisplacement = XMVectorSubtract(XMLoadFloat2A(&oCamera.TargetPosition), XMLoadFloat2A(&oCamera.Origin));
		float const fDistance = XMVectorGetX(XMVector2Length(xmTargetDisplacement));
		
		XMVECTOR const xmDisplacement(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Y, XM_SWIZZLE_X, XM_SWIZZLE_Y>(XMLoadFloat2A(&oCamera.Displacement)));

		uint32_t Comp(0);
		XMVectorEqualR(&Comp, SFM::abs(xmDisplacement), XMVectorZero());  // absolute, so its tested in either direction
		
		if (XMComparisonAnyFalse(Comp) && oCamera.InitialDistanceToTarget >= 0) {
			
			XMVECTORF32 const xmSpeed{ Iso::CAMERA_TRANSLATE_SPEED, Iso::CAMERA_TRANSLATE_SPEED, 0.0f, 0.0f };

			XMVECTOR xmVelocity = SFM::__fma(XMVectorMultiply(xmDisplacement, _mm_set1_ps(tDelta.count())), xmSpeed, XMLoadFloat2A(&oCamera.Velocity));

			xmVelocity = XMVectorScale(xmVelocity, 1.0f - Iso::CAMERA_DAMPING);

			XMStoreFloat2A(&oCamera.Velocity, xmVelocity);
			XMStoreFloat2A(&oCamera.Origin, XMVectorAdd(XMLoadFloat2A(&oCamera.Origin), xmVelocity));

			oCamera.InitialDistanceToTarget -= XMVectorGetX(XMVector2Length(xmVelocity));

			if (tNow - tLastDistance > milliseconds(Iso::CAMERA_DISTANCE_RESET_MILLISECONDS)) {  // allows a small amount of accumulation to simulate a small amount of instant acceleration =)
				XMStoreFloat2A(&oCamera.Displacement, XMVectorZero());
				tLastDistance = tNow;
			}
			tLastVelocity = tNow;  // keep reseting //

		}
		else {
			oCamera.tTranslateStart = zero_time_point;
		}
	}
	else {

		XMVECTOR xmVelocity = XMLoadFloat2A(&oCamera.Velocity);

		{
			uint32_t Comp(0);
			XMVectorGreaterR(&Comp, SFM::abs(xmVelocity), XMVectorZero());  // absolute, so its tested in either direction

			if (XMComparisonAnyTrue(Comp)) { // only decay to zero if needed
				xmVelocity = XMVectorScale(xmVelocity, 1.0f - Iso::CAMERA_DAMPING);

				XMStoreFloat2A(&oCamera.Velocity, xmVelocity);
				XMStoreFloat2A(&oCamera.Origin, XMVectorAdd(XMLoadFloat2A(&oCamera.Origin), xmVelocity));
			}
		}

		// ensure we decay to zero by reseting explicity after n seconds //
		if (tNow - tLastVelocity > seconds(Iso::CAMERA_VELOCITY_RESET_SECONDS)) {
			XMStoreFloat2A(&oCamera.Velocity, XMVectorZero());
			tLastVelocity = tNow;
		}
	} // **** NICE FUCKING SCROLLING ******** END //
	

	// ZOOM //
	if (zero_time_point != oCamera.tZoomStart) {
		static constexpr fp_seconds const tSmooth = fp_seconds(SMOOTH_MS_ZOOM);

		fp_seconds const tDelta = critical_now() - oCamera.tZoomStart;

		if (tDelta >= tSmooth) { // reset, finished transition
			oCamera.tZoomStart = zero_time_point;
			oCamera.ZoomFactor = oCamera.TargetZoomFactor;
		}
		else {
			float const tInvDelta = SFM::saturate((float)tDelta.count() / (float)tSmooth.count());
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
			float const tInvDelta = SFM::saturate((float)tDelta.count() / (float)tSmooth.count());
			oCamera.Azimuth = v2_rotation_t( SFM::lerp(oCamera.PrevAzimuthAngle, oCamera.TargetAzimuthAngle, tInvDelta) );
		}
	}
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
				translateCamera(vScroll);
				_bMotionDelta = true; // override so that dragging continue
			}
		}

		if (eMouseButtonState::LEFT_PRESSED == _mouseState) {  // LEFT DRAGGING //

			if (_bMotionDelta) {

				if (_bDraggingMouse) {

					MinCity::UserInterface.LeftMouseDragAction(XMLoadFloat2A(&_vMouse), XMLoadFloat2A(&_vDragLast), _tDragStart);

					_vDragLast = _vMouse;
					_tDragStart = now();
				}
			}

		}
		else if (eMouseButtonState::RIGHT_PRESSED == _mouseState) {  // RIGHT DRAGGING - ROTATION //

			if (_bMotionDelta) {
				  
				if (_bDraggingMouse) {
					float const fXDelta = _vMouse.x - _vDragLast.x;

					rotateCamera(fXDelta * tDelta.count());	// bugfix: must be constant delta, not the difference between now() and dragstart

					_vDragLast = _vMouse;
					_tDragStart = now();
				}
			}

		}
		
	}

	// get Origin's location in voxel units
	// range is (-144,-144) TopLeft, to (144,144) BottomRight
	//
	// Clamp Camera Origin and update //
	// big bugfix: must add previous fractional offset, otherwise results in choppy motion *do not change*
	XMVECTOR const xmOrigin = SFM::clamp(XMVectorAdd(XMLoadFloat2A(&oCamera.Origin), XMLoadFloat2A(&oCamera.voxelFractionalGridOffset)),
		_mm_set1_ps(Iso::MIN_VOXEL_FCOORD), _mm_set1_ps(Iso::MAX_VOXEL_FCOORD));
	
		
	point2D_t voxelIndex(v2_to_p2D(xmOrigin));

	point2D_t const visibleRadius(p2D_half(point2D_t(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z))); // want radius, hence the half value

	// get starting voxel in TL corner of screen
	voxelIndex = p2D_sub(voxelIndex, visibleRadius);
	
	// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
	point2D_t const voxelIndex_TopLeft = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));
	oCamera.voxelIndex_TopLeft = p2D_sub(voxelIndex_TopLeft, Iso::GRID_OFFSET);

	point2D_t voxelIndex_Center( SFM::clamp(oCamera.voxelIndex_TopLeft.v, _mm_setzero_si128(), _mm_set1_epi32(Iso::WORLD_GRID_SIZE - 1) ));
	// Change from  (0,0) => (x,y) to  (-x,-y) => (x,y)
	oCamera.voxelIndex_Center = p2D_sub(voxelIndex_Center, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

	// Convert Fractional component from GridSpace
	XMVECTOR const xmFract(SFM::sfract(xmOrigin));  // FOR REALLY SMOOTH SCROLLING //

	XMStoreFloat2A(&oCamera.voxelFractionalGridOffset, xmFract); // shaders - for correction of grid and camera fractional difference *note its already negated just add it to correct

	// big bugfix: must remove fractional offset so any internal or external usage of the world origin does not "double add" the fractional offset
	// the fractional offset is manually added to the 3d camera origin and target
	// this prevents an object translation having the fractional offset in-accurately added to its own origin
	// if both were added this results in choppy motion *do not change*
	XMStoreFloat2A(&oCamera.Origin, XMVectorSubtract(xmOrigin, xmFract));

#ifndef NDEBUG
	static XMVECTOR DebugVariable;
	DebugVariable = xmFract;
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

	if (MinCity::isPaused() && MinCity::isFocused()) {
		rotateCamera(fp_seconds(fixed_delta_duration).count()); // *** slow turntable rotation using delta directly is ok here
	}
	return(xmOrigin);
}

bool const __vectorcall cVoxelWorld::OnMouseMotion(FXMVECTOR xmMotionIn)
{
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
	if (_bMotionDelta) {	// handling motion //

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
			{ // update panning positio
				XMVECTOR const xmFrame(p2D_to_v2(MinCity::getFramebufferSize()));

				XMVECTOR xmPosition = XMVectorSubtract(xmFrame, xmMotionCurrent);  // mouse x,y hiresolution position
				xmPosition = XMVectorMultiply(xmPosition, SFM::rcp(xmFrame)); // normalize [0...1]
				xmPosition = SFM::__fms(xmPosition, _mm_set_ps1(2.0f), _mm_set_ps1(1.0f)); // [-1...1]

				XMStoreFloat2A(&oCamera.Pan, xmPosition);
			}

			// Mouse Move
			cMinCity::UserInterface.MouseMoveAction(xmMotionCurrent);
		}
	}

	return(_bMotionDelta);
}
void cVoxelWorld::OnMouseLeft(int32_t const state)
{
	if (eMouseButtonState::RELEASED == state) {
		
		_bDraggingMouse = false;

		cMinCity::UserInterface.LeftMouseReleaseAction(XMLoadFloat2A(&_vMouse));
	}
	else { // PRESSED

		cMinCity::UserInterface.LeftMousePressAction(XMLoadFloat2A(&_vMouse));

	}


	_mouseState = state;
}
void cVoxelWorld::OnMouseRight(int32_t const state)
{
	if (eMouseButtonState::RELEASED == state) {
		
		_bDraggingMouse = false;
	}
	else { // PRESSED

	}


	_mouseState = state;
}
void cVoxelWorld::OnMouseLeftClick()
{
	cMinCity::UserInterface.LeftMouseClickAction(XMLoadFloat2A(&_vMouse));
}
void cVoxelWorld::OnMouseRightClick()
{
}
void cVoxelWorld::OnMouseScroll(float const delta)
{
	zoomCamera(delta);
}
void cVoxelWorld::OnMouseInactive()
{
	if (eMouseButtonState::RELEASED == _mouseState) {
		_mouseState = eMouseButtonState::INACTIVE;
		_bDraggingMouse = false;
	}
}


namespace world
{
	// World Space (-x,-y) to (X, Y) Coordinates Only - (Camera Origin) - not swizzled
	XMVECTOR const __vectorcall getOrigin()
	{
		return(XMLoadFloat2A(&oCamera.Origin));
	}
	XMVECTOR const __vectorcall getOriginFractionalGridOffsetXY()
	{
		return(XMLoadFloat2A(&oCamera.voxelFractionalGridOffset));
	}
	XMVECTOR const __vectorcall getOriginFractionalGridOffsetXZ()
	{
		return(XMVectorSet(oCamera.voxelFractionalGridOffset.x, 0.0f, oCamera.voxelFractionalGridOffset.y, 0.0f));
	}
	v2_rotation_t const& getAzimuth()
	{
		return(oCamera.Azimuth);
	}

	Iso::Voxel const * const __restrict __vectorcall getVoxelAt(point2D_t voxelIndex)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		// Check bounds
		if ((voxelIndex.x | voxelIndex.y) >= 0) {

			if (voxelIndex.x < Iso::WORLD_GRID_SIZE && voxelIndex.y < Iso::WORLD_GRID_SIZE) {

				return((_theGrid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)));
			}
		}

		return(nullptr);
	}
	Iso::Voxel const * const __restrict __vectorcall getVoxelAt(FXMVECTOR const Location)
	{																																														//          	equal same voxel	
		// still in Grid Space (-x,-y) to (X, Y) Coordinates 
		return(getVoxelAt(v2_to_p2D(Location)));
	}

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict XM_CALLCONV getVoxelAt_IfVisible(FXMVECTOR const Location)
	{
		Iso::Voxel const* const __restrict pVoxel = getVoxelAt(Location);

		if (nullptr != pVoxel) // Test
		{
			// Same Test that RenderGrid Uses, simpler
			// need to transform grid space coordinates to world space coordinates and then swizzle to proper 3d coordinate
			XMVECTOR const xmWorldSpace = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorSubtract(Location, world::getOrigin()));
			if (Volumetric::VolumetricLink->Visibility.SphereTestFrustum(xmWorldSpace, Iso::VOX_RADIUS))
			{
				return(pVoxel);
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
				*(_theGrid + ((voxelIndex.y * Iso::WORLD_GRID_SIZE) + voxelIndex.x)) = std::move(newData);
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

		// oriented rect filling algorithm, see processing sketch "rotation" for reference
		if (vR.sine() >= 0.0f) {

			while (voxelIterate.y <= voxelEnd.y) {

				voxelIterate.x = voxelArea.left;
				while (voxelIterate.x <= voxelEnd.x) {

					point2D_t const p0(p);
					p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

					// back
					if (0 == (p.y - p0.y - 1)) {
						setVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
						Iso::Voxel const* const pVoxel = getVoxelAt(point2D_t(p.x, p0.y));
						if (pVoxel) {
							Iso::Voxel oVoxel(*pVoxel);
							if (!(isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == getExtendedType(oVoxel))) // not accidentally highlighting roads
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
						if (!(isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == getExtendedType(oVoxel))) // not accidentally highlighting roads
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
		else {

			while (voxelIterate.y <= voxelEnd.y) {

				voxelIterate.x = voxelArea.left;
				while (voxelIterate.x <= voxelEnd.x) {

					point2D_t const p0(p);
					p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

					// back
					if (0 == (p0.y - p.y - 1)) {
						setVoxelHashAt<true>(point2D_t(p.x, p0.y), hash);

#ifndef NDEBUG // setting emission when setting hash for model instances (dynamic only)
#ifdef DEBUG_HIGHLIGHT_BOUNDING_RECTS
						Iso::Voxel const* const pVoxel = getVoxelAt(point2D_t(p.x, p0.y));
						if (pVoxel) {
							Iso::Voxel oVoxel(*pVoxel);
							if (!(isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == getExtendedType(oVoxel))) // not accidentally highlighting roads
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
						if (!(isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == getExtendedType(oVoxel))) // not accidentally highlighting roads
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

		// oriented rect filling algorithm, see processing sketch "rotation" for reference
		if (vR.sine() >= 0.0f) {

			while (voxelIterate.y <= voxelEnd.y) {

				voxelIterate.x = voxelArea.left;
				while (voxelIterate.x <= voxelEnd.x) {

					point2D_t const p0(p);
					p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

					// back
					if (0 == (p.y - p0.y - 1)) {
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
		else {

			while (voxelIterate.y <= voxelEnd.y) {

				voxelIterate.x = voxelArea.left;
				while (voxelIterate.x <= voxelEnd.x) {

					point2D_t const p0(p);
					p = p2D_rotate(voxelIterate, voxelArea.center(), vR);

					// back
					if (0 == (p0.y - p.y - 1)) {
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
	}

	Iso::Voxel const* const __restrict getNeighbour(point2D_t voxelIndex, point2D_t const relativeOffset)
	{
		// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
		voxelIndex = p2D_add(voxelIndex, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));

		return(::getNeighbour(voxelIndex, relativeOffset));
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

	void recomputeGroundOcclusion(rect2D_t voxelArea)
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
					uint8_t OcclusionShading(0);

					// old layout: //
					/*

											  [NBR_TL]
								   [NBR_L]				   [NBR_T]
						NBR_BL					VOXEL					NBR_TR
									NBR_B					NBR_R
												NBR_BR

					*/
					// Therefore the neighbours of a voxel, follow same isometric layout/order //
					/*

											  [NBR_TR]
								   [NBR_T]				   [NBR_R]
						NBR_TL					VOXEL					NBR_BR
									NBR_L					NBR_B
												NBR_BL
					*/

					// Side Left = NBR_T (ao.x)
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_T]);
					if (nullptr != pNeighbour) {
						
						if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
							OcclusionShading |= Iso::OCCLUSION_SHADING_SIDE_LEFT;
						}
					}


					// Corner = NBR_TR (ao.y)
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_TR]);
					if (nullptr != pNeighbour) {
						
						if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
							OcclusionShading |= Iso::OCCLUSION_SHADING_CORNER;
						}
					}

					// Side Right = NBR_R (ao.z)
					pNeighbour = world::getNeighbour(voxelIterate, ADJACENT[NBR_R]);
					if (nullptr != pNeighbour) {
						
						if (Iso::getHeightStep(*pNeighbour) > curVoxelHeightStep) {
							OcclusionShading |= Iso::OCCLUSION_SHADING_SIDE_RIGHT;
						}
					}


					// finally, if no flags were set on occlusion, flag as such
					if (0 == OcclusionShading) {
						Iso::clearOcclusion(oVoxel);
					}
					else {	// other wise save computed ao shading
						Iso::setOcclusion(oVoxel, OcclusionShading);
					}

					setVoxelAt(voxelIterate, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}

				++voxelIterate.x;
			}

			++voxelIterate.y;
		}
	}

	// Random //
	point2D_t const __vectorcall getRandomVoxelIndexInArea(rect2D_t const area)
	{
		return(point2D_t(PsuedoRandomNumber32(area.left, area.right),
			PsuedoRandomNumber32(area.top, area.bottom)));
	}
	point2D_t const __vectorcall getRandomVisibleVoxelIndex()
	{
		return(getRandomVoxelIndexInArea(MinCity::VoxelWorld.getVisibleGridBounds()));
	}
	rect2D_t const __vectorcall getRandomNonVisibleAreaNear()
	{
		rect2D_t const
			visible_area(MinCity::VoxelWorld.getVisibleGridBounds());
		rect2D_t
			selected_area{};
		// randomly select area of voxels that are exterior to the visible voxel rect

		// - by direction (kinda like a quadrant, surrounding the visible rect)

		// note that vector indices != direction value after first round of finding a valid quadrant
		// so at no point assume they are equal, it only coindentally starts out that way
		std::vector<uint32_t, tbb::scalable_allocator<uint32_t>> enabledDirections({  eDirection::NW, eDirection::N, eDirection::NE,
																					  eDirection::E, eDirection::SE, eDirection::S,
																					  eDirection::SW, eDirection::W });
		random_shuffle(enabledDirections.begin(), enabledDirections.end()); // adds more randomization at a very low cost

		bool bInvalidDirection(false);

		do
		{
			uint32_t direction;

			bool bIsEnabledDirection(false);
			std::vector<uint32_t>::iterator iterCurrent;

			do {
				uint32_t const pending_direction = (uint32_t const)PsuedoRandomNumber32(0, enabledDirections.size() - 1);

				for (std::vector<uint32_t>::iterator iter = enabledDirections.begin(); iter != enabledDirections.end(); ++iter) {
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
			std::vector<point2D_t, tbb::scalable_allocator<point2D_t>> history; // this is an optimization, rather than using std::unordered_set and finding "dups" every iteration
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

						if (Iso::isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)) {
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

				return(Iso::isExtended(oVoxel) & (Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)));
			}
			return(false);
		}

		bool const __vectorcall search_neighbour_for_road(point2D_t& __restrict found_road_point, point2D_t const origin, point2D_t const offset)
		{
			Iso::Voxel const* const pVoxel = world::getNeighbour(origin, offset);
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isExtended(oVoxel) && (Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel))) {

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

				return(Iso::isExtended(oVoxel) & (Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)));
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
XMGLOBALCONST inline XMVECTORF32 const _voxelGridToLocal{ Volumetric::Allocation::VOXEL_GRID_VISIBLE_X, Volumetric::Allocation::VOXEL_GRID_VISIBLE_Z };
XMGLOBALCONST inline XMVECTORF32 const _xmInverseVisible{ Volumetric::INVERSE_GRID_VISIBLE_X, Volumetric::INVERSE_GRID_VISIBLE_Z, Volumetric::INVERSE_GRID_VISIBLE_X, Volumetric::INVERSE_GRID_VISIBLE_Z };
XMGLOBALCONST inline XMVECTORF32 const _visibleLight{ 2.0f, 1.0f, 2.0f, 1.0f }; // for scaling model extents to account for potential lighting radius

static struct voxelRender // ** static container, all methods and members must be static inline ** //
{
	// this construct significantly improves throughput of voxels, by batching the streaming stores //
	// *and* reducing the contention on the atomic pointer fetch_and_add to nil (Used to profile at 25% cpu utilization on the lock prefix, now is < 0.3%)

	using VoxelLocalBatch = sBatched<VertexDecl::VoxelNormal, eStreamingBatchSize::GROUND>;
	using VoxelThreadBatch = tbb::enumerable_thread_specific<
		VoxelLocalBatch,
		tbb::cache_aligned_allocator<VoxelLocalBatch>,
		tbb::ets_key_per_instance >;
	
	static inline VoxelThreadBatch batchedGround, batchedRoad, batchedRoadTrans;

	
	STATIC_INLINE void XM_CALLCONV RenderGround(XMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		Iso::Voxel const& __restrict oVoxel,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelGround,
		VoxelLocalBatch& __restrict localGround)
	{
		// a more accurate index, based on position which has fractional component, vs old usage of arrayIndex
		XMVECTOR const xmIndex(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorSubtract(xmVoxelOrigin, Iso::GRID_OFFSET_X_Z)));

		if (XMVector4GreaterOrEqual(xmIndex, XMVectorZero()))
		{
			// **** HASH FORMAT 32bits available //

			// Build hash //
			uint32_t groundHash(0);
			groundHash |= Iso::getHeightStep(oVoxel);			//				000R 1111   // msb is reserved (5th bit)
			groundHash |= (Iso::getOcclusion(oVoxel) << 5);		//				111x xxxx
			groundHash |= (Iso::isEmissive(oVoxel) << 8);		// 0000 0001	xxxx xxxx

			// ************ uv.y must be inverted !!!! (bugfix: minimap does not match world map)
			point2D_t voxelWorldUV(voxelIndex);
			//voxelWorldUV.y = (int32_t)(Iso::WORLD_GRID_SIZE - 1) - voxelWorldUV.y;

			XMVECTOR voxelLocalUV;
			{
				{ // bugfix for mismatch between static voxel model lighting not matching location for terrain (inversion here aswell)
					voxelLocalUV = XMVectorSubtract(_voxelGridToLocal, XMVectorSubtract(_voxelGridToLocal, xmIndex));
				}
			}
			// ***xy = world relative UV*****,  ****** zw = visible relative UV ******* see voxelModel.h render() for details

			// fractional offset is in world coordinates
			// allows grid to scroll smoothly
			// for the *signed* fractional range +-0.0 - 0.9999
			// represents a fractional part of the main voxel grid element whose size is VOX_SIZE
			// so a UV coordinate needs to be scaled accordingly to remap the range of oine discrete grid voxel to a 
			// uv range equal to it
			// (1.0f / VOX_SIZE) * fractional world coordinates = normalized fractional coordinates
			// normalized fractional coordinates * INVERSE_GRID_VISIBLE = uv coordinates for "local visible uv range" that we care about
			// add that to the voxel local UV shoulld then apply smooth interpolation of local UV coordinates
			// just like the world coordinates applied to the model origins
			// hope there is enough precision....
			// may need to convert signed fractional range to unsigned...
			// y axis u(V) may be flipped uggghh

			XMVECTOR const xmUVs(SFM::__fma(XMVectorRotateRight<2>(voxelLocalUV), _xmInverseVisible,
				XMVectorScale(p2D_to_v2(voxelWorldUV), Iso::INVERSE_WORLD_GRID_FSIZE))
			); // terrain uses only xz for 3d lightmap sample, as y = 0

			localGround.emplace_back(
				voxelGround,
				
					xmVoxelOrigin,
					xmUVs,
					groundHash
				);
		}
	}


	STATIC_INLINE void XM_CALLCONV RenderRoad(XMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		Iso::Voxel const& __restrict oVoxel,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxelRoad,
		VoxelLocalBatch& __restrict localRoad)
	{
		// a more accurate index, based on position which has fractional component, vs old usage of arrayIndex
		XMVECTOR const xmIndex(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorSubtract(xmVoxelOrigin, Iso::GRID_OFFSET_X_Z)));

		if (XMVector4GreaterOrEqual(xmIndex, XMVectorZero()))
		{
			// **** HASH FORMAT 32bits available //

			// Build hash //
			uint32_t roadHash(0);
			roadHash |= Iso::getHeightStep(oVoxel);							//			     	                      000R 1111   // msb is reserved (5th bit)
			roadHash |= (Iso::getOcclusion(oVoxel) << 5);					//		     		                      111x xxxx
			roadHash |= (Iso::isEmissive(oVoxel) << 8);						//                           0000 0001	  xxxx xxxx
			roadHash |= (uint32_t(Iso::getHash(oVoxel, Iso::GROUND_HASH)) << 9);		// 0000 0001    1111 1111    1111 111x    xxxx xxxx 

			// ************ uv.y must be inverted !!!! (bugfix: minimap does not match world map)
			point2D_t voxelWorldUV(voxelIndex);
			//voxelWorldUV.y = (int32_t)(Iso::WORLD_GRID_SIZE - 1) - voxelWorldUV.y;

			XMVECTOR voxelLocalUV;
			{
				{ // bugfix for mismatch between static voxel model lighting not matching location for terrain (inversion here aswell)
					voxelLocalUV = XMVectorSubtract(_voxelGridToLocal, XMVectorSubtract(_voxelGridToLocal, xmIndex));
				}
			}
			// ***xy = world relative UV*****,  ****** zw = visible relative UV ******* see voxelModel.h render() for details

			// fractional offset is in world coordinates
			// allows grid to scroll smoothly
			// for the *signed* fractional range +-0.0 - 0.9999
			// represents a fractional part of the main voxel grid element whose size is VOX_SIZE
			// so a UV coordinate needs to be scaled accordingly to remap the range of oine discrete grid voxel to a 
			// uv range equal to it
			// (1.0f / VOX_SIZE) * fractional world coordinates = normalized fractional coordinates
			// normalized fractional coordinates * INVERSE_GRID_VISIBLE = uv coordinates for "local visible uv range" that we care about
			// add that to the voxel local UV shoulld then apply smooth interpolation of local UV coordinates
			// just like the world coordinates applied to the model origins
			// hope there is enough precision....
			// may need to convert signed fractional range to unsigned...
			// y axis u(V) may be flipped uggghh

			XMVECTOR const xmUVs(SFM::__fma(XMVectorRotateRight<2>(voxelLocalUV), _xmInverseVisible,
				XMVectorScale(p2D_to_v2(voxelWorldUV), Iso::INVERSE_WORLD_GRID_FSIZE))
			); // terrain uses only xz for 3d lightmap sample, as y = 0

			localRoad.emplace_back(
				voxelRoad,
				
					xmVoxelOrigin,
					xmUVs,
					roadHash
				);
		}
	}


	template<bool const Dynamic, bool const UPDATE_OPACITY, typename VOXELBUFFER_3D, typename VOXELBUFFER_TRANS_3D>
	STATIC_INLINE void XM_CALLCONV RenderModel(uint8_t const index, FXMVECTOR xmVoxelOrigin, point2D_t const& __restrict voxelIndex,
		Iso::Voxel const& __restrict oVoxel,
		tbb::atomic<VOXELBUFFER_3D*>& __restrict voxels,
		tbb::atomic<VOXELBUFFER_TRANS_3D*>& __restrict voxels_trans)
	{
		auto const ModelInstanceHash = Iso::getHash(oVoxel, index);
		auto const FoundModelInstance = MinCity::VoxelWorld.lookupVoxelModelInstance<Dynamic>(ModelInstanceHash);

		if (FoundModelInstance) {
			// visibility too agressive culling light emission from models, so extents are doubled on xz axis
			//if (Volumetric::VolumetricLink->Visibility.AABBTestFrustum(xmVoxelOrigin, XMVectorMultiply(_visibleLight, XMLoadFloat3A(&FoundModelInstance->getModel()._Extents)))) {
				FoundModelInstance->Render<UPDATE_OPACITY>(xmVoxelOrigin, voxelIndex, oVoxel, voxels, voxels_trans);
			//}
		}
	}

	template<uint32_t const MAX_VISIBLE_X, uint32_t const MAX_VISIBLE_Y,
		bool const UPDATE_OPACITY = true,
		bool const RENDER_DYNAMIC = true,
		typename VOXELGROUND, typename VOXELROAD, typename VOXELSTATIC, typename VOXELDYNAMIC>
		static void XM_CALLCONV RenderGrid(point2D_t const voxelStart,
			tbb::atomic<VOXELGROUND*>& __restrict voxelGround,
			tbb::atomic<VOXELROAD*>& __restrict voxelRoad,
			tbb::atomic<VOXELROAD*>& __restrict voxelRoadTrans,
			tbb::atomic<VOXELSTATIC*>& __restrict voxelStatic,
			tbb::atomic<VOXELDYNAMIC*>& __restrict voxelDynamic,
			tbb::atomic<VOXELDYNAMIC*>& __restrict voxelTrans)
	{
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

		point2D_t voxelEnd = p2D_add(voxelReset, iterations);
		voxelEnd = p2D_min(voxelEnd, point2D_t(Iso::WORLD_GRID_SIZE, Iso::WORLD_GRID_SIZE));
#ifdef DEBUG_TEST_FRONT_TO_BACK
		voxelEnd.y -= lines_missing;
#endif
		typedef struct alignas(16) __declspec(novtable) sRenderFuncBlockChunk {

		private:
			point2D_t const 						voxelStart;
			Iso::Voxel const* const __restrict		theGrid;
			tbb::atomic<VOXELGROUND*>& __restrict	voxelGround;
			tbb::atomic<VOXELROAD*>& __restrict		voxelRoad;
			tbb::atomic<VOXELROAD*>& __restrict		voxelRoadTrans;
			tbb::atomic<VOXELSTATIC*>& __restrict	voxelStatic;
			tbb::atomic<VOXELDYNAMIC*>& __restrict	voxelDynamic;
			tbb::atomic<VOXELDYNAMIC*>& __restrict	voxelTrans;

			sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
		public:
			__forceinline explicit __vectorcall sRenderFuncBlockChunk(
				point2D_t const voxelStart_,
				Iso::Voxel const* const __restrict theGrid_,
				tbb::atomic<VOXELGROUND*> & __restrict voxelGround_,
				tbb::atomic<VOXELROAD*> & __restrict voxelRoad_,
				tbb::atomic<VOXELROAD*>& __restrict voxelRoadTrans_,
				tbb::atomic<VOXELSTATIC*> & __restrict voxelStatic_,
				tbb::atomic<VOXELDYNAMIC*> & __restrict voxelDynamic_,
				tbb::atomic<VOXELDYNAMIC*> & __restrict voxelTrans_)
				: voxelStart(voxelStart_),
				voxelGround(voxelGround_), voxelRoad(voxelRoad_), voxelRoadTrans(voxelRoadTrans_), voxelStatic(voxelStatic_), voxelDynamic(voxelDynamic_), voxelTrans(voxelTrans_),
				theGrid(theGrid_)
			{}

			void __vectorcall operator()(tbb::blocked_range2d<int32_t, int32_t> const& r) const {

				VoxelLocalBatch& __restrict localGround(batchedGround.local());
				VoxelLocalBatch& __restrict localRoad(batchedRoad.local());
				VoxelLocalBatch& __restrict localRoadTrans(batchedRoadTrans.local());

				int32_t const	// pull out into registers from memory
					y_begin(r.rows().begin()),
					y_end(r.rows().end()),
					x_begin(r.cols().begin()),
					x_end(r.cols().end());

				for (int32_t y = y_begin; y < y_end; ++y)
				{
					int32_t x = x_begin;

					Iso::Voxel const* __restrict pVoxel = theGrid + ((size_t(y) << size_t(Iso::WORLD_GRID_SIZE_BITS)) + size_t(x));
					_mm_prefetch((const CHAR*)pVoxel, _MM_HINT_T0);

					for (; x < x_end; ++x)
					{
						Iso::Voxel const oVoxel(*pVoxel);
						++pVoxel; // sequentially access for best cache prediction

						// Make index (row,col)relative to starting index voxel
						// calculate origin from relative row,col
						// Add the offset of the world origin calculated
						// Now inside screenspacve coordinates
						// Rendering is FRONT to BACK only (checked)
						point2D_t const voxelIndex(x, y);
						XMVECTOR const xmVoxelOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(p2D_sub(voxelIndex, voxelStart)));

						if (Volumetric::VolumetricLink->Visibility.SphereTestFrustum(xmVoxelOrigin, Iso::VOX_RADIUS)) {
								
							// ***Ground always exists*** //
							RenderGround(xmVoxelOrigin, voxelIndex, oVoxel, voxelGround, localGround);

							if (isExtended(oVoxel))
							{
								switch (getExtendedType(oVoxel))
								{
								case Iso::EXTENDED_TYPE_ROAD:
									if (Iso::isEmissive(oVoxel)) { // any emissive road is transparent
										RenderRoad(xmVoxelOrigin, voxelIndex, oVoxel, voxelRoadTrans, localRoadTrans); // trans road
									}
									RenderRoad(xmVoxelOrigin, voxelIndex, oVoxel, voxelRoad, localRoad); // opaque road

									break;
								case Iso::EXTENDED_TYPE_WATER:
									// todo
									break;
									// default should not exist //
								}
							} // extended
						}					

						if (Iso::isOwner(oVoxel, Iso::STATIC_HASH))	// only roots actually do rendering work. skip all children
						{
#ifndef DEBUG_NO_RENDER_STATIC_MODELS
							RenderModel<false, UPDATE_OPACITY>(Iso::STATIC_HASH, xmVoxelOrigin, voxelIndex, oVoxel, voxelStatic, voxelTrans);
#endif
						} // root
						// a voxel in the grid can have a static model and dynamic model simultaneously
						if constexpr (RENDER_DYNAMIC) {

							if (Iso::isOwnerAny(oVoxel, Iso::DYNAMIC_HASH)) { // only if there are dynamic hashes which this voxel owns
								for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
									if (Iso::isOwner(oVoxel, i)) {

										RenderModel<true, UPDATE_OPACITY>(i, xmVoxelOrigin, voxelIndex, oVoxel, voxelDynamic, voxelTrans);
									}
								}
							}
						}
					} // for

				} // for

			} // operation

		} const RenderFuncBlockChunk;

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		tTime const tGridStart(high_resolution_clock::now());
#endif
		{
			tbb::queuing_rw_mutex::scoped_lock lock(_theGrid._lock, false); // read-only grid access

			tbb::auto_partitioner part; /*load balancing - do NOT change - adapts to variance of whats in the voxel grid*/
			tbb::parallel_for(tbb::blocked_range2d<int32_t, int32_t>(voxelReset.y, voxelEnd.y, eThreadBatchGrainSize::GRID_RENDER_2D,
				voxelReset.x, voxelEnd.x, eThreadBatchGrainSize::GRID_RENDER_2D), // **critical loop** // debug will slow down to 100ms+ / frame if not parallel //
				RenderFuncBlockChunk(voxelStart, _theGrid, voxelGround, voxelRoad, voxelRoadTrans, voxelStatic, voxelDynamic, voxelTrans), part
			);
		}
		// ####################################################################################################################
		// ensure all batches are output and cleared for next trip
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
} inline voxelRender; // end ns voxelRender

template<typename VertexDeclaration>
struct alignas(32) voxelBuffer {
	vku::GenericBuffer								stagingBuffer[2];
};

// all instances belong in here
inline static struct alignas(32) voxelData
{
	inline static struct alignas(32) {
		voxelBuffer<VertexDecl::VoxelDynamic>
			opaque;
		voxelBuffer<VertexDecl::VoxelDynamic>
			trans;
	} visibleDynamic;

	voxelBuffer<VertexDecl::VoxelNormal>
		visibleStatic;

	inline static struct alignas(32) {
		voxelBuffer<VertexDecl::VoxelNormal>
			opaque;
		voxelBuffer<VertexDecl::VoxelNormal>
			trans;
	} visibleRoad;

	voxelBuffer<VertexDecl::VoxelNormal>
		visibleTerrain;

} voxels;

namespace Volumetric
{
	voxLink* VolumetricLink(nullptr);
}
namespace world
{
	cVoxelWorld::cVoxelWorld()
		:
		_lastState{}, _currentState{}, _targetState{},
		_vMouse{},
		_terrainTexture(nullptr), _roadTexture(nullptr), _blackbodyTexture(nullptr),
		_blackbodyImage(nullptr),
		_sequence(GenerateVanDerCoruptSequence<30, 2>()),
		_activeExplosion(nullptr), _activeTornado(nullptr), _activeShockwave(nullptr), _activeRain(nullptr)
#ifdef DEBUG_STORAGE_BUFFER
		, DebugStorageBuffer(nullptr)
#endif
	{
		Volumetric::VolumetricLink = new Volumetric::voxLink{ *this, _OpacityMap, _Visibility };
	}

	void cVoxelWorld::LoadTextures()
	{
		MinCity::TextureBoy.Initialize();

		// Load bluenoise *** must be done here first so that noise is available
		{
			supernoise::blue.Load(TEXTURE_DIR L"bluenoise.8bit");
#ifndef NDEBUG
			// validation test - save blue noise texture from resulting 1D blue noise function
			ImagingMemoryInstance* imgNoise = ImagingNew(eIMAGINGMODE::MODE_L, BLUENOISE_DIMENSION_SZ, BLUENOISE_DIMENSION_SZ);

			size_t psuedoFrame(0);

			for (int32_t y = BLUENOISE_DIMENSION_SZ - 1; y >= 0; --y) {
				for (int32_t x = BLUENOISE_DIMENSION_SZ - 1; x >= 0; --x) {
					imgNoise->block[y * BLUENOISE_DIMENSION_SZ + x] = SFM::saturate_to_u8(supernoise::blue.get1D(psuedoFrame++) * 255.0f);
				}
			}

			ImagingSaveToKTX(imgNoise, DEBUG_DIR "noisetest.ktx");

			ImagingDelete(imgNoise);
#endif
		}
		
		// other textures:
		MinCity::TextureBoy.LoadKTXTexture<true>(_roadTexture, TEXTURE_DIR L"road_array.ktx");
		MinCity::TextureBoy.AddTextureToTextureArray(_roadTexture, TEX_ROAD);

		Imaging const blackbodyImage(ImagingLoadRawBGRA(TEXTURE_DIR "blackbody_real.data", BLACKBODY_IMAGE_WIDTH, 1));
		MinCity::TextureBoy.ImagingToTexture(blackbodyImage, _blackbodyTexture);
		MinCity::TextureBoy.AddTextureToTextureArray(_blackbodyTexture, TEX_BLACKBODY);

		_blackbodyImage = blackbodyImage; // save image to be used for blackbody radiation light color lookup
#ifndef NDEBUG
		ImagingSaveToKTX(blackbodyImage, DEBUG_DIR "blackbodytest.ktx");
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
			_theGrid._protected = (Iso::Voxel* const __restrict)__memalloc_large(sizeof(Iso::Voxel) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE, 64);
			FMT_LOG(VOX_LOG, "world grid voxel allocation: {:n} bytes", (size_t)(sizeof(_theGrid[0]) * Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE));
		}
		
		GenerateGround();
#ifndef NDEBUG
		OutputVoxelStats();
#endif
		_OpacityMap.create(MinCity::Vulkan.getDevice(), MinCity::Vulkan.computePool(), MinCity::Vulkan.computeQueue(0), MinCity::getFramebufferSize());
		MinCity::PostProcess.create(MinCity::Vulkan.getDevice(), MinCity::Vulkan.transientPool(), MinCity::Vulkan.graphicsQueue(), MinCity::getFramebufferSize());
		createAllBuffers();
		
		Volumetric::LoadAllVoxelModels();

#ifdef DEBUG_STORAGE_BUFFER
		DebugStorageBuffer = new vku::StorageBuffer(MinCity::Vulkan.getDevice(), MinCity::Vulkan.getMemProps(), sizeof(UniformDecl::DebugStorageBuffer), false, vk::BufferUsageFlagBits::eTransferDst);
		DebugStorageBuffer->upload(MinCity::Vulkan.getDevice(), MinCity::Vulkan.getMemProps(), MinCity::Vulkan.transientPool(), MinCity::Vulkan.graphicsQueue(), init_debug_buffer);
#endif
	}

	// private specializations of placeXXXInstanceAt
	cCopterGameObject* const cVoxelWorld::placeCopterInstanceAt(point2D_t const voxelIndex)
	{
		// body
		auto const [hash_parent, instance_parent] = placeVoxelModelInstanceAt<Volumetric::eVoxelModels_Dynamic::MISC>(voxelIndex, Volumetric::eVoxelModels_Indices::COPTER_BODY, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE);

		if (instance_parent) {

			auto const [hash_prop, instance_prop] = placeVoxelModelInstanceAt<Volumetric::eVoxelModels_Dynamic::MISC>(voxelIndex, Volumetric::eVoxelModels_Indices::COPTER_PROP, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE | Volumetric::eVoxelModelInstanceFlags::CHILD_ONLY);

			if (instance_prop) {
				instance_parent->setChild(instance_prop);

				return(&cCopterGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash_parent]));
			}
		}

#ifndef NDEBUG
		{
			FMT_LOG_WARN(VOX_LOG, "placeCopterInstanceAt<{:s}> failed. modelIndex({:d}) of modelGroup({:d}) at voxelIndex({:d},{:d})",
				"dynamic", Volumetric::eVoxelModels_Indices::COPTER_BODY, Volumetric::eVoxelModels_Dynamic::MISC, voxelIndex.x, voxelIndex.y);
		}
#endif
		return(nullptr);
	}

	void cVoxelWorld::OnLoaded(tTime const& __restrict tNow)
	{
		// Initialize Car/Traffic simulation
		::cCarGameObject::Initialize(tNow);
		::cPoliceCarGameObject::Initialize(tNow);

#ifdef DEBUG_DEPTH_CUBE

		placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(getVisibleGridCenter(),
						Volumetric::eVoxelModels_Indices::DEPTH_CUBE,
						Volumetric::eVoxelModelInstanceFlags::INSTANT_CREATION);
#endif
		{ // test dynamics 
			cTestGameObject* pGameObj;

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
					pGameObj = placeUpdateableInstanceAt<cTestGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(p2D_add(getVisibleGridCenter(), point2D_t(25, 25)),
						Volumetric::eVoxelModels_Indices::VOODOO_SKULL);
					pGameObj->getModelInstance()->setTransparency(Volumetric::eVoxelTransparency::ALPHA_75);
				}
			}
		}


		{ // copter dynamic
			cCopterGameObject* pGameObj = placeCopterInstanceAt(p2D_add(getVisibleGridCenter(), point2D_t(-25, -25)));
			if (pGameObj) {
				FMT_LOG_OK(GAME_LOG, "Copter launched");
			}
			else {
				FMT_LOG_FAIL(GAME_LOG, "No Copter for you - nullptr");
			}
		}

		// rain!!!
		_activeRain = new sRainInstance(XMVectorZero(), 128.0f);
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
			tbb::queuing_rw_mutex::scoped_lock lock(_theGrid._lock, false); // read-only access
			memcpy(gridSnapshot, _theGrid, gridSz);
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
		Iso::Voxel* const __restrict theGrid = (Iso::Voxel* const __restrict)__memalloc_large(gridSz);

		memcpy(theGrid, new_grid, gridSz);

		// the sizes are the same for the new grid, and existing grid so we could just memcpy 
		// however to ensure atomicity we would rather do a interlocked pointer swap, making this routine threadsafe
		Iso::Voxel* __restrict oldGrid(nullptr);
		{
			// the data inside of the pointer (memory allocated, voxels of the grid) needs to be protected by a mutex
			// to be thread safe. This lock has very little contention put on it from this side, however if the lock is already 
			// obtained by RenderGrid the wait this function will have is high (time for RenderGrid to complete)

			tbb::queuing_rw_mutex::scoped_lock lock(_theGrid._lock, true); // write access
			oldGrid = (Iso::Voxel*)_InterlockedExchangePointer((PVOID * __restrict)&_theGrid._protected, theGrid);
		}
		// free the old grid's large allocation
		if (oldGrid) {
			__memfree_large(oldGrid, gridSz);
		}
	}

} // end ns world

namespace world
{
	rect2D_t const cVoxelWorld::getVisibleGridBounds() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		point2D_t const gridBounds(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z);

		rect2D_t const area(r2D_set_by_width_height(point2D_t{}, gridBounds));
		// this specifically does not clamp - to not modify the width/height of the visible rect
		// ie.) randomVoxel functions depend on this
		return(r2D_add(area, p2D_sub(oCamera.voxelIndex_Center, p2D_half(gridBounds))));
	}
	rect2D_t const cVoxelWorld::getVisibleGridBoundsClamped() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		point2D_t const gridBounds(Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z);
		rect2D_t area(r2D_set_by_width_height(point2D_t{}, gridBounds));

		area = r2D_add(area, p2D_sub(oCamera.voxelIndex_Center, p2D_half(gridBounds)));

		// clamp to world grid
		area = r2D_min(area, Iso::MAX_VOXEL_COORD);
		area = r2D_max(area, Iso::MIN_VOXEL_COORD);

		return(area);
	}
	point2D_t const& cVoxelWorld::getVisibleGridCenter() const // Grid Space (-x,-y) to (x, y) Coordinates Only
	{
		return(oCamera.voxelIndex_Center);
	}
	XMVECTOR const XM_CALLCONV cVoxelWorld::getOrigin() const // World Space (-x,-y) ... (x,y) - not swizzled
	{
		return(world::getOrigin());
	}
	v2_rotation_t const& cVoxelWorld::getAzimuth() const
	{
		return(world::getAzimuth());
	}
	float const	cVoxelWorld::getZoomFactor() const
	{
		return(oCamera.ZoomFactor);
	}
	void XM_CALLCONV cVoxelWorld::setCameraOrigin(FXMVECTOR const xmOrigin) // Grid Space (-x,-y) to (x, y) Coordinates Only (Iso::MIN_VOXEL_COORD...Iso::MAX_VOXEL_COORD)
	{
		// Clamp Camera Origin and update //
		XMVECTOR const xmNewOrigin = SFM::clamp(xmOrigin, _mm_set1_ps(Iso::MIN_VOXEL_FCOORD), _mm_set1_ps(Iso::MAX_VOXEL_FCOORD));
		XMStoreFloat2A(&oCamera.Origin, xmNewOrigin);
	}

	void cVoxelWorld::createAllBuffers()
	{
		// ##### allocation totals for gpu buffers of *minigrid* voxels are always halved. requirement/assumption is given
		// a linear access pattern (rendering) - only half of the total volume is occupied ####
		// main vertex buffer for static voxels - **** buffer allocation sqrt'd and doubled

		// these are cpu side "staging" buffers, matching in size to the gpu buffers that are allocated for them in cVulkan
		// *** these staging buffers are dynamic, the active size is reset to zero for a reason here.
		for (uint32_t i = 0; i < 2; ++i) {
			voxels.visibleDynamic.opaque.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelDynamic), true);
			voxels.visibleDynamic.opaque.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleDynamic.trans.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelDynamic), true);
			voxels.visibleDynamic.trans.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleStatic.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal), true);
			voxels.visibleStatic.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleTerrain.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal), true);
			voxels.visibleTerrain.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleRoad.opaque.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal), true);
			voxels.visibleRoad.opaque.stagingBuffer[i].setActiveSizeBytes(0);

			voxels.visibleRoad.trans.stagingBuffer[i].createAsStagingBuffer(
				Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal), true);
			voxels.visibleRoad.trans.stagingBuffer[i].setActiveSizeBytes(0);
		}


		// shared buffer and other buffers
		{
			point2D_t const frameBufferSize(MinCity::getFramebufferSize());
			size_t const buffer_size(frameBufferSize.x * frameBufferSize.y * sizeof(uint8_t));

			_buffers.reset_subgroup_layer_count_max.createAsStagingBuffer(buffer_size);
			_buffers.reset_subgroup_layer_count_max.clearLocal();  // ensure reset buffer contains all zeroes
			_buffers.subgroup_layer_count_max = vku::StorageBuffer(buffer_size, false, vk::BufferUsageFlagBits::eTransferDst);

			_buffers.reset_shared_buffer.createAsStagingBuffer(sizeof(BufferDecl::VoxelSharedBuffer));
			_buffers.reset_shared_buffer.clearLocal();  // ensure reset buffer contains all zeroes
			_buffers.shared_buffer = vku::StorageBuffer(sizeof(BufferDecl::VoxelSharedBuffer), false, vk::BufferUsageFlagBits::eTransferDst);
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
		// clears w/o requirement of being mapped //
		_OpacityMap.clear();


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


		// mapped clears //
		// bugfix: tried running the clears in parallel, causes flickering voxel models
		__memclr_aligned_32<true>(MappedVoxels_Terrain_Start, voxels.visibleTerrain.stagingBuffer[resource_index].activesizebytes());
		__memclr_aligned_32<true>(MappedVoxels_Road_Start[Volumetric::eVoxelType::opaque], voxels.visibleRoad.opaque.stagingBuffer[resource_index].activesizebytes());
		__memclr_aligned_32<true>(MappedVoxels_Road_Start[Volumetric::eVoxelType::trans], voxels.visibleRoad.trans.stagingBuffer[resource_index].activesizebytes());
		__memclr_aligned_32<true>(MappedVoxels_Static_Start, voxels.visibleStatic.stagingBuffer[resource_index].activesizebytes());
		__memclr_aligned_16<true>(MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::opaque], voxels.visibleDynamic.opaque.stagingBuffer[resource_index].activesizebytes());
		__memclr_aligned_16<true>(MappedVoxels_Dynamic_Start[Volumetric::eVoxelType::trans], voxels.visibleDynamic.trans.stagingBuffer[resource_index].activesizebytes());

		__streaming_store_fence(); // ensure "streaming" clears are coherent



		voxelRender::RenderGrid<Iso::SCREEN_VOXELS_X, Iso::SCREEN_VOXELS_Z>(
			oCamera.voxelIndex_TopLeft,
			MappedVoxels_Terrain,
			MappedVoxels_Road[Volumetric::eVoxelType::opaque], MappedVoxels_Road[Volumetric::eVoxelType::trans],
			MappedVoxels_Static,
			MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque], MappedVoxels_Dynamic[Volumetric::eVoxelType::trans]);

		{ // voxels //
			vku::VertexBufferPartition* const __restrict& __restrict dynamic_partition_info_updater(MinCity::Vulkan.getDynamicPartitionInfo());

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

			// voxel explosion //
			if (_activeExplosion) {
				if (isExplosionVisible(_activeExplosion)) {
					RenderExplosion(_activeExplosion, MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque]);
				}
			}
			{ // EXPLOSION
				VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
				size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - running_offset_start;
				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_EXPLOSION].active_vertex_count = (uint32_t const)current_dynamic_size;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_EXPLOSION].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += current_dynamic_size;
				running_offset_start = MappedVoxels_Dynamic_Current;
#ifdef DEBUG_EXPLOSION_COUNT
				if (dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_EXPLOSION].active_vertex_count) {
					FMT_NUKLEAR_DEBUG(false, "Explosion {:d} voxels being rendered", dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_EXPLOSION].active_vertex_count);
				}
				else {
					FMT_NUKLEAR_DEBUG_OFF();
				}
#endif
			}

			// voxel tornado //
			if (_activeTornado) {
				if (isTornadoVisible(_activeTornado)) {
					RenderTornado(_activeTornado, MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque]);
				}
			}
			{ // TORNADO
				VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
				size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - running_offset_start;
				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_TORNADO].active_vertex_count = (uint32_t const)current_dynamic_size;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_TORNADO].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += current_dynamic_size;
				running_offset_start = MappedVoxels_Dynamic_Current;
#ifdef DEBUG_TORNADO_COUNT
				if (dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_TORNADO].active_vertex_count) {
					FMT_NUKLEAR_DEBUG(false, "Tornado {:d} voxels being rendered", dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_TORNADO].active_vertex_count);
				}
				else {
					FMT_NUKLEAR_DEBUG_OFF();
				}
#endif
			}

			// voxel shockwave //
			if (_activeShockwave) {
				if (isShockwaveVisible(_activeShockwave)) {
					RenderShockwave(_activeShockwave, MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque]);
				}
			}
			{ // SHOCKWAVE
				VertexDecl::VoxelDynamic* const __restrict MappedVoxels_Dynamic_Current = MappedVoxels_Dynamic[Volumetric::eVoxelType::opaque];
				size_t const current_dynamic_size = MappedVoxels_Dynamic_Current - running_offset_start;
				// set the parent / main partition info
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_SHOCKWAVE].active_vertex_count = (uint32_t const)current_dynamic_size;
				dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_SHOCKWAVE].vertex_start_offset = (uint32_t const)running_offset_size;
				running_offset_size += current_dynamic_size;
				running_offset_start = MappedVoxels_Dynamic_Current;
#ifdef DEBUG_SHOCKWAVE_COUNT
				if (dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_SHOCKWAVE].active_vertex_count) {
					FMT_NUKLEAR_DEBUG(false, "Shockwave {:d} voxels being rendered", dynamic_partition_info_updater[eVoxelDynamicVertexBufferPartition::VOXEL_SHADER_SHOCKWAVE].active_vertex_count);
				}
				else {
					FMT_NUKLEAR_DEBUG_OFF();
				}
#endif
			}


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
			vku::VertexBufferPartition* const __restrict& __restrict road_partition_info_updater(MinCity::Vulkan.getRoadPartitionInfo());

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
		
		
		// Update stagingBuffere inside this thread (no two memory regions are overlapping while mapped across threads)
		_OpacityMap.commit(resource_index); // copy lightbuffer from cpu friendly read-write normal memory to potentially write combined (writeonly) buffer


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

		vk::Device const& device(MinCity::Vulkan.getDevice());

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
		return(_OpacityMap.renderCompute(std::forward<vku::compute_pass&& __restrict>(c), render_data));
	}
	void cVoxelWorld::Transfer(uint32_t const resource_index, vk::CommandBuffer& __restrict cb,
		vku::DynamicVertexBuffer* const* const& __restrict vbo, vku::UniformBuffer& __restrict ubo)
	{
		vk::CommandBufferBeginInfo bi(vk::CommandBufferUsageFlagBits::eOneTimeSubmit); // updated every frame
		cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::DYNAMIC);

		{ // ######################### STAGE 1 - UBO UPDATE, OTHER BUFFERS //
			cb.updateBuffer(
				ubo.buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform), (const void*)&_currentState.Uniform
			);

			{
				_buffers.shared_buffer.uploadDeferred(cb, _buffers.reset_shared_buffer);
				_buffers.subgroup_layer_count_max.uploadDeferred(cb, _buffers.reset_subgroup_layer_count_max);
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
				// *Required* - solves a bug with flickering geometry, vulkan.cpp has the corresponding "acquire" operation in static command buffer operation
				static constexpr size_t const buffer_count(3ULL);
				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ vbo[eVoxelVertexBuffer::VOXEL_TERRAIN], vbo[eVoxelVertexBuffer::VOXEL_STATIC], vbo[eVoxelVertexBuffer::VOXEL_DYNAMIC] };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexInput,
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
				);
			}

			// *Required* - solves a bug with trailing voxels, vulkan.cpp has the corresponding "acquire" operation in static command buffer operation
			// see https://www.khronos.org/registry/vulkan/specs/1.0/html/chap6.html#synchronization-memory-barriers under BufferMemoryBarriers
			ubo.barrier( // ## RELEASE ## //
				cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexShader,
				vk::DependencyFlagBits::eByRegion,
				vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
			);

			{ // ## RELEASE ## //
				static constexpr size_t const buffer_count(2ULL);
				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &_buffers.shared_buffer, &_buffers.subgroup_layer_count_max };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eFragmentShader,  // first usage is in z only pass in voxel_clear.frag
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
				);
			}
		}

		cb.end();	// ********* command buffer end is called here only //

		// do not believe this is neccessary __streaming_store_fence(); // ensure writes are coherent before queue submission
	}
	void cVoxelWorld::AcquireTransferQueueOwnership(vk::CommandBuffer& __restrict cb)
	{
		// transfer queue ownership of buffers *required* see voxelworld.cpp Transfer() function
		{ // ## ACQUIRE ## //
			static constexpr size_t const buffer_count(2ULL);
			std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &_buffers.shared_buffer, &_buffers.subgroup_layer_count_max };
			vku::GenericBuffer::barrier(buffers, // ## ACQUIRE ## // batched 
				cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eFragmentShader, // first usage is in z only pass in voxel_clear.frag
				vk::DependencyFlagBits::eByRegion,
				vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
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

	void __vectorcall cVoxelWorld::zoomCamera(float const inout)
	{
		static tTime tLast{ zero_time_point };

		tTime const tNow(critical_now());
		if (tNow - tLast > fixed_delta_x2_duration)
		{
			if (::zoomCamera(SFM::round_to_i32(inout) > 0 ? -1 : 1)) { // mouse delta is linear steps of 1 only allowed, also is inverted
				tLast = tNow;
			}
		}
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
		static tTime tLast{ zero_time_point };

		tTime const tNow(critical_now());
		if (tNow - tLast > fixed_delta_x2_duration)
		{
			if (::rotateCamera(anglerelative)) {
				tLast = tNow;
			}
		}
	}

	STATIC_INLINE bool const XM_CALLCONV translateCamera(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does not orient in current direction of camera
	{
		uint32_t Result;
		XMVectorEqualR(&Result, XMVectorZero(), xmDisplacement);
		if (XMComparisonAnyFalse(Result)) {

			XMVECTOR const xmOrigin(XMLoadFloat2A(&oCamera.Origin));

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, xmPosition);
			XMStoreFloat2A(&oCamera.Displacement, XMVectorAdd(XMLoadFloat2A(&oCamera.Displacement), xmDisplacement));
			XMStoreFloat(&oCamera.InitialDistanceToTarget, XMVector2Length(xmDisplacement));

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

			XMVECTOR const xmOrigin(XMLoadFloat2A(&oCamera.Origin));

			// orient displacement in direction of camera
			xmDisplacement = v2_rotate(xmDisplacement, oCamera.Azimuth);

			XMVECTOR const xmPosition = XMVectorAdd(xmOrigin, xmDisplacement);

			XMStoreFloat2A(&oCamera.TargetPosition, xmPosition);

			float fDisplacementLength;
			XMStoreFloat(&fDisplacementLength, XMVector2Length(xmDisplacement));

			// get actual normal and scale it by the displacement length
			xmDisplacement = XMVectorScale(XMVector2Normalize(XMVectorSubtract(xmPosition, xmOrigin)), fDisplacementLength);

			// accumulate displacement
			XMStoreFloat2A(&oCamera.Displacement, XMVectorAdd(XMLoadFloat2A(&oCamera.Displacement), xmDisplacement));
			XMStoreFloat(&oCamera.InitialDistanceToTarget, XMVector2Length(xmDisplacement));

			// signal transition
			oCamera.tTranslateStart = now();

			return(true); // translation started
		}

		return(false); // translation cancelled
	}

	void __vectorcall cVoxelWorld::translateCamera(point2D_t const vDir)	// intended for scrolling from mouse hitting screen extents
	{
		static tTime tLast{ zero_time_point };

		tTime const tNow(now());
		if (tNow - tLast > fixed_delta_x2_duration)
		{
			XMVECTOR xmIso = p2D_to_v2(point2D_t(-vDir.y, vDir.x)); // must be swapped and 1st component negated
			xmIso = XMVector2Normalize(xmIso);
			xmIso = v2_rotate(xmIso, v2_rotation_constants::v225); // 225 degrees is perfect rotation to align isometry to window up/down left/right
			
			// this makes scrolling on either axis the same constant step, otherwise scrolling on xaxis is faster than yaxis
			point2D_t const absDir(p2D_abs(vDir));
			if (absDir.x > absDir.y) {
				xmIso = XMVectorScale(xmIso, Iso::CAMERA_SCROLL_DISTANCE);
			}
			else {
				xmIso = XMVectorScale(xmIso, Iso::CAMERA_SCROLL_DISTANCE * cMinCity::getFramebufferAspect());
			}
			
			if (::translateCameraOrient(xmIso)) { // this then scrolls in direction camera is currently facing correctly
				tLast = tNow;
			}
		}
	}

	void XM_CALLCONV cVoxelWorld::translateCamera(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does not orient in current direction of camera
	{
		static tTime tLast{ zero_time_point };

		tTime const tNow(now());
		if (tNow - tLast > fixed_delta_x2_duration)
		{
			if (::translateCamera(xmDisplacement)) {
				tLast = tNow;
			}
		}
	}

	void XM_CALLCONV cVoxelWorld::translateCameraOrient(FXMVECTOR const xmDisplacement)  // simpler version for general usage, does not orient in current direction of camera
	{
		static tTime tLast{ zero_time_point };

		tTime const tNow(now());
		if (tNow - tLast > fixed_delta_x2_duration)
		{
			if (::translateCameraOrient(xmDisplacement)) {
				tLast = tNow;
			}
		}
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

			point2D_t const rawData = MinCity::Vulkan.queryMouseBuffer(XMLoadFloat2A(&_vMouse));

			if (!rawData.isZero()) { // bugfix for when mouse hovers out of grid bounds 

				// normalize from [0...65535] to [0.0f....1.0f] //
				XMVECTOR xmVoxelIndex(p2D_to_v2(rawData));
				xmVoxelIndex = XMVectorScale(xmVoxelIndex, INV_MAX_VALUE);

				// change normalized range to voxel grid range [0.0f....1.0f] to [-WORLD_GRID_FHALFSIZE, WORLD_GRID_FHALFSIZE]
				xmVoxelIndex = SFM::__fms(xmVoxelIndex, _mm_set1_ps(Iso::WORLD_GRID_FSIZE), _mm_set1_ps(Iso::WORLD_GRID_FHALFSIZE));

				point2D_t const voxelIndex(v2_to_p2D(xmVoxelIndex));

				if (!voxelIndex.isZero()) {

					_voxelIndexHover.v = voxelIndex.v;
				}
			}
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

	void __vectorcall cVoxelWorld::UpdateUniformStateLatest()
	{
		// This method is called at the last available point before the *current* uniform state is uploaded to the gpu
		//
		// eg.) 
		//      both UpdateUniformStateTarget & UpdateUniformState are called before the resource staging in the main voxelkworld update()
		//      This synchronizes usage of this value with the current frame so that Compute uses the same value as Rendering aswell.

		// currently no uniforms need to be set here
	}

	void __vectorcall cVoxelWorld::UpdateUniformState(float const tRemainder)  // interpolated sub frame state also containing anything that needs to be updated every frame
	{
		static constexpr float const MAX_DELTA = duration_cast<fp_seconds>(fixed_delta_x2_duration).count(),		// 33ms
									 MIN_DELTA = MAX_DELTA * 0.25f;													// 8ms
		static float time_last(0.0f), // last interpolated time
					 time_delta_last(0.0f); // last interpolated time delta

		_currentState.time = SFM::lerp(_lastState.time, _targetState.time, tRemainder);

		// clamp at the 2x step size, don't care or want spurious spikes of time
		float const time_delta = SFM::clamp(_currentState.time - time_last, MIN_DELTA, MAX_DELTA);

		//pack into vector for uniform buffer layout
		_currentState.Uniform.aligned_data0 = XMVectorSetZ(_currentState.Uniform.aligned_data0, (time_delta + time_delta_last) * 0.5f); // z = frame time delta (average of this frame and last frames delta to smooth out large changes between frames)
		_currentState.Uniform.aligned_data0 = XMVectorSetW(_currentState.Uniform.aligned_data0, _currentState.time); // w = time

		_currentState.Uniform.frame = (uint32_t)MinCity::getFrameCount();		// todo check overflow of 32bit frame counter for shaders

		time_last = _currentState.time; // update last interpolated time
		time_delta_last = time_delta;	//   ""    ""       ""      time delta

		// view matrix derived from eyePos
		// the fractional offset here cancels out the "staircase" pattern that would be evident when scrolling - **important that the *target* gridoffset is used
		// swizzle is ONLY for 2D vector to 3D vector conversion, placing x,y into x & z
		// evrything is still in world transform where xyz = width, height, depth
		XMVECTOR const xmOffset = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMLoadFloat2A(&oCamera.voxelFractionalGridOffset));
		_currentState.Uniform.eyePos = XMVectorAdd(SFM::lerp(_lastState.Uniform.eyePos, _targetState.Uniform.eyePos, tRemainder), xmOffset);
		_currentState.Uniform.eyeDir = XMVector3Normalize(XMVectorSubtract(_currentState.Uniform.eyePos, xmOffset)); // target is always 0,0,0 (not including fractional grid offset)

		// **panning is isolated to not affect raymarching uniformity**
#ifdef PANNING_ENABLED
		_currentState.pan = SFM::lerp(_lastState.pan, _targetState.pan, tRemainder);
#else
		_currentState.pan = XMVectorZero();
#endif
		
		XMMATRIX const xmView = XMMatrixLookAtLH(XMVectorAdd(_currentState.Uniform.eyePos, _currentState.pan),
												 XMVectorAdd(_currentState.pan, xmOffset), Iso::xmUp);
		_currentState.Uniform.view = xmView;

		static XMFLOAT3A lastEyePos{};

		if (XMVector3NotEqual(XMLoadFloat3A(&lastEyePos), _currentState.Uniform.eyePos)) { // panning does not change visible section of opacitymap, only scrolling does
			_OpacityMap.UpdateViewMatrix(xmView);
			XMStoreFloat3A(&lastEyePos, _currentState.Uniform.eyePos);
		}

		_currentState.Uniform.inv_view = XMMatrixInverse(nullptr, xmView);

		// Update Frustum, which updates projection matrix, which is derived from ZoomFactor
		_currentState.zoom = SFM::lerp(_lastState.zoom, _targetState.zoom, tRemainder);
		_Visibility.UpdateFrustum(xmView, _currentState.zoom, MinCity::getFrameCount());

		// Get current projection matrix after update of frustum and in turn projection
		_currentState.Uniform.proj = _Visibility.getProjectionMatrix();

		_currentState.Uniform.viewProj = XMMatrixMultiply(xmView, _currentState.Uniform.proj);  // matrices can not be interpolated effectively they must be recalculated
																										// from there base components
	}
	void __vectorcall cVoxelWorld::UpdateUniformStateTarget(tTime const& __restrict tNow, tTime const& __restrict tStart, bool const bFirstUpdate) // fixed timestep state
	{
		_lastState = _targetState;

		_targetState.time = fp_seconds(tNow - tStart).count();

		// "XMMatrixInverse" found to be imprecise for acquiring vector components (forward, direction)
		// using original values instead (-----precise))
		_targetState.Uniform.eyePos = v3_rotate_azimuth(Iso::xmEyePt_Iso, oCamera.Azimuth);

		XMVECTOR const xmPan = XMVectorSwizzle<XM_SWIZZLE_Y, XM_SWIZZLE_Z, XM_SWIZZLE_X, XM_SWIZZLE_W>(XMVectorScale(XMLoadFloat2A(&oCamera.Pan), 20.0f));
		_targetState.pan = v3_rotate_azimuth(xmPan, oCamera.Azimuth + Iso::AzimuthIsometric);

		_targetState.zoom = oCamera.ZoomFactor;
		
		// move state to last target
		UpdateUniformState(bFirstUpdate ? 1.0f : 0.0f);
	}
	void cVoxelWorld::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, bool const bPaused, bool const bFirstUpdate)
	{
		tTime const tStart(start());

		// 35.264 degrees up, 45 degrees left - True Isometric View Projection (requires orthographic projection matrix)
		/*          +  eye
				  / |
				/   |
			  /     |
			+-------+
		lookat				for 120 units -z, at 35.264 degrees lookat angle, height equals 84.85158 units +y
		*/

		// ***** Anything that will affect uniform state BEFORE
		/*XMVECTOR const xmOrigin =*/ UpdateCamera(tNow, tDelta);

		//###########################################################//
		UpdateUniformStateTarget(critical_now(), tStart, bFirstUpdate);
		//###########################################################//
		
		[[unlikely]] if ( eExclusivity::MAIN != cMinCity::getExclusivity() ) // ** Only after this point exists game update related code
			return;															 // ** above is independent, and is compatible with all alternative exclusivity states (LOADING/SAVING/etc.)

		// ***** Anything that uses uniform state updates AFTER
		if (_bMotionDelta || oCamera.Motion) {

			HoverVoxel();

			_bMotionDelta = false; // must reset
		}
		
		// any operations that do not need to execute while paused should not
		if (!bPaused) {

			{ // static instance validation

				using static_unordered_map = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Static*>;
				for (static_unordered_map::const_iterator iter = _hshVoxelModelInstances_Static.cbegin();
					iter != _hshVoxelModelInstances_Static.cend(); ++iter) {

					if (iter->second) {
						iter->second->Validate();
					}
				}
				// parallel version any benefit? did crash....
				/*
				tbb::parallel_for_each(_hshVoxelModelInstances_Static.cbegin(), _hshVoxelModelInstances_Static.cend(), [&](std::pair<uint32_t const, Volumetric::voxelModelInstance_Static*> iter) {
					if (iter.second) {
						iter.second->Validate();
					}
				});
				*/
			}
			{ // dynamic instance validation

				using dynamic_unordered_map = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Dynamic*>;
				for (dynamic_unordered_map::const_iterator iter = _hshVoxelModelInstances_Dynamic.cbegin();
					iter != _hshVoxelModelInstances_Dynamic.cend(); ++iter) {

					if (iter->second) {
						iter->second->Validate();
					}
				}
				/* parallel version any benefit? did crash....
				tbb::parallel_for_each(_hshVoxelModelInstances_Dynamic.cbegin(), _hshVoxelModelInstances_Dynamic.cend(), [&](std::pair<uint32_t const, Volumetric::voxelModelInstance_Dynamic*> iter) {
					if (iter.second) {
						iter.second->Validate();
					}
				});
				*/
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
				auto it = cTestGameObject::begin();
				while (cTestGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}

			// ---------------------------------------------------------------------------//
			CleanUpInstanceQueue(); // *** must be done after all game object updates *** //
			// ---------------------------------------------------------------------------//

			// TESTING: //
			/*
			static tTime tLastTest(zero_time_point);
			static uint32_t copter_count(0);

			if (copter_count < 5 && tNow - tLastTest > milliseconds(500))
			{
				tLastTest = tNow;

				point2D_t const randomVoxelIndex = getRandomNonVisibleVoxelIndexNear();

				Iso::Voxel const* const pVoxel(world::getVoxelAt(randomVoxelIndex));
				if (pVoxel) {

					Iso::Voxel oVoxel(*pVoxel);

					if (!(Iso::isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel))) { // only if not road
						if (nullptr != placeCopterInstanceAt(randomVoxelIndex)) {
							++copter_count;
						}
					}

					//world::setVoxelAt(randomRoadEdgeVoxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}
				else {
					FMT_LOG_FAIL(GAME_LOG, "No voxel at ({:d},{:d}) !!", randomVoxelIndex.x, randomVoxelIndex.y);
				}

				
			}
			static tTime tLast(zero_time_point);

			if ((high_resolution_clock::now() - tLast) > nanoseconds(milliseconds(50))) {

				FMT_NUKLEAR_DEBUG(false, " copters( {:n} )", copter_count);
				tLast = high_resolution_clock::now();
			}
			*/

			// special instances //
			/*
			if (_activeExplosion) {

				if (!UpdateExplosion(tNow, _activeExplosion)) {
					SAFE_DELETE(_activeExplosion);
				}
			}
			else {

				_activeExplosion = new sExplosionInstance(XMVectorZero(), 50.0f);
			}

			if (_activeTornado) {

				if (!UpdateTornado(tNow, _activeTornado)) {
					SAFE_DELETE(_activeTornado);
				}
			}
			else {

				//_activeTornado = new sTornadoInstance(XMVectorZero(), 75.0f);
			}
			*/

			if (_activeRain) {

				if (!UpdateRain(tNow, _activeRain)) {  // ### todo - rain pours while paused if rain is not updated while paused
					SAFE_DELETE(_activeRain);
				}
			}

			if (_activeShockwave) {

				if (!UpdateShockwave(tNow, _activeShockwave)) {
					SAFE_DELETE(_activeShockwave);
				}
			}
			else // (nullptr == _activeShockwave) { // ** testing ** //
			{
				_activeShockwave = new sShockwaveInstance(XMVectorZero(), 100.0f);	// maximum radius is also limited by length of animation time, set radius as low as possible to get best perf while still covering the desired maxium area for the etire aimation
			}


			// City Update //
			MinCity::City->Update(tNow);

			// TESTING //
			{
				tTime tLast(zero_time_point);
				seconds interval(1);

				if (tNow - tLast > nanoseconds(interval)) {

					MinCity::City->modifyPopulationBy(PsuedoRandomNumber(-10000, 10000));
					MinCity::City->modifyCashBy(PsuedoRandomNumber32(-1000, 1000));

					interval = seconds(PsuedoRandomNumber(1, 15));
					tLast = tNow;
				}
			}

		} // end !Paused //

		{ // ### ONLOADED EVENT must be triggered here !! //
			[[unlikely]] if (bFirstUpdate) {
				OnLoaded(tNow);
			}
		}
	}

	void cVoxelWorld::SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants, uint32_t const stage)
	{
		_OpacityMap.SetSpecializationConstants_ComputeLight(constants, stage);
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
		// volume dimensions //
		constants.emplace_back(vku::SpecializationConstant(0, (float)Volumetric::voxelOpacity::getWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(1, (float)Volumetric::voxelOpacity::getHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(2, (float)Volumetric::voxelOpacity::getDepth())); // should be depth
	}
	void cVoxelWorld::SetSpecializationConstants_VolumetricLight_FS(std::vector<vku::SpecializationConstant>& __restrict constants) // all shader variables should be swizzled to xzy into fragment shader for texture lookup optimization. (ie varying vertex->fragnent shader variables)
	{
		point2D_t const frameBufferSz(MinCity::getFramebufferSize());
		point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

		constants.emplace_back(vku::SpecializationConstant(0, (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)(downResFrameBufferSz.y)));// // half-res frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)(downResFrameBufferSz.x)));// // half-res frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)(downResFrameBufferSz.y)));// // half-res frame buffer height

		// volume dimensions //
		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(5, (float)Volumetric::voxelOpacity::getDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(7, 1.0f / (float)Volumetric::voxelOpacity::getWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(9, 1.0f / (float)Volumetric::voxelOpacity::getHeight())); // should be height

		// light volume dimensions //
		constants.emplace_back(vku::SpecializationConstant(10, (float)Volumetric::voxelOpacity::getLightWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(11, (float)Volumetric::voxelOpacity::getLightDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(12, (float)Volumetric::voxelOpacity::getLightHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(13, 1.0f / (float)Volumetric::voxelOpacity::getLightWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(14, 1.0f / (float)Volumetric::voxelOpacity::getLightDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(15, 1.0f / (float)Volumetric::voxelOpacity::getLightHeight())); // should be height

		// For depth reconstruction from hardware depth buffer
		// https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
		constexpr double ZFar = Globals::MAXZ_DEPTH;
		constexpr double ZNear = Globals::MINZ_DEPTH;
		constants.emplace_back(vku::SpecializationConstant(16, (float)ZFar)); 
		constants.emplace_back(vku::SpecializationConstant(17, (float)ZNear)); 
	}

	void cVoxelWorld::SetSpecializationConstants_Nuklear(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		MinCity::Nuklear.SetSpecializationConstants(constants);
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
	
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getVolumeLength())); // // PS is *not* dependent on type of voxel for geometry size, should always be MINI_VOX_SIZE

		constants.emplace_back(vku::SpecializationConstant(5, (float)Volumetric::voxelOpacity::getLightWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getLightDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightHeight())); // should be height

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightWidth())); // should be inv width
		constants.emplace_back(vku::SpecializationConstant(9, 1.0f / (float)Volumetric::voxelOpacity::getLightDepth())); // should be inv depth
		constants.emplace_back(vku::SpecializationConstant(10, 1.0f / (float)Volumetric::voxelOpacity::getLightHeight())); // should be inv height;
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelRoad_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		point2D_t const frameBufferSize(MinCity::getFramebufferSize());

		constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
		constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
		constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getVolumeLength())); // // PS is *not* dependent on type of voxel for geometry size, should always be MINI_VOX_SIZE

		constants.emplace_back(vku::SpecializationConstant(5, (float)Volumetric::voxelOpacity::getLightWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getLightDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightHeight())); // should be height

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightWidth())); // should be inv width
		constants.emplace_back(vku::SpecializationConstant(9, 1.0f / (float)Volumetric::voxelOpacity::getLightDepth())); // should be inv depth
		constants.emplace_back(vku::SpecializationConstant(10, 1.0f / (float)Volumetric::voxelOpacity::getLightHeight())); // should be inv height
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_Basic_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, bool const bMiniVoxel)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)(bMiniVoxel ? (Iso::MINI_VOX_SIZE) : Iso::VOX_SIZE))); // VS is dependent on type of voxel for geometry size
		// used for uv -> voxel in vertex shader image store operation for opacity map
		constants.emplace_back(vku::SpecializationConstant(1, (float)Volumetric::voxelOpacity::getWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(2, (float)Volumetric::voxelOpacity::getHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(3, (float)Volumetric::voxelOpacity::getDepth())); // should be depth
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		SetSpecializationConstants_Voxel_Basic_VS_Common(constants, false);

		// used for uv -> voxel in vertex shader image store operation for opacity map

		constants.emplace_back(vku::SpecializationConstant(4, (float)1.0f / Iso::MAX_HEIGHT_STEP));
		constants.emplace_back(vku::SpecializationConstant(5, (float)Iso::HEIGHT_SCALE));
		constants.emplace_back(vku::SpecializationConstant(6, (int)MINIVOXEL_FACTOR));		
	}

	void cVoxelWorld::SetSpecializationConstants_Voxel_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, bool const bMiniVoxel)
	{
		constants.emplace_back(vku::SpecializationConstant(0, (float)(bMiniVoxel ? (Iso::MINI_VOX_SIZE) : Iso::VOX_SIZE))); // VS is dependent on type of voxel for geometry size
	}
	void cVoxelWorld::SetSpecializationConstants_VoxelTerrain_VS(std::vector<vku::SpecializationConstant>& __restrict constants) // ** also used for roads 
	{
		SetSpecializationConstants_Voxel_VS_Common(constants, false);

		// used for uv -> voxel in vertex shader image store operation for opacity map
		constants.emplace_back(vku::SpecializationConstant(1, (float)Volumetric::voxelOpacity::getHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(2, (float)1.0f / Iso::MAX_HEIGHT_STEP));
		constants.emplace_back(vku::SpecializationConstant(3, (float)Iso::HEIGHT_SCALE));
		constants.emplace_back(vku::SpecializationConstant(4, (int)MINIVOXEL_FACTOR));
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
		XMStoreFloat3A(&transformInv, Volumetric::_xmInverseVisibleXYZ);

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

		constants.emplace_back(vku::SpecializationConstant(4, (float)Volumetric::voxelOpacity::getVolumeLength())); // // PS is *not* dependent on type of voxel for geometry size, should always be MINI_VOX_SIZE

		constants.emplace_back(vku::SpecializationConstant(5, (float)Volumetric::voxelOpacity::getLightWidth())); // should be width
		constants.emplace_back(vku::SpecializationConstant(6, (float)Volumetric::voxelOpacity::getLightDepth())); // should be depth
		constants.emplace_back(vku::SpecializationConstant(7, (float)Volumetric::voxelOpacity::getLightHeight())); // should be height

		constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)Volumetric::voxelOpacity::getLightWidth())); // should be inv width
		constants.emplace_back(vku::SpecializationConstant(9, 1.0f / (float)Volumetric::voxelOpacity::getLightDepth())); // should be inv depth
		constants.emplace_back(vku::SpecializationConstant(10, 1.0f / (float)Volumetric::voxelOpacity::getLightHeight())); // should be inv height
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
	void cVoxelWorld::AddSpecializationConstants_Voxel_FS_Transparent(std::vector<vku::SpecializationConstant>& __restrict constants)
	{
		constants.emplace_back(vku::SpecializationConstant(11, (float)Volumetric::voxelOpacity::getHeight())); // should be height
		constants.emplace_back(vku::SpecializationConstant(12, 1.0f / (float)Volumetric::voxelOpacity::getHeight())); // should be inv height
	}

	void cVoxelWorld::UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, SAMPLER_SET_STANDARD_POINT, uint32_t const stage)
	{
		_OpacityMap.UpdateDescriptorSet_ComputeLight(dsu, samplerLinearClamp, samplerLinearRepeat, stage);
	}

	void cVoxelWorld::UpdateDescriptorSet_VolumetricLight(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_STANDARD_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, halfdepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);  // depth
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2D()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);  // blue noise
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

	void cVoxelWorld::UpdateDescriptorSet_VolumetricLightResolve(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_STANDARD_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(0U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2D()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfvolumetricImageView, vk::ImageLayout::eGeneral);
		dsu.beginImages(1U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfreflectionImageView, vk::ImageLayout::eGeneral);
	}
	void cVoxelWorld::UpdateDescriptorSet_VolumetricLightUpsample(vku::DescriptorSetUpdater& __restrict dsu,
		vk::ImageView const& __restrict fulldepthImageView, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView,
		SAMPLER_SET_STANDARD_POINT)
	{
		// Set initial sampler value
		dsu.beginImages(0U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, fulldepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointClamp, halfdepthImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2D()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfvolumetricImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, halfreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginBuffers(5U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer.buffer(), 0, _buffers.shared_buffer.maxsizebytes());
	}
	void cVoxelWorld::UpdateDescriptorSet_PostAA(vku::DescriptorSetUpdater& __restrict dsu,
		vk::ImageView const& __restrict colorImageView, vk::ImageView const& __restrict guiImageView0, vk::ImageView const& __restrict guiImageView1,
		SAMPLER_SET_STANDARD_POINT)
	{
		// 1 - colorview (backbuffer)
		dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		// 2 - bluenoise
		dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerPointRepeat, supernoise::blue.getTexture2D()->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);  // blue noise!	

		// continue wih rest of descriptor set ...
		MinCity::PostProcess.UpdateDescriptorSet_PostAA(dsu, guiImageView0, guiImageView1, samplerLinearClamp);
	}

	void cVoxelWorld::UpdateDescriptorSet_VoxelCommon(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict fullreflectionImageView, vk::ImageView const& __restrict lastColorImageView, SAMPLER_SET_STANDARD_POINT_ANISO)
	{
		dsu.beginBuffers(1U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer.buffer(), 0, _buffers.shared_buffer.maxsizebytes());

		dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->DistanceDirection->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(3U, 1, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, _OpacityMap.getVolumeSet().LightMap->Color->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
		dsu.beginImages(4U, 0, vk::DescriptorType::eInputAttachment);
		dsu.image(nullptr, fullreflectionImageView, vk::ImageLayout::eShaderReadOnlyOptimal);

		// finalize texture array and commit to descriptor set #######################################################################
		tbb::concurrent_vector<vku::TextureImage2DArray const*> const& rTextures( MinCity::TextureBoy.lockTextureArray() );

		dsu.beginImages(5U, TEX_NOISE, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_NOISE_SAMPLER, rTextures[TEX_NOISE]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_TERRAIN, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_TERRAIN_SAMPLER, rTextures[TEX_TERRAIN]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_ROAD, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_ROAD_SAMPLER, rTextures[TEX_ROAD]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		dsu.beginImages(5U, TEX_BLACKBODY, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(TEX_BLACKBODY_SAMPLER, rTextures[TEX_BLACKBODY]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		// #############################################################################################################################
		
		dsu.beginImages(6U, 0, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(samplerLinearClamp, lastColorImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
	}
	void cVoxelWorld::UpdateDescriptorSet_Voxel_ClearMask(vku::DescriptorSetUpdater& __restrict dsu)
	{
		// Set initial sampler value
		dsu.beginImages(1U, 0, vk::DescriptorType::eStorageImage);
		dsu.image(nullptr, _OpacityMap.getVolumeSet().OpacityMap->imageView(), vk::ImageLayout::eGeneral);	// used to clear opacity
		dsu.beginBuffers(2U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.subgroup_layer_count_max.buffer(), 0, _buffers.subgroup_layer_count_max.maxsizebytes());
		dsu.beginBuffers(3U, 0, vk::DescriptorType::eStorageBuffer);
		dsu.buffer(_buffers.shared_buffer.buffer(), 0, _buffers.shared_buffer.maxsizebytes());
	}
	
	// hides (unsets root/owner) so that instance of the specific model type
	bool const cVoxelWorld::hideVoxelModelInstanceAt(point2D_t const voxelIndex, int32_t const modelGroup, uint32_t const modelIndex, std::vector<Iso::voxelIndexHashPair, tbb::scalable_allocator<Iso::voxelIndexHashPair>>* const pRecordHidden)
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

	bool const cVoxelWorld::hideVoxelModelInstancesAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex, std::vector<Iso::voxelIndexHashPair, tbb::scalable_allocator<Iso::voxelIndexHashPair>>* const pRecordHidden)
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
		// cleanup special instances
		SAFE_DELETE(_activeExplosion);
		SAFE_DELETE(_activeTornado);
		SAFE_DELETE(_activeRain);
		SAFE_DELETE(_activeShockwave);
		

		// _currentVoxelIndexPixMap is released auto-magically - virtual memory

		// cleanup all game object vectors
		cTestGameObject::clear();

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

		for (uint32_t i = 0; i < 2; ++i) {
			voxels.visibleDynamic.opaque.stagingBuffer[i].release();
			voxels.visibleDynamic.trans.stagingBuffer[i].release();
			voxels.visibleStatic.stagingBuffer[i].release();
			voxels.visibleTerrain.stagingBuffer[i].release();
			voxels.visibleRoad.opaque.stagingBuffer[i].release();
			voxels.visibleRoad.trans.stagingBuffer[i].release();
		}

		SAFE_DELETE(_terrainTexture);
		SAFE_DELETE(_roadTexture);
		SAFE_DELETE(_blackbodyTexture);

		if (_blackbodyImage) {
			ImagingDelete(_blackbodyImage);
		}

		_buffers.reset_subgroup_layer_count_max.release();
		_buffers.subgroup_layer_count_max.release();
		_buffers.reset_shared_buffer.release();
		_buffers.shared_buffer.release();

		supernoise::blue.Release();

#ifdef DEBUG_STORAGE_BUFFER
		SAFE_RELEASE_DELETE(DebugStorageBuffer);
#endif
	}
} // end ns world