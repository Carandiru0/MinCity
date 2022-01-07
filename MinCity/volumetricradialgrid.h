/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#ifndef VOLUMETRICRADIALGRID_H
#define VOLUMETRICRADIALGRID_H

#include "cVoxelWorld.h"
#include "sBatched.h"
#include "voxLink.h"
#include <Utility/class_helper.h>
#include <vector>

//#define DEBUG_RANGE 

#pragma warning( disable : 4166 ) // __vectorcall ctor
#pragma warning( disable : 4141 ) // inline used more than once
#pragma warning( disable : 4166 ) // __vectorcall ctor

namespace Volumetric
{
	BETTER_ENUM(eRadialGridRenderOptions, uint32_t const,

		NO_OPTIONS = 0,
		FILL_COLUMNS_DOWN = (1 << 0),
		FILL_COLUMNS_UP = (1 << 1),
		FADE_COLUMNS = (1 << 2),
		CULL_CHECKERBOARD = (1 << 3),
		CULL_EXPANDING = (1 << 4),
		CULL_INNERCIRCLE_GROWING = (1 << 5),
		FOLLOW_GROUND_HEIGHT = (1 << 6),
		HOLLOW = (1 << 7),
		NO_TOPS = (1 << 8)
	);
	
	typedef struct sxRow
	{
																																																				// Length Variable optimized out
		__inline bool const operator<(sxRow const& rhs) const // is identity, always absolute(2 * x)
		{																																						  											// before: vecRows.push_back( xRow(-x, y, x * 2)); // after: vecRows.push_back( xRow(-x, y));
			return( Start.y > rhs.Start.y );		 
		}

		XMFLOAT2A	 	Start;
		float			RangeLeft, RangeRight;

		sxRow(float const x, float const y)
			: Start(x, y), RangeLeft(0.0f), RangeRight(0.0f)
		{}

	} xRow;
	
	typedef struct alignas(16) voxelShaderDesc_ : no_copy
	{
		float 				distance;
		v2_rotation_t 		rotation;

		union {  // **must be no larger than a single uint / 4bytes**

			struct { // currently using 26 bits of 32

				uint32_t
					lit : 1,			// if lit is true, emissive is "implied" true
					emissive : 1,		// however can be emissive but not actually lit, so if "emissive" is true, lit is not "implied" (for certain effects usage)
					column_sz : 8,
					rgb_color : 16;		// color - 5bits for r, 6bits for g, 5bits for b - *do not access directly!!* - use color functions provided for speed (getColor / setColor)
			};							// rgb_color is used to define color of a light, with all the blending between multiple lights - 16bit will blend up / upscale easily to 32bit by the light buffer manager class

			uint32_t		Data;
		};

		uint8_t				 alpha;

		__forceinline voxelShaderDesc_()		// *** default ctor is a voxel below ground level - will not be rendered
			: distance(1.0f),  // bugfix: do not bmi rotation(0.0f) this calls sincos, however by default rotation is already initialized
			Data(0), alpha(0)
		{}

		voxelShaderDesc_(voxelShaderDesc_&& rhs)
			: distance(std::move(rhs.distance)), rotation(std::move(rhs.rotation)),
			Data(std::move(rhs.Data)), alpha(std::move(rhs.alpha))
		{}
		voxelShaderDesc_& operator=(voxelShaderDesc_&& rhs)
		{
			distance = std::move(rhs.distance);
			rotation = std::move(rhs.rotation);
			Data = std::move(rhs.Data);
			alpha = std::move(rhs.alpha);

			return(*this);
		}

		__forceinline uvec4_v const __vectorcall getColor() const { // only rgb, use .transparency (float) for alpha

			// convert 565 to 888 (16bit to 32bit
			// R4 | R3 | R2 | R1 | R0 - G5 | G4 | G3 | G2 | G1 | G0 - B4 | B3 | B2 | B1 | B0
			//  1    1    1    1 |  1    0    0    0 |  0    0    0    0 |  0    0    0    0	=
			//  0    0    0    0 |  0    1    1    1 |  1    1    1    0 |  0    0    0    0
			//  0    0    0    0 |  0    0    0    0 |  0    0    0    1 |  1    1    1    1

			// unpack the 16bit color
			uint32_t const rgb(rgb_color);
			uvec4_t rgba;
			rgba.r = (rgb & 0xF800) >> 11; // only grabbing bits that belong to red channel in packed "Data"
			rgba.g = (rgb & 0x07E0) >> 5;  // only  ""       ""        """      green ""        ""      ""
			rgba.b = (rgb & 0x001F);		//  ""                               blue                    ""
			rgba.a = 0; // no alpha

			return(SFM::rgb565_to_888(uvec4_v(rgba))); // scale 16bit color to 32bit color
		}

		__inline uint32_t const getColorPacked() const { // only rgb, use .transparency (float) for alpha
			uvec4_t rgba;
			getColor().rgba(rgba);
			return(SFM::pack_rgba(rgba));
		}

		__inline void __vectorcall setColor( uvec4_v const color ) { // only rgb, use .transparency (float) for alpha
			
			// convert 888 to 565 (32bit to 16bit)

			uvec4_v const color565(SFM::rgb888_to_565(color)); // scales 32bit color to 16bit scale

			// pack the scaled result into 16bits
			uvec4_t rgba;
			color565.rgba(rgba); // alpha is ignored

			uint32_t rgb(0);
			// set color bits (safetly without modifying higher bits contained in data)
			rgb |= (0xF800 & (rgba.r << 11));
			rgb |= (0x07E0 & (rgba.g << 5));
			rgb |= (0x001F & rgba.b);

			rgb_color = rgb;
		}
	} voxelShaderDesc;

