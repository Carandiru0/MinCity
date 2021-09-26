/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#pragma once
#ifndef VOXEL_MODEL_H
#define VOXEL_MODEL_H

#include "globals.h"
#include <Math/superfastmath.h>
#include <Math/v2_rotation_t.h>
#include "IsoVoxel.h"
#include "Declarations.h"
#include "eStateGroups.h"
#include "voxelState.h"
#include "voxelScreen.h"
#include "voxLink.h"
#include "adjacency.h"

#include "sBatched.h"

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#include "performance.h"
#endif

// forward declarations //
namespace Volumetric
{
	template< bool const Dynamic >
	class voxelModelInstance;	// forward declaration
	class voxelModelInstance_Dynamic;
	class voxelModelInstance_Static;

} // end ns

// definitions //
namespace Volumetric
{

namespace voxB
{
	typedef struct voxCoord	
	{
		uint8_t		x,
					y,
					z;
		
		inline void set(uint8_t const xx, uint8_t const yy, uint8_t const zz)
		{
			x = xx;
			y = yy;
			z = zz;
		}
		inline voxCoord(uint8_t const xx, uint8_t const yy, uint8_t const zz)
			: x(xx), y(yy), z(zz)
		{}
			
	} voxCoord;
		
	typedef struct voxelNormal
	{
		union
		{
			struct
			{
				// bits 0, 1, 2   3,  4,  5
				//	    Nx Ny Nz  SNx SNy SNz
				uint8_t 
					Nx : 1, Ny : 1, Nz : 1,			// voxel normal (absolute part) bits 1,2,3
					SNx : 1, SNy : 1, SNz : 1,		// voxel normal (sign) bits 4,5,6
					Padding : 2;
			};

			uint8_t const encoded;
		};

		inline uint32_t const bits() const {
			return(encoded);
		}

		// bits 0, 1, 2   3,  4,  5
		//	    Nx Ny Nz  SNx SNy SNz
		// bit 0: BIT_ADJ_ABOVE
		// bit 1: BIT_ADJ_BACK
		// bit 2: BIT_ADJ_FRONT
		// bit 3: BIT_ADJ_RIGHT
		// bit 4: BIT_ADJ_LEFT
		inline voxelNormal(uint32_t const Adjacency)				// absolute part, 0 if both are present (cancel out), otherwise if either (XOR) are present 1
			: Nx((0 != (BIT_ADJ_LEFT & Adjacency)) ^ (0 != (BIT_ADJ_RIGHT & Adjacency))),
			  Ny(0 != (BIT_ADJ_ABOVE & Adjacency) /* no need */),
			  Nz((0 != (BIT_ADJ_FRONT & Adjacency)) ^ (0 != (BIT_ADJ_BACK & Adjacency))),
																	// (sign part, point in direction opposite of adjacency
			  SNx(0 != (BIT_ADJ_LEFT & Adjacency)),	// left occupied, point right (+1) , else point left (-1)	(dependent on absolute part Nx == 1, if Nx == 0 this is a don't care)				
			  SNy(0),
			  SNz(0 != (BIT_ADJ_FRONT & Adjacency)),	// front occupied, point back (+1) , else point front (-1)	(dependent on absolute part Nz == 1, if Nz == 0 this is a don't care)
			  Padding{ 0 }

		{
			// default to only state left if all zero, point up
			if (0 == (Nx|Ny|Nz)) {
				Ny = 1; SNy = 1;

			}
		}

		inline voxelNormal(int32_t const x, int32_t const y, int32_t const z)				// absolute part, 0 if both are present (cancel out), otherwise if either (XOR) are present 1
			: Nx(SFM::abs(x)),
			Ny(SFM::abs(y)),
			Nz(SFM::abs(z)),
			// (sign part, point in direction opposite of adjacency
			SNx(x < 0 ? 0 : 1),	// left occupied, point right (+1) , else point left (-1)	(dependent on absolute part Nx == 1, if Nx == 0 this is a don't care)				
			SNy(y < 0 ? 0 : 1), /* Y sign will always be negative, no pointing up restriction */
			SNz(z < 0 ? 0 : 1),	// front occupied, point back (+1) , else point front (-1)	(dependent on absolute part Nz == 1, if Nz == 0 this is a don't care)
			Padding{ 0 }

		{
		}
		STATIC_INLINE_PURE voxelNormal const toNormal(int32_t const x, int32_t const y, int32_t const z) { return(voxelNormal(x, y, z)); }

	} voxelNormal;

	typedef struct voxelDescPacked	// Final 256x256x256 Structure
	{
		union
		{
			struct 
			{
				uint8_t // (largest type uses 8 bits)
					x : 8,							// bits 1 : 24, Position (0 - 255) 256 Values per Component
					y : 8,
					z : 8,
					Left : 1,						// bits 25 : 29, Adjacency (0 / 1) Binary boolean
					Right : 1,
					Front : 1,
					Back : 1,
					Above : 1,
					Occlusion_Corner : 1,			// bits 30 : 32, Occlusion (0/1) binary
					Occlusion_Left : 1,
					Occlusion_Right : 1;
	
			};
			
			uint32_t Data; // union "mask" of above
		};
		
		union
		{
			struct
			{
				uint32_t
					Color : 24,		  // RGB 16.78 million voxel colors
					Alpha : 8;		  
			};

			uint32_t RGBA;
		};

		uint8_t	OcclusionCount; // 8 neighbours checked 0 - 8 inclusive


		inline __m128i const XM_CALLCONV  getPosition() const { return(_mm_setr_epi32(x, y, z, 0)); }
		
		inline uint32_t const 			  getColor() const { return(Color); }
		inline uint32_t const 			  getAlpha() const { return(Alpha); }
		inline uint32_t const			  getAdjacency() const { return((Left << 4U) | (Right << 3U) | (Front << 2U) | (Back << 1U) | (Above)); }
		inline void						  addAdjacency(uint32_t const Adj) { 
			Left	|= (0 != (BIT_ADJ_LEFT & Adj));
			Right	|= (0 != (BIT_ADJ_RIGHT & Adj));
			Front	|= (0 != (BIT_ADJ_FRONT & Adj));
			Back	|= (0 != (BIT_ADJ_BACK & Adj));
			Above	|= (0 != (BIT_ADJ_ABOVE & Adj));
		}

		inline uint32_t const			  getOcclusion() const { return( (((uint32_t)OcclusionCount) << 3U) | (Occlusion_Right << 2U) | (Occlusion_Left << 1U) | (Occlusion_Corner) ); }

		inline voxelNormal const		  getNormal() const { return(voxelNormal(getAdjacency())); }

		__inline __declspec(noalias) bool const operator<(voxelDescPacked const& rhs) const
		{		
			return(y < rhs.y);  // no point in sorting by depth(.z) - ie.) if camera rotates around model
		}						// this provides some z depth overdraw compensation however with topmost voxels occluding voxels below
								// **note: model is loaded upside down, so top is actually 0 and bottom is max height
		__inline __declspec(noalias) bool const operator!=(voxelDescPacked const& rhs) const
		{	
			return(Data != rhs.Data);
		}
		inline voxelDescPacked(voxCoord const Coord, uint8_t const Adj, uint8_t const Occlusion, uint8_t const OcclusionCount_, uint32_t const inColor)
			: x(Coord.x), y(Coord.y), z(Coord.z),
				Left(0 != (BIT_ADJ_LEFT & Adj)), Right(0 != (BIT_ADJ_RIGHT & Adj)), Front(0 != (BIT_ADJ_FRONT & Adj)),
			    Back(0 != (BIT_ADJ_BACK & Adj)), Above(0 != (BIT_ADJ_ABOVE & Adj)),
			    Occlusion_Corner(Iso::OCCLUSION_SHADING_CORNER == (Iso::OCCLUSION_SHADING_CORNER & Occlusion)),
			    Occlusion_Left(Iso::OCCLUSION_SHADING_SIDE_LEFT == (Iso::OCCLUSION_SHADING_SIDE_LEFT & Occlusion)),
			    Occlusion_Right(Iso::OCCLUSION_SHADING_SIDE_RIGHT == (Iso::OCCLUSION_SHADING_SIDE_RIGHT & Occlusion)),
				OcclusionCount(OcclusionCount_),
				Color(inColor), Alpha(0)
		{}
		voxelDescPacked() = default;

		voxelDescPacked(voxelDescPacked const& rhs) noexcept
			: Data(rhs.Data), RGBA(rhs.RGBA), OcclusionCount(rhs.OcclusionCount)
		{}
		voxelDescPacked(voxelDescPacked&& rhs) noexcept
			: Data(rhs.Data), RGBA(rhs.RGBA), OcclusionCount(rhs.OcclusionCount)
		{}
		voxelDescPacked const& operator=(voxelDescPacked const& rhs) noexcept
		{
			Data = rhs.Data;
			RGBA = rhs.RGBA;
			OcclusionCount = rhs.OcclusionCount;

			return(*this);
		}
		voxelDescPacked const& operator=(voxelDescPacked&& rhs) noexcept
		{
			Data = rhs.Data;
			RGBA = rhs.RGBA;
			OcclusionCount = rhs.OcclusionCount;
			 
			return(*this);
		}
		
	} voxelDescPacked;
	