	typedef struct sRadialGridInstance
	{
		__inline bool const isInvalidated() const {
			return(Invalidated);
		}
		
		__inline fp_seconds const getLifetime() const {
			return(tLife);
		}
		__inline bool const  isDead() const {
			return(zero_time_duration == tLife);
		}
		
		__inline fp_seconds const& __restrict getLocalTime() const {
			return(tLocal);
		}
		
		__inline fp_seconds const& __restrict getLocalTimeDelta() const {
			return(tDelta);
		}

		__inline XMVECTOR const XM_CALLCONV getLocation() const { return(XMLoadFloat2A(&vLoc)); }

		__inline void XM_CALLCONV setLocation(FXMVECTOR const xmLoc) { XMStoreFloat2A(&vLoc, xmLoc); }

		__inline v2_rotation_t const& __vectorcall getRotation() const { return(vR); }

		__inline v2_rotation_t& __vectorcall getRotation() { return(vR); }

		__inline float const  getRadius() const {
			return(CurrentRadius);
		}

		__inline float const  getInvRadius() const {
			return(InvCurrentRadius);
		}
		
		__inline void setRadius(float const newRadius) {
			CurrentRadius = newRadius; InvCurrentRadius = 1.0f/newRadius; Invalidated = true;
		}
		
		__inline float const  getScale() const {
			return(Scale);
		}

		__inline float const  getInvScale() const {
			return(InvScale);
		}

		__inline void setScale(float const newScale) {
			Scale = newScale; InvScale = 1.0f / Scale;
		}

		__inline float const  getStepScale() const {
			return(StepScale);
		}

		__inline void setStepScale(float const newScale) {
			StepScale = newScale;
		}

		__inline void resetInvalidated() {
			Invalidated = false;
		}  
		
		__inline std::optional<tTime const> const UpdateLocalTime(tTime const tNow) // returns true (alive), false (dead) 
		{
			if ( zero_time_point != tLastUpdate && tNow > tLastUpdate ) {
				
				tDelta = fp_seconds(tNow - tLastUpdate);
				
				tLife -= tDelta;
				
				if (tLife <= zero_time_duration) {
					tLife = zero_time_duration;
					return(std::nullopt);
				}
				else {
					tLocal += tDelta;	// done this way to always be forward, no ping-ponging possible
				}
				
			}
			else
			{
				tDelta = zero_time_duration;
			}
			
			tTime const tReturn = tLastUpdate;
			tLastUpdate = tNow;
			
			return(tReturn);
		}
		
		std::optional<tTime const> const Update(tTime const tNow);	// returns tLastUpdate (alive), std::nullopt (dead)
		
		__forceinline __declspec(noalias) virtual bool const __vectorcall op(FXMVECTOR const, float const, Volumetric::voxelShaderDesc&& __restrict) const = 0;

		__vectorcall sRadialGridInstance( FXMVECTOR const xmWorldCoordOrigin, float const Radius, fp_seconds const tLife,
										  vector<Volumetric::xRow> const& __restrict Rows )
			: Scale(1.0f), InvScale(1.0f), StepScale(1.0f),  CurrentRadius(Radius), InvCurrentRadius(1.0f/Radius),
				tLife(tLife), tLastUpdate(zero_time_point),
				tLocal(zero_time_duration), tDelta(zero_time_duration), Invalidated(true),
				InstanceRows(Rows)
#ifdef DEBUG_RANGE
			, RangeMin(FLT_MAX), RangeMax(-FLT_MAX)
#endif
		{
			XMStoreFloat2A(&vLoc, xmWorldCoordOrigin);
		}
			
	public:
		vector<Volumetric::xRow> const& __restrict InstanceRows;
#ifdef DEBUG_RANGE
		float RangeMin, RangeMax;
#endif
	protected:
		XMFLOAT2A						vLoc;
		v2_rotation_t					vR;
		
	private:
		float							Scale, InvScale, StepScale;
		float							CurrentRadius,
										InvCurrentRadius;
		tTime							tLastUpdate;
		fp_seconds						tLocal, tDelta, tLife;
		bool							Invalidated;

	public:
		static constexpr float const DEFAULT_ROTATION = SFM::GOLDEN_RATIO_ZERO;	// bugfix - prevents perfectly aligned voxel to view resultinbg in potentially no visibility
																	// this is used on global rotation that the radial grid instance has
																	// NOT in voxelShaderDesc as seen above is init to default for v2_rotation_t
	} RadialGridInstance;
	