	typedef struct voxelModelFeatures
	{
		voxelScreen const* videoscreen = nullptr;

		~voxelModelFeatures() {
			SAFE_DELETE(videoscreen);
		}

	} voxelModelFeatures;

	template< bool const Dynamic >
	struct voxelModelIdent
	{
		int32_t	const	_modelGroup;
		uint32_t const	_index;
	};
	
	typedef struct voxelModelBase
	{		
		voxelDescPacked const* __restrict   _Voxels;		// Finalized linear array of voxels (constant readonly memory)
		
		uvec4_t 		_maxDimensions;					// actual used size of voxel model
		XMFLOAT3A 		_maxDimensionsInv;
		XMFLOAT3A		_Extents;						// xz = localarea bounds, y = maxdimensions.y (Extents are 0.5f * (width/height/depth) as in origin at very center of model on all 3 axis)
		rect2D_t		_LocalArea;						// Rect defining local area in grid units (converted from minivoxels to voxels)
		
		voxelModelFeatures _Features;

		uint32_t 		_numVoxels;						// # of voxels activated
		uint32_t		_numVoxelsEmissive;
		uint32_t		_numVoxelsTransparent;

		bool const	    _isDynamic_;

		voxelState* __restrict _State;

		inline voxelModelBase(bool const isDynamic) 
			: _numVoxels(0), _numVoxelsEmissive(0), _numVoxelsTransparent(0), _Extents{}, _isDynamic_(isDynamic), _Voxels(nullptr), _State(nullptr)
		{}

		void ComputeLocalAreaAndExtents();

		void createState() {			// State is option and only created on models that request it
			if (nullptr == _State) {
				_State = (voxelState* __restrict)scalable_aligned_malloc(_numVoxels * sizeof(voxelState), 16);
				memset(&_State[0], 0, _numVoxels * sizeof(voxelState)); // cant use __memclr_aligned_16, bugfix.
			}
		}

		~voxelModelBase(); // defined at end of voxBinary.cpp

	} voxelModelBase; // voxelModelBase

	template< bool const Dynamic >
	class voxelModel : public voxelModelBase
	{
	public:
		voxelModelIdent<Dynamic> const& identity() const { return(_identity);}
	private:
		voxelModelIdent<Dynamic> const _identity;

	public:
		constexpr bool const isDynamic() const { return(Dynamic); }
		
		inline voxelModel(voxelModelIdent<Dynamic>&& identity)
		: voxelModelBase(Dynamic), _identity(std::forward<voxelModelIdent<Dynamic>&&>(identity))
		{}

		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex, Iso::Voxel const& __restrict oVoxel, voxelModelInstance<Dynamic> const& instance, 
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const;
	};
		
	read_only inline XMVECTORF32 const _xmORIGINMOVE{ 0.5f, 1.0f, 0.5f, 0.5f }; // **** note Y is not centered on origin of model, instead its at the bottom of model
	STATIC_INLINE_PURE XMVECTOR XM_CALLCONV getMiniVoxelGridIndex(FXMVECTOR maxDimensions, FXMVECTOR maxDimensionsInv,
		FXMVECTOR xmVoxelRelativeModelPosition)
	{
		
		// Use relative coordinates in signed fashion so origin is forced to middle of model

		// normalize relative coordinates (mul)  -  change from [0.0f ... 1.0f] tp [-1.0f ... 1.0f]
		XMVECTOR xmPlotGridSpace = SFM::__fms(xmVoxelRelativeModelPosition, maxDimensionsInv, _xmORIGINMOVE);

		// now scale by dimension size (this is halved by now scaling above by two - already * 2.0f dont worry)
		xmPlotGridSpace = XMVectorMultiply(xmPlotGridSpace, maxDimensions);

		return(xmPlotGridSpace);
	}

	namespace local
	{
		using VoxelLocalBatchNormal = sBatched<VertexDecl::VoxelNormal, eStreamingBatchSize::MODEL>;
		using VoxelLocalBatchDynamic = sBatched<VertexDecl::VoxelDynamic, eStreamingBatchSize::MODEL>;

		extern __declspec(selectany) inline thread_local constinit VoxelLocalBatchNormal voxels_static{};
		extern __declspec(selectany) inline thread_local constinit VoxelLocalBatchDynamic voxels_dynamic{};
		extern __declspec(selectany) inline thread_local constinit VoxelLocalBatchDynamic voxels_trans{};
	} // end ns