	STATIC_INLINE_PURE float const getRowLength(float const xDisplacement) { return(SFM::abs(xDisplacement * 2.0f)); }
	
	// Template inline function pointers!!!
	// _______________________________distance should be normalized in the range [0...-1] -- Inverted Y Axis -1 is UP -- easily convert [0...1] range by negating the return value
	//using voxel_op = bool const (*__vectorcall const)(FXMVECTOR const, float const, Volumetric::RadialGridInstance const* const __restrict, Volumetric::RadialGridInstance::voxelShaderDesc&& __restrict); // signature for all valid template params

	// public declarations only 
	NO_INLINE void radialgrid_generate(float const radius, vector<xRow>& __restrict vecRows);
	__forceinline STATIC_INLINE_PURE bool const __vectorcall isRadialGridNotVisible(FXMVECTOR const vOrigin, float const fRadius);

	template<uint32_t const Options = eRadialGridRenderOptions::NO_OPTIONS>
	STATIC_INLINE void renderRadialGrid(Volumetric::RadialGridInstance const* const __restrict radialGrid, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic); 

} // end namespace

using VoxelLocalBatch = sBatched<VertexDecl::VoxelDynamic, eStreamingBatchSize::RADIAL>;

// ######### private static inline definitions only

template<uint32_t const Options>
STATIC_INLINE bool const __vectorcall renderVoxel(FXMVECTOR const xmDisplacement, fp_seconds const tLocal, Volumetric::RadialGridInstance const* const __restrict radialGrid, 
												  tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{	
	static constexpr float const GROUND_ZERO_EPSILON = -0.0000001f;  // fix ug due to precision of floating point (voxels eing clipped distance < 0.0f)

	XMVECTOR const xmObjectGridSpace( XMVectorAdd(radialGrid->getLocation(), xmDisplacement) );
	
	Iso::Voxel const* pVoxelFound(nullptr);
	
	//if ( nullptr != (pVoxelFound = world::getVoxelAt_IfVisible(xmPlotGridSpace)) )
	if ( nullptr != (pVoxelFound = world::getVoxelAt(xmObjectGridSpace)) )
	{		
		// op always fed normalized coordinates in the [-1.0f...1.0f] range relative to the radius. 
		// normalized displacement = displacement / radius (both are already in multiples of MINI_VOX_STEP)
		Volumetric::voxelShaderDesc desc;
		XMVECTOR const xmNormDisplacement(XMVectorScale(xmDisplacement, radialGrid->getInvRadius()));
		bool const bVisible(radialGrid->op(xmNormDisplacement,
									       time_to_float(tLocal), std::forward<Volumetric::voxelShaderDesc&& __restrict>(desc)) );

		if (desc.lit) { // if lit - emissive is implied, if emissive lit is not implied
			desc.emissive = true;
		}
#ifdef DEBUG_RANGE
		const_cast<Volumetric::RadialGridInstance* const __restrict>(radialGrid)->RangeMin = __fminf(radialGrid->RangeMin, fHeightDistanceNormalized);
		const_cast<Volumetric::RadialGridInstance* const __restrict>(radialGrid)->RangeMax = __fmaxf(radialGrid->RangeMax, fHeightDistanceNormalized);
#endif
		// note that when distance is negative, were are inside the volumetric object //
		if (bVisible && (desc.distance - GROUND_ZERO_EPSILON) < 0.0f ) {	// distance used as height
				
			float fVoxelHeight = desc.distance * radialGrid->getScale();
			float fUniformDistance(-desc.distance);
				
			if constexpr ( Volumetric::eRadialGridRenderOptions::FOLLOW_GROUND_HEIGHT == (Volumetric::eRadialGridRenderOptions::FOLLOW_GROUND_HEIGHT & Options) ) // statically evaluated @ compile time
			{
				Iso::Voxel const oVoxelFound(*pVoxelFound); // Read

				float const groundHeight = Iso::getRealHeight(oVoxelFound);
				fVoxelHeight -= groundHeight;
				// adjust so Uniform Distance is still smooth 
				// h = d * s
				// d = h / s
				fUniformDistance += groundHeight * radialGrid->getInvScale();
			}
				
			// take 2D (x,z) (in XMFLOAT2A form so residing in x,y) coordinates and add height, swizzle to correct form of x,y,z result
			XMVECTOR xmVoxelOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorSetZ(xmObjectGridSpace, fVoxelHeight)));
			// *************** make relative to world origin (gridspace to worldspace transform) *****************************
			xmVoxelOrigin = XMVectorSubtract(xmVoxelOrigin, world::getOriginNoFractionalOffset()); // this is ultimately multiplied by view matrix which already has the fractional offset translation

			// UV's swizzled to x,z,y form
			// a more accurate index, based on position which has fractional component, vs old usage of arrayIndex (these voxels are not affected by gridoffset) - they may need to be***
			XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmVoxelOrigin, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));
			//xmIndex = XMVectorSetY(xmIndex, -XMVectorGetY(xmIndex) + (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y * 0.5f);
			// all w care about is the visible relative UV (to "grid")

			[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
				&& XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
			{
				// xyz = visible relative UV,  w = notusing detailed occlusion - set to something else ?
				XMVECTOR xmUVs(XMVectorMultiplyAdd(
					xmIndex,
					Volumetric::_xmInverseVisibleXYZ,
					XMVectorSet(0.0f, 0.0f, 0.0f, fUniformDistance) // Uniform/voxel float parameter (instead of usual COLOR used by voxels in models
				));


				// Build hash //
				uint32_t hash(0);

				// unsupported hash |= voxel.getAdjacency();			 //           0000 0000 0001 1111
				// unsupported hash |= (voxel.getOcclusion() << 5);		 //           0000 1111 111x xxxx
					
				hash |= (uint32_t(desc.emissive) << 12);				 // 0000 0000 0001 xxxx xxxx xxxx
				hash |= (uint32_t(desc.alpha >> 6) << 13);				 // 0000 0000 011x xxxx xxxx xxxx

				v2_rotation_t const vR(desc.rotation.isZero() ?
					radialGrid->getRotation() :
					(radialGrid->getRotation() + desc.rotation));
				// pitch is not supported but input is compliant here so shader behaves properly
				// voxelradialgrid support only azimuth
				XMVECTOR const xmR(XMVectorAdd(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMVectorRotateRight<2>(vR.v2()))); // *bugfix pitch is xy, azimuth is zw

				// begin render radial grid voxel (add to vertex buffer )
				// critical to increment voxelDynamicRow whn finished, propogates 
				// all the way back to this parallel invocations' pointer to th isolated/blocked range of rows w ar working on
				// on only a successfully visible etc voxel add to vertex buffer (staging)			
				constexpr bool const
					bFillDown((Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_DOWN == (Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_DOWN & Options)) & (Volumetric::eRadialGridRenderOptions::HOLLOW != (Volumetric::eRadialGridRenderOptions::HOLLOW & Options))),
					bFillUp((Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_UP == (Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_UP & Options)) & (Volumetric::eRadialGridRenderOptions::HOLLOW != (Volumetric::eRadialGridRenderOptions::HOLLOW & Options)));

				if constexpr (bFillUp || (Volumetric::eRadialGridRenderOptions::NO_TOPS == (Volumetric::eRadialGridRenderOptions::NO_TOPS & Options))) {
					hash |= Volumetric::voxB::BIT_ADJ_ABOVE; // all adjacent until top most
				}

				// always render first voxel
				localVoxels.emplace_back(
					pVoxelsOut, 
					
						xmVoxelOrigin,
						xmUVs,
						xmR,
						hash 
					);

				if (desc.lit) {
					Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, desc.getColorPacked());
				}

				if constexpr (bFillDown || (Volumetric::eRadialGridRenderOptions::NO_TOPS == (Volumetric::eRadialGridRenderOptions::NO_TOPS & Options))) {
					hash |= Volumetric::voxB::BIT_ADJ_ABOVE; // all adjacent after top most
				}

				if constexpr (bFillDown || bFillUp)
				{			  						
					// filling column
					constexpr float const FILL_DIRECTION = bFillDown ? 1.0f : -1.0f;
					float const fStepVoxel(radialGrid->getStepScale() * Iso::MINI_VOX_STEP * FILL_DIRECTION);

					uint32_t column_voxel_size(desc.column_sz);

					if (0 == column_voxel_size) {
						column_voxel_size = Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y >> 1; // limit
					}

					// height = step * count
					column_voxel_size = SFM::min(column_voxel_size, SFM::floor_to_u32(SFM::abs(fVoxelHeight / fStepVoxel)));

					if (0 != column_voxel_size) {

						constexpr float const STEP_UV = Volumetric::INVERSE_MINIGRID_VISIBLE_Y * FILL_DIRECTION;
						float const fInvMaxDistance((Iso::MINI_VOX_STEP / radialGrid->getScale()) * FILL_DIRECTION);

						float fUVHeight(XMVectorGetY(xmUVs));

						for (int32_t i = column_voxel_size - 1; i >= 0 ; --i) {

							fVoxelHeight += fStepVoxel;
							xmVoxelOrigin = XMVectorSetY(xmVoxelOrigin, fVoxelHeight);

							fUVHeight -= STEP_UV;
							xmUVs = XMVectorSetY(xmUVs, fUVHeight);

							fUniformDistance -= fInvMaxDistance;
							xmUVs = XMVectorSetW(xmUVs, fUniformDistance);

							// fading
							if constexpr (Volumetric::eRadialGridRenderOptions::FADE_COLUMNS == (Volumetric::eRadialGridRenderOptions::FADE_COLUMNS & Options))
							{
								static constexpr uint32_t const SHIFT_TRANSPARENCY = 13U,
																MASK_TRANSPARENCY = 0x6000U;
								
								int32_t current = int32_t((hash & MASK_TRANSPARENCY) >> SHIFT_TRANSPARENCY);	// get bits
								hash &= ~MASK_TRANSPARENCY; // clear bits
								current = SFM::max(0, --current); // modify value (simply goes to next lower level of transparency, or the minimum transparency value)
								hash |= (uint32_t(current) << SHIFT_TRANSPARENCY); // set bits
							}

							if constexpr (bFillUp && (Volumetric::eRadialGridRenderOptions::NO_TOPS != (Volumetric::eRadialGridRenderOptions::NO_TOPS & Options))) {

								if (0 == i) {
									hash &= (~Volumetric::voxB::BIT_ADJ_ABOVE); // top most needs top on filling up
								}
							}

							localVoxels.emplace_back(
								pVoxelsOut,
								
									xmVoxelOrigin,
									xmUVs,
									xmR,
									hash
								);

							if (desc.lit) {
								Volumetric::VolumetricLink->Opacity.getMappedVoxelLights().seed(XMVectorMultiplyAdd(xmVoxelOrigin, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias), desc.getColorPacked());
							}
						}
					}
				}
				
				return(true);
			}
		}
	}
	return(false);
}

// not working properly, deprecated //
template<uint32_t const Options>
__forceinline STATIC_INLINE void renderVoxelRow_CullInnerCircleGrowing(
	Volumetric::xRow const* const __restrict pCurRow, fp_seconds const tLocal,
	Volumetric::RadialGridInstance const* const __restrict radialGrid,
	tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{
	XMFLOAT2A vDisplacement(pCurRow->Start);
	float fCurRangeLeft(pCurRow->RangeLeft), fCurRangeRight(pCurRow->RangeRight);
		
	float RunWidth = Volumetric::getRowLength(vDisplacement.x);
		
	do
	{
			if (vDisplacement.x <= fCurRangeLeft || vDisplacement.x >= fCurRangeRight) { // Growing
				if ( renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels) ) {
					
					if ( vDisplacement.x < 0.0f ) { 
						if ( 0.0f == fCurRangeLeft )
							fCurRangeLeft = vDisplacement.x;
						else
							fCurRangeLeft = SFM::max(fCurRangeLeft, vDisplacement.x);
					}
					else {
						if ( 0.0f == fCurRangeRight )
							fCurRangeRight = vDisplacement.x;
						else
							fCurRangeRight = SFM::min(fCurRangeRight, vDisplacement.x);
					}
				}
				
				vDisplacement.x += Iso::MINI_VOX_STEP;
				RunWidth -= Iso::MINI_VOX_STEP;
			}
			else { // Inside the "inner circle" culling zone
				// Move Displacement to the right side
				RunWidth -= (fCurRangeRight - vDisplacement.x);
				vDisplacement.x = fCurRangeRight;
			}

	} while ( RunWidth >= 0.0f );
	
	// Finish with storing updated range for this row
	const_cast<Volumetric::xRow* const __restrict>(pCurRow)->RangeLeft = fCurRangeLeft;
	const_cast<Volumetric::xRow* const __restrict>(pCurRow)->RangeRight = fCurRangeRight;
}