	template<bool const Dynamic>
	__inline void XM_CALLCONV voxelModel<Dynamic>::Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		Iso::Voxel const& __restrict oVoxel,
		voxelModelInstance<Dynamic> const& instance,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const
	{
		typedef struct __declspec(novtable) sRenderFuncBlockChunk {

		private:
			XMVECTOR const						xmVoxelOrigin, xmUV;
			voxB::voxelDescPacked const* const __restrict voxelsIn;
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static;
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic;
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans;
			Iso::Voxel const& __restrict		rootVoxel;
			voxelModelInstance<Dynamic> const& instance;
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			PerformanceType& PerformanceCounters;
#endif		

			sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
		public:
			__forceinline explicit sRenderFuncBlockChunk(FXMVECTOR xmVoxelOrigin_, FXMVECTOR xmUV_,
				voxB::voxelDescPacked const* const __restrict voxelsIn_,
				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static_,
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic_,
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans_,
				Iso::Voxel const& __restrict rootVoxel_,
				voxelModelInstance<Dynamic> const& __restrict instance_
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceType& PerformanceCounters_
#endif
			) // add in ground height from root voxel passed in, total area encompassing the model has been averaged / flat //
				:
				xmVoxelOrigin(XMVectorSubtract(xmVoxelOrigin_, XMVectorSet(0.0f, instance_.getElevation(), 0.0f, 0.0f))),
				xmUV(xmUV_),
				voxelsIn(voxelsIn_), voxels_static(voxels_static_), voxels_dynamic(voxels_dynamic_), voxels_trans(voxels_trans_),
				rootVoxel(rootVoxel_),
				instance(instance_)
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceCounters(PerformanceCounters_)
#endif
			{
			}

			void operator()(tbb::blocked_range<uint32_t> const& r) const {

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				PerformanceType::reference local_perf = PerformanceCounters.local();
				tTime const tStartOp = high_resolution_clock::now();
				++local_perf.operations;
#endif
				uint32_t const // pull out into registers from memory
					vxl_begin(r.begin()),
					vxl_end(r.end());

				uint32_t const Transparency(instance.getTransparency());
				bool const Faded(instance.isFaded());

				uint32_t const flags(instance.getFlags());

				auto const& __restrict model(instance.getModel());

				float const YDimension((float)model._maxDimensions.y);

				XMVECTOR const maxDimensionsInv(XMLoadFloat3A(&model._maxDimensionsInv)), maxDimensions(uvec4_v(model._maxDimensions).v4f());

				voxB::voxelDescPacked const* __restrict pVoxelsIn(voxelsIn + vxl_begin);

				constexpr uint32_t const PREFETCH_ELEMENTS = CACHE_LINE_BYTES / sizeof(voxB::voxelDescPacked);
				uint32_t prefetch_count(PREFETCH_ELEMENTS);
				_mm_prefetch((const CHAR*)pVoxelsIn, _MM_HINT_T0);

#pragma loop( ivdep )
				for (uint32_t vxl = vxl_begin; vxl < vxl_end; ++vxl) {
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
					tTime const tStartIter = high_resolution_clock::now();
					++local_perf.iterations;
#endif
					if (0 == --prefetch_count) {
						_mm_prefetch((const CHAR*)pVoxelsIn, _MM_HINT_T0);
						prefetch_count = PREFETCH_ELEMENTS;
					}

					voxB::voxelDescPacked voxel(*pVoxelsIn); // copy out reduces accesses to memory, and its a small very small size structure
					++pVoxelsIn; // sequentially accessed for maximum cache prediction

					XMVECTOR xmMiniVox = getMiniVoxelGridIndex(maxDimensions, maxDimensionsInv, _mm_cvtepi32_ps(voxel.getPosition()));

					[[maybe_unused]] XMVECTOR xmAzimuth, xmPitch;  // rotation vectors (optimized out depending on "Dynamic")

					if constexpr (Dynamic) {
						voxelModelInstance_Dynamic const& __restrict instance_dynamic(static_cast<voxelModelInstance_Dynamic const& __restrict>(instance));	// this resolves to a safe implicit static_cast downcast

						xmAzimuth = instance_dynamic.getAzimuth().v2();
						xmPitch = instance_dynamic.getPitch().v2();

						// rotation order is important
						// in this order, the rotations add together
						// in the other order, each rotation is independent of each other
						// no quaternions, no matrices, just vectors - simplest possible solution.
						xmMiniVox = v3_rotate_azimuth(v3_rotate_pitch(xmMiniVox, xmPitch), xmAzimuth);
					}

					XMVECTOR const xmStreamOut = XMVectorAdd(xmVoxelOrigin, XMVectorScale(XMVectorSetY(xmMiniVox, -YDimension - XMVectorGetY(xmMiniVox)), Iso::MINI_VOX_STEP)); // relative to current ROOT voxel origin

					XMVECTOR const xmIndex(XMVectorMultiplyAdd(xmStreamOut, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

					[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						&& XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
					{
						// Build hash //
						uint32_t hash(0);
						hash |= voxel.getAdjacency();					//           0000 0000 0001 1111
						hash |= (voxel.getOcclusion() << 5);			//           0000 1111 111x xxxx

						bool Transparent(false), Emissive(false);
						if (nullptr != model._State) {

							voxB::voxelState state(model._State[vxl]);
							voxel.Alpha = Transparency; // only applicable if transparent voxel

							state = instance.OnVoxel(voxel, state, vxl);

							if (state.Hidden)
								continue;
							
							Transparent = Faded | state.Transparent;
							Emissive = (state.Emissive | Iso::isEmissive(rootVoxel)) & !Faded;			// dynamic emission state
						}
						else {
							Emissive = Iso::isEmissive(rootVoxel);		// static emission state
						}

						hash |= (Emissive << 12);						// 0000 0000 0001 xxxx xxxx xxxx

						{	// xyz = visible relative UV,  w = detailed occlusion 
							// UV coordinates are not swizzled at this point, however by the fragment shader they are xzy
							// UV coordinate describe the "visible grid" relative position bound to 0...VOXEL_MINIGRID_VISIBLE_XYZ
							// these are not world coordinates! good for sampling view locked/aligned volumes such as the lightmap

							// Make All voxels relative to voxel root origin // inversion neccessary //
							XMVECTOR const xmUVs(XMVectorSetW(xmUV, (float)voxel.getColor())); // srgb is passed to vertex shader which converts it to linear; which is faster than here with cpu

							if (!Transparent) {

								if constexpr (Dynamic) {

									local::voxels_dynamic.emplace_back(
										voxels_dynamic,
										xmStreamOut,
										xmUVs,
										XMVectorAdd(XMVectorRotateRight<2>(xmAzimuth), xmPitch),
										hash
									);
								}
								else {
									local::voxels_static.emplace_back(
										voxels_static,
										xmStreamOut,
										xmUVs,
										hash
									);
								}

							}
							else { // transparency enabled

								hash |= ((voxel.Alpha >> 6) << 13);				// 0000 0000 011x xxxx xxxx xxxx

								if constexpr (Dynamic) {

									local::voxels_trans.emplace_back(
										voxels_trans,
										xmStreamOut,
										xmUVs,
										XMVectorAdd(XMVectorRotateRight<2>(xmAzimuth), xmPitch),
										hash
									);
								}
								else { // sneaky override for *static* transparent voxels

									local::voxels_trans.emplace_back(
										voxels_trans,
										xmStreamOut,
										xmUVs,
										XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f),  // bugfix: BIG bug: (first paramete must be equal to 1.0f, second 0.0f) - was rotated, now transparency adjacency works & solves problem with hidden voxels at 0.0f rotation
										hash
									);
								}

							}
						}

						if (Emissive) {										  // crash prevented at beginning of function

							// the *World position* of the light is stored, so it should be used with a corresponding *world* point in calculations
							// te lightmap volume however is sampled with the uv relative coordinates of a range between 0...VOXEL_MINIGRID_VISIBLE_X
							// and is in the fragment shaderrecieved swizzled in xzy form
							VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, voxel.getColor());

						}
					}
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
					local_perf.iteration_duration = std::max(local_perf.iteration_duration, high_resolution_clock::now() - tStartIter);
#endif
				} // end for

				// ####################################################################################################################
				// ensure all batches are  output (residual/remainder)
				if constexpr (Dynamic) {
					local::voxels_dynamic.out(voxels_dynamic);
				}
				else {
					local::voxels_static.out(voxels_static);
				}
				
				local::voxels_trans.out(voxels_trans);
				// ####################################################################################################################

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				local_perf.operation_duration = high_resolution_clock::now() - tStartOp;
				local_perf.thread_id = tbb::this_tbb_thread::get_id();

#endif
			}
		} const RenderFuncBlockChunk;

		//##########################################################################################################################################//

		[[maybe_unused]] tbb::atomic<VertexDecl::VoxelNormal*> pVoxelsOutStatic;
		[[maybe_unused]] tbb::atomic<VertexDecl::VoxelDynamic*> pVoxelsOutDynamic;
		tbb::atomic<VertexDecl::VoxelDynamic*> pVoxelsOutTrans;

		if (!instance.isFaded()) {
			if constexpr (Dynamic) {
				pVoxelsOutDynamic = voxels_dynamic.fetch_and_add<tbb::release>(_numVoxels - _numVoxelsTransparent);
			}
			else {
				pVoxelsOutStatic = voxels_static.fetch_and_add<tbb::release>(_numVoxels - _numVoxelsTransparent);
			}
			pVoxelsOutTrans = voxels_trans.fetch_and_add<tbb::release>(_numVoxelsTransparent);
		}
		else {
			pVoxelsOutTrans = voxels_trans.fetch_and_add<tbb::release>(_numVoxels + _numVoxelsTransparent);
		}

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		PerformanceType PerformanceCounters;
#endif
	/*	RenderFuncBlockChunk(xmVoxelOrigin,
			_Voxels, pCurVoxelBatchOut, oVoxel,
			highLightRange, State, instance
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			, PerformanceCounters
#endif
		)(tbb::blocked_range<uint32_t>(0, numTraverse));
		*/

		tbb::affinity_partitioner part;

		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, _numVoxels, eThreadBatchGrainSize::MODEL),
			RenderFuncBlockChunk(xmVoxelOrigin, 
				XMVectorScale(p2D_to_v2(voxelIndex), Iso::INVERSE_WORLD_GRID_FSIZE),
				_Voxels, 
				pVoxelsOutStatic, pVoxelsOutDynamic, pVoxelsOutTrans,
				oVoxel,
				instance
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceCounters
#endif
			)
			, part
		);

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		PerformanceResult& result(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
		result += PerformanceCounters;	// add to global total this model instance performance counters
#endif

	}
} // end namespace voxB
} // end namespace Volumetric


#endif