// not working properly, deprecated //
template<uint32_t const Options>
__forceinline STATIC_INLINE void renderVoxelRow_CullExpanding(
	Volumetric::xRow const* const __restrict pCurRow, fp_seconds const tLocal,
	Volumetric::RadialGridInstance const* const __restrict radialGrid,
	tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{
	XMFLOAT2A vDisplacement(pCurRow->Start);

	float const HalfWidth = Volumetric::getRowLength(vDisplacement.x) * 0.5f;
	float const SymmetricStart = vDisplacement.x + HalfWidth;
	
	{
		float RunHalfWidth(HalfWidth);
		vDisplacement.x = SymmetricStart;
		// Render LeftSide until renderVoxel returns false starting from middle
		do
		{
			if ( !renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels) )
				break;	// only rendering expanding volume, stop rendering side once a zero height/undefined voxel is hit
			
			vDisplacement.x -= Iso::MINI_VOX_STEP;	// Middle to left
			RunHalfWidth -= Iso::MINI_VOX_STEP;
		} while (RunHalfWidth >= 0.0f);
	}
	
	{
		float RunHalfWidth(HalfWidth);
		vDisplacement.x = SymmetricStart + Iso::MINI_VOX_STEP;	// start right side offset by one tiny voxel to not render same voxel twice
		// Render RightSide until renderVoxel returns false starting from middle
		do
		{
			if ( !renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels) )
				break;	// only rendering expanding volume, stop rendering side once a zero height/undefined voxel is hit
			
			vDisplacement.x += Iso::MINI_VOX_STEP;	// Middle to right
			RunHalfWidth -= Iso::MINI_VOX_STEP;
		} while (RunHalfWidth >= 0.0f);
	}
}

template<uint32_t const Options>
__forceinline STATIC_INLINE void renderVoxelRow_CheckerEven(
	Volumetric::xRow const* const __restrict pCurRow, fp_seconds const tLocal,
	Volumetric::RadialGridInstance const* const __restrict radialGrid,
	tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{
	XMFLOAT2A vDisplacement(pCurRow->Start);

	float RunWidth = Volumetric::getRowLength(vDisplacement.x);
	uint32_t column(0);

	do {

		if constexpr (Volumetric::eRadialGridRenderOptions::HOLLOW == (Volumetric::eRadialGridRenderOptions::HOLLOW & Options)) { // only the "top"

			if (0 != column && ((RunWidth - Iso::MINI_VOX_STEP) >= 0.0f)) { // inside column
				renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
			else { // edge column
				renderVoxel<(Options & (~Volumetric::eRadialGridRenderOptions::HOLLOW))>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
		}
		else {
			renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
		}

		++column;
		vDisplacement.x += Iso::MINI_VOX_STEP * 2.0f;
		RunWidth -= Iso::MINI_VOX_STEP * 2.0f;
		
	} while (RunWidth >= 0.0f);
}

template<uint32_t const Options>
__forceinline STATIC_INLINE void renderVoxelRow_CheckerOdd(
	Volumetric::xRow const* const __restrict pCurRow, fp_seconds const tLocal,
	Volumetric::RadialGridInstance const* const __restrict radialGrid,
	tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{
	XMFLOAT2A vDisplacement(pCurRow->Start);
	vDisplacement.x += Iso::MINI_VOX_STEP;	// odd (checkerboard) offset

	float RunWidth = Volumetric::getRowLength(vDisplacement.x);
	uint32_t column(0);

	do {

		if constexpr (Volumetric::eRadialGridRenderOptions::HOLLOW == (Volumetric::eRadialGridRenderOptions::HOLLOW & Options)) { // only the "top"

			if (0 != column && ((RunWidth - Iso::MINI_VOX_STEP) >= 0.0f)) { // inside column
				renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
			else { // edge column
				renderVoxel<(Options & (~Volumetric::eRadialGridRenderOptions::HOLLOW))>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
		}
		else {
			renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
		}

		++column;
		vDisplacement.x += Iso::MINI_VOX_STEP * 2.0f;
		RunWidth -= Iso::MINI_VOX_STEP * 2.0f;

	} while (RunWidth >= 0.0f);
}

template<uint32_t const Options>
__forceinline STATIC_INLINE void renderVoxelRow_Brute( 
	Volumetric::xRow const* const __restrict pCurRow, fp_seconds const tLocal,
	Volumetric::RadialGridInstance const* const __restrict radialGrid, 
	tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut, VoxelLocalBatch& __restrict localVoxels)
{
	XMFLOAT2A vDisplacement(pCurRow->Start);

	float RunWidth = Volumetric::getRowLength(vDisplacement.x);
	uint32_t column(0);

	do {
		
		if constexpr (Volumetric::eRadialGridRenderOptions::HOLLOW == (Volumetric::eRadialGridRenderOptions::HOLLOW & Options)) { // only the "top"

			if (0 != column && ((RunWidth - Iso::MINI_VOX_STEP) >= 0.0f)) { // inside column
				renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
			else { // edge column
				renderVoxel<(Options & (~Volumetric::eRadialGridRenderOptions::HOLLOW))>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
			}
		}
		else {
			renderVoxel<Options>(XMLoadFloat2A(&vDisplacement), tLocal, radialGrid, pVoxelsOut, localVoxels);
		}

		++column;
		vDisplacement.x += Iso::MINI_VOX_STEP;
		RunWidth -= Iso::MINI_VOX_STEP;

	} while (RunWidth >= 0.0f);
}
	


namespace Volumetric
{
	// public static inline definitions only
	
	__forceinline STATIC_INLINE_PURE bool const __vectorcall isRadialGridNotVisible( FXMVECTOR const vOrigin, float const fRadius )
	{
		/*
		vec2_t const vExtent = world::v2_GridToScreen( v2_adds(vOrigin, fRadius) );
		vOrigin = world::v2_GridToScreen( vOrigin );
		
		float const DistanceSquared = v2_distanceSquared( v2_sub(vExtent, vOrigin) );
			
		// approximation, floored location while radius is ceiled, overcompensated, to use interger based visibility test
		return( OLED::TestStrict_Not_OnScreen( v2_to_p2D(vOrigin), int32::__ceilf(DistanceSquared) ) );
		*/
		return(false); // todo proper
	}

	namespace local
	{
		extern __declspec(selectany) inline thread_local constinit VoxelLocalBatch voxels{};
	} // end ns

	template<uint32_t const Options>
	STATIC_INLINE void renderRadialGrid( Volumetric::RadialGridInstance const* const __restrict radialGrid, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict pVoxelsOut)
	{
		fp_seconds const tLocal(radialGrid->getLocalTime());
		
		// Fastest way to iterate over a vector! (in parallel as isolated random access)
		vector<Volumetric::xRow> const& __restrict Rows(radialGrid->InstanceRows);

		tbb::parallel_for(
			tbb::blocked_range<xRow const* __restrict>(Rows.data(), Rows.data() + Rows.size(), eThreadBatchGrainSize::RADIAL),
			[&](tbb::blocked_range<xRow const* __restrict> rows) {

				xRow const* __restrict pCurRow(rows.begin());
				xRow const* __restrict pLastRow(rows.end());
				for (; pCurRow < pLastRow; ++pCurRow) {

					// statically evaluated @ compile time
					if constexpr (eRadialGridRenderOptions::CULL_EXPANDING == (eRadialGridRenderOptions::CULL_EXPANDING & Options)) {								// expanding volumes ie explosion
						renderVoxelRow_CullExpanding<Options>(pCurRow, tLocal, radialGrid, pVoxelsOut, local::voxels);
					}
					else if constexpr (eRadialGridRenderOptions::CULL_INNERCIRCLE_GROWING == (eRadialGridRenderOptions::CULL_INNERCIRCLE_GROWING & Options)) { // growing volume with inside skippped as it grows ie shockwave
						renderVoxelRow_CullInnerCircleGrowing<Options>(pCurRow, tLocal, radialGrid, pVoxelsOut, local::voxels);
					}
					else if constexpr (eRadialGridRenderOptions::CULL_CHECKERBOARD == (eRadialGridRenderOptions::CULL_CHECKERBOARD & Options)) {

						if (0 == ((pCurRow - rows.begin()) & 1)) {
							renderVoxelRow_CheckerEven<Options>(pCurRow, tLocal, radialGrid, pVoxelsOut, local::voxels);
						}
						else {
							renderVoxelRow_CheckerOdd<Options>(pCurRow, tLocal, radialGrid, pVoxelsOut, local::voxels);
						}
					}
					else {
						renderVoxelRow_Brute<Options>(pCurRow, tLocal, radialGrid, pVoxelsOut, local::voxels);
					}

				}

				// ####################################################################################################################
				// ensure all batches are  output (residual/remainder)
				local::voxels.out(pVoxelsOut);
				// ####################################################################################################################
			});
	}
} // end namespace

#endif


