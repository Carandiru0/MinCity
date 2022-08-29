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
#include "voxelScreen.h"
#include "voxelSequence.h"
#include "voxLink.h"
#include "adjacency.h"

#include "sBatched.h"
#include <Utility/bit_volume.h>

#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#include "performance.h"
#endif

#pragma warning(disable : 4804) // unsafe usage of bool in setMetallic

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
		
		voxCoord() = default;
		
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

	typedef struct alignas(8) voxelDescPacked	// Final 256x256x256 Structure - *bugfix - alignment explicitly set to 8, the maximum size of structure. Aligning to 16 doubles the size of the structure. In the interest of bandwidth and file size (eg. .v1xa sequences) the alignment is set to 8. *do not increase*
	{											// ***do not change alignment*** *breaks .v1x &  .v1xa data files if alignment is changed*
		union
		{
			struct 
			{
				uint8_t // (largest type uses 8 bits)
					x : 8,							// bits 1 : 24, Position (0 - 255) 256 Values per Component
					y : 8,
					z : 8,
					Left : 1,						// bits 25 : 29, Adjacency
					Right : 1,
					Front : 1,
					Back : 1,
					Above : 1,
					Hidden : 1,						// bit 30, Visibility
					Reserved_0 : 1,					// bit 31, Unused
					Reserved_1 : 1;					// bit 32, Unused
			};
			
			uint32_t Data; // union "mask" of above
		};
		
		union
		{
			struct
			{
				uint32_t // (largest type uses 24 bits)
					Color : 24,						// RGB 16.78 million voxel colors
					Video : 1,						// Videoscreen
					Emissive : 1,					// Emission
					Transparent : 1,				// Transparency
					Metallic : 1,					// Metallic
					Roughness : 4;					// Roughness
			};

			uint32_t RGBM;
		};


		inline __m128i const XM_CALLCONV  getPosition() const { return(_mm_setr_epi32(x, y, z, 0)); }
		
		inline uint32_t const 			  getColor() const { return(Color); }

		inline uint32_t const			  getAdjacency() const { return((Left << 4U) | (Right << 3U) | (Front << 2U) | (Back << 1U) | (Above)); }
		inline void						  setAdjacency(uint32_t const Adj) { 
			Left	= (0 != (BIT_ADJ_LEFT & Adj));
			Right	= (0 != (BIT_ADJ_RIGHT & Adj));
			Front	= (0 != (BIT_ADJ_FRONT & Adj));
			Back	= (0 != (BIT_ADJ_BACK & Adj));
			Above	= (0 != (BIT_ADJ_ABOVE & Adj));
		}

		// Material (8 bits)
		// -video (2 values)
		// -emissive (2 values)
		// -transparent (2 values)
		// -metallic (2 values)
		// -roughness (16 values)
		inline bool const				  isMetallic() const { return(Metallic); }
		inline void					      setMetallic(bool const metallic) { Metallic = metallic; }

		inline float const				  getRoughness() const { return(float(Roughness) / 15.0f); }  // roughness returned in the range [0.0f ... 1.0f] **effectively only 4 values are ever returned or used
		inline void						  setRoughness(float const roughness) { Roughness = SFM::floor_to_u32(SFM::saturate(roughness) * 15.0f + 0.5f); } // accepts value in the [0.0f ... 1.0f]
		
		inline voxelNormal const		  getNormal() const { return(voxelNormal(getAdjacency())); }

		__inline __declspec(noalias) bool const operator<(voxelDescPacked const& rhs) const
		{
			// slices ordered by Z 
			// (z * xMax * yMax) + (y * xMax) + x;

			// slices ordered by Y: <---- USING Y
			// (y * xMax * zMax) + (z * xMax) + x;

			uint32_t const index((y * Volumetric::MODEL_MAX_DIMENSION_XYZ * Volumetric::MODEL_MAX_DIMENSION_XYZ) + (z * Volumetric::MODEL_MAX_DIMENSION_XYZ) + x);
			uint32_t const index_rhs((rhs.y * Volumetric::MODEL_MAX_DIMENSION_XYZ * Volumetric::MODEL_MAX_DIMENSION_XYZ) + (rhs.z * Volumetric::MODEL_MAX_DIMENSION_XYZ) + rhs.x);

			return(index < index_rhs);
		}
		
		__inline __declspec(noalias) bool const operator!=(voxelDescPacked const& rhs) const
		{	
			return(Data != rhs.Data);
		}
		inline voxelDescPacked(voxCoord const Coord, uint8_t const Adj, uint32_t const inColor)
			: x(Coord.x), y(Coord.y), z(Coord.z),
				Left(0 != (BIT_ADJ_LEFT & Adj)), Right(0 != (BIT_ADJ_RIGHT & Adj)), Front(0 != (BIT_ADJ_FRONT & Adj)),
			    Back(0 != (BIT_ADJ_BACK & Adj)), Above(0 != (BIT_ADJ_ABOVE & Adj)),
				Hidden(0), Reserved_0(0), Reserved_1(0),
				Color(inColor), Video(0), Emissive(0), Transparent(0), Metallic(0), Roughness(0)
		{}
		voxelDescPacked() = default;

		voxelDescPacked(voxelDescPacked const& __restrict rhs) = default;
		voxelDescPacked(voxelDescPacked&& __restrict rhs) = default;

		voxelDescPacked const& operator=(voxelDescPacked const& __restrict rhs) noexcept
		{
			memcpy(&(*this), &rhs, sizeof(voxelDescPacked));
			//Data = rhs.Data;
			//RGBM = rhs.RGBM;

			return(*this);
		}
		voxelDescPacked const& operator=(voxelDescPacked&& __restrict rhs) noexcept
		{
			memcpy(&(*this), &rhs, sizeof(voxelDescPacked));
			//Data = rhs.Data;
			//RGBM = rhs.RGBM;
			 
			return(*this);
		}
		
	} voxelDescPacked;
	
	using model_volume = bit_volume<Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ>; // 2 MB

	STATIC_INLINE_PURE uint32_t const __vectorcall encode_adjacency(uvec4_v const xmIndex, model_volume const* const __restrict bits) // *note - good only for model max size dimensions
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
			adjacent |= bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z) << Volumetric::adjacency::left;
		}
		if (iIndex.x + 1 < model_volume::width()) {
			adjacent |= bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z) << Volumetric::adjacency::right;
		}
		if (iIndex.z - 1 >= 0) {
			adjacent |= bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1) << Volumetric::adjacency::front;
		}
		if (iIndex.z + 1 < model_volume::depth()) {
			adjacent |= bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1) << Volumetric::adjacency::back;
		}
		if (iIndex.y + 1 < model_volume::height()) {
			adjacent |= bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z) << Volumetric::adjacency::above;
		}
		return(adjacent);
	}
	
	typedef struct voxelModelFeatures
	{
		static constexpr uint8_t const
			RESERVED_0 = (1 << 0),
			RESERVED_1 = (1 << 1),
			SEQUENCE = (1 << 2),
			VIDEOSCREEN = (1 << 3);

		voxelScreen const*		videoscreen = nullptr;
		voxelSequence const*	sequence = nullptr;
		
		voxelModelFeatures() = default;

		voxelModelFeatures(voxelModelFeatures&& src)
		{
			// otherwise pointers are set to nullptr by default initialization (above)
			std::swap<voxelScreen const*>(videoscreen, src.videoscreen);
			std::swap<voxelSequence const*>(sequence, src.sequence);
		}
		~voxelModelFeatures() {
			SAFE_DELETE(videoscreen);
			SAFE_DELETE(sequence);
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
		voxelDescPacked const* __restrict   _Voxels;			// Finalized linear array of voxels (constant readonly memory)

		uint32_t 		_numVoxels;						// # of voxels activated
		uint32_t		_numVoxelsEmissive;
		uint32_t		_numVoxelsTransparent;
		
		uvec4_t 		_maxDimensions;					// actual used size of voxel model
		XMFLOAT3A 		_maxDimensionsInv;
		XMFLOAT3A		_Extents;						// xz = localarea bounds, y = maxdimensions.y (Extents are 0.5f * (width/height/depth) as in origin at very center of model on all 3 axis)
		rect2D_t		_LocalArea;						// Rect defining local area in grid units (converted from minivoxels to voxels)
		
		voxelModelFeatures _Features;
		bool			   _Mapped;						// "_Voxels" points to memory mapped file (virtual memory) containing contigous data
		
		inline voxelModelBase() 
			: _maxDimensions{}, _maxDimensionsInv{}, _Extents{}, _numVoxels(0), _numVoxelsEmissive(0), _numVoxelsTransparent(0), _Voxels(nullptr), _Mapped(false)
		{}

		voxelModelBase(voxelModelBase&& src)
			: _maxDimensions(src._maxDimensions), _maxDimensionsInv(src._maxDimensionsInv), _Extents(src._Extents), _LocalArea(src._LocalArea), _Features(std::move(src._Features)), 
			_numVoxels(src._numVoxels), _numVoxelsEmissive(src._numVoxelsEmissive), _numVoxelsTransparent(src._numVoxelsTransparent), _Voxels(nullptr), _Mapped(src._Mapped)
		{
			std::swap<voxelDescPacked const* __restrict>(_Voxels, src._Voxels);
		}

		voxelModelBase(uint32_t const width, uint32_t const height, uint32_t const depth)
			: _maxDimensions{}, _maxDimensionsInv{}, _Extents{}, _numVoxels(0), _numVoxelsEmissive(0), _numVoxelsTransparent(0), _Voxels(nullptr), _Mapped(false)
		{
			vec4_v const maxDimensions(width, height, depth);
			
			maxDimensions.xyzw(_maxDimensions);
			
			XMVECTOR const xmDimensions(maxDimensions.v4f());
			XMStoreFloat3A(&_maxDimensionsInv, XMVectorReciprocal(xmDimensions));

			_Voxels = (voxelDescPacked const* __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * (width * height * depth), CACHE_LINE_BYTES); // matches voxBinary usage (alignment)
			
			ComputeLocalAreaAndExtents();
		}
		
		void ComputeLocalAreaAndExtents();

		~voxelModelBase(); // defined at end of voxBinary.cpp

	private:
		voxelModelBase(voxelModelBase const&) = delete;
		voxelModelBase& operator=(voxelModelBase const&) = delete;
		
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
		: _identity(std::forward<voxelModelIdent<Dynamic>&&>(identity))
		{}

		voxelModel(voxelModel<Dynamic>&& src)
			: voxelModelBase(std::forward<voxelModel<Dynamic>&&>(src)), _identity(src._identity)
		{}

		voxelModel(uint32_t const width, uint32_t const height, uint32_t const depth)
			: voxelModelBase(width, height, depth), _identity{ 0, 0 }
		{}
		template<bool const EmissionOnly, bool const Faded>
		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex, Iso::Voxel const& __restrict oVoxel, voxelModelInstance<Dynamic> const& instance, 
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const;

	private:
		voxelModel(voxelModel<Dynamic> const&) = delete;
		voxelModel<Dynamic>& operator=(voxelModel<Dynamic> const&) = delete;
	};
		
	read_only inline XMVECTORF32 const _xmORIGINMOVE{ 0.5f, 0.5f, 0.5f, 0.5f };
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
	template<bool const EmissionOnly, bool const Faded>
	__inline void XM_CALLCONV voxelModel<Dynamic>::Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		Iso::Voxel const& __restrict oVoxel,
		voxelModelInstance<Dynamic> const& instance,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const
	{
		typedef struct no_vtable sRenderFuncBlockChunk {

		private:
			XMVECTOR const											xmVoxelOrigin;
			voxB::voxelDescPacked const* const __restrict			voxelsIn;
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict		voxels_static;
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict		voxels_dynamic;
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict		voxels_trans;
			voxelModelInstance<Dynamic> const& __restrict			instance;

			XMVECTOR const  maxDimensionsInv, maxDimensions;
			float const		YDimension;
			uint32_t const	Transparency;

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			PerformanceType& PerformanceCounters;
#endif		

			sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
		public:
			__forceinline sRenderFuncBlockChunk(FXMVECTOR xmVoxelOrigin_,
				voxB::voxelDescPacked const* const __restrict voxelsIn_,
				tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static_,
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic_,
				tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans_,
				voxelModelInstance<Dynamic> const& __restrict instance_
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceType& PerformanceCounters_
#endif
			) // add in ground height from root voxel passed in, total area encompassing the model has been averaged / flat //
				:
				xmVoxelOrigin(XMVectorSubtract(xmVoxelOrigin_, XMVectorSet(0.0f, instance_.getElevation(), 0.0f, 0.0f))),
				voxelsIn(voxelsIn_), voxels_static(voxels_static_), voxels_dynamic(voxels_dynamic_), voxels_trans(voxels_trans_),
				instance(instance_),

				maxDimensionsInv(XMLoadFloat3A(&instance_.getModel()._maxDimensionsInv)), 
				maxDimensions(uvec4_v(instance_.getModel()._maxDimensions).v4f()),
				YDimension(XMVectorGetY(maxDimensions)),
				Transparency(instance_.getTransparency())
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

					[[maybe_unused]] XMVECTOR xmOrient, xmExtra;  // combined rotation vectors (optimized out depending on "Dynamic")

					if constexpr (Dynamic) {
						voxelModelInstance_Dynamic const& __restrict instance_dynamic(static_cast<voxelModelInstance_Dynamic const& __restrict>(instance));	// this resolves to a safe implicit static_cast downcast

						XMVECTOR const xmRoll(instance_dynamic.getRoll().v2());
						XMVECTOR const xmYaw(instance_dynamic.getYaw().v2());
						XMVECTOR const xmPitch(instance_dynamic.getPitch().v2());
						// rotation order is important
						// in this order, the rotations add together
						// in the other order, each rotation is independent of each other
						// no quaternions, no matrices, just vectors - simplest possible solution.
						xmOrient = XMVectorAdd(XMVectorRotateRight<2>(xmYaw), xmPitch);
						xmExtra = xmRoll;
						xmMiniVox = v3_rotate_roll(v3_rotate_yaw(v3_rotate_pitch(xmMiniVox, xmPitch), xmYaw), xmRoll);
					}

					XMVECTOR xmStreamOut = XMVectorAdd(xmVoxelOrigin, XMVectorScale(XMVectorSetY(xmMiniVox, SFM::__fms(YDimension, -0.5f, XMVectorGetY(xmMiniVox))), Iso::MINI_VOX_STEP)); // relative to current ROOT voxel origin, precise height offset for center of model

					XMVECTOR xmIndex(XMVectorMultiplyAdd(xmStreamOut, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

					[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						&& XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
					{			
						voxel = instance.OnVoxel(xmIndex, voxel, vxl);  // per voxel operations!

						if (voxel.Hidden)
							continue;

						uint32_t const color(voxel.getColor() & 0x00FFFFFF); // remove alpha
						bool const Emissive((voxel.Emissive & !Faded) & (bool)color); // if color is true black it's not emissive
						
						if constexpr (!EmissionOnly)
						{	// xyz = visible relative UV,  w = detailed occlusion 
							// UV coordinates are not swizzled at this point, however by the fragment shader they are xzy
							// UV coordinate describe the "visible grid" relative position bound to 0...VOXEL_MINIGRID_VISIBLE_XYZ
							// these are not world coordinates! good for sampling view locked/aligned volumes such as the lightmap

							bool const Transparent(Faded | voxel.Transparent);
							
							// update xmStreamOut if xmIndex is modified in instance.OnVoxel
							xmStreamOut = SFM::__fms(xmIndex, Volumetric::_xmInvTransformToIndexScale, _xmTransformToIndexBiasOverScale);

							// Build hash //
							uint32_t hash(0);
							// ** see uniforms.vert for definition of constants used here **

							hash |= voxel.getAdjacency();						//           0000 0000 0001 1111
							hash |= (Emissive << 5);							//           0000 0000 001x xxxx
							hash |= (voxel.Metallic << 6);						// 0000 0000 0000 xxxx x1xx xxxx
							hash |= (voxel.Roughness << 8);						// 0000 0000 0000 1111 Uxxx xxxx

							// Make All voxels relative to voxel root origin // inversion neccessary //
							// srgb is passed to vertex shader which converts it to linear; which is faster than here with cpu

							if (!Transparent) {

								if constexpr (Dynamic) {

									local::voxels_dynamic.emplace_back(
										voxels_dynamic,
										xmStreamOut,
										XMVectorSetW(xmExtra, (float)color), // dynamic uses extra vector
										xmOrient,
										hash
									);
								}
								else {
									local::voxels_static.emplace_back(
										voxels_static,
										xmStreamOut,
										XMVectorSet(0.0f, 0.0f, 0.0f, (float)color), // static does not use extra vector except color (.w)
										hash
									);
								}

							}
							else { // transparency enabled

								hash |= ((Transparency >> 6) << 13);				// 0000 0000 011U xxxx xxxx xxxx

								if constexpr (Dynamic) {

									local::voxels_trans.emplace_back(
										voxels_trans,
										xmStreamOut,
										XMVectorSetW(xmExtra, (float)color), // dynamic uses extra vector
										xmOrient,
										hash
									);
								}
								else { // sneaky override for *static* transparent voxels

									local::voxels_trans.emplace_back(
										voxels_trans,
										xmStreamOut,
										XMVectorSet(1.0f, 0.0f, 0.0f, (float)color), // static does not use extra vector except color (.w) --- *bugfix since this is actualy upgraded to dynamic voxel, make rotation values correct for no rotation in the extra vector
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
							VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, color);						
							
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

		uint32_t const vxl_count(instance.getVoxelCount());
		uint32_t const vxl_transparent_count(instance.getVoxelTransparentCount());

		if (!instance.isFaded()) {
			if constexpr (Dynamic) {
				pVoxelsOutDynamic = voxels_dynamic.fetch_and_add<tbb::release>(vxl_count - vxl_transparent_count);
			}
			else {
				pVoxelsOutStatic = voxels_static.fetch_and_add<tbb::release>(vxl_count - vxl_transparent_count);
			}
			pVoxelsOutTrans = voxels_trans.fetch_and_add<tbb::release>(vxl_transparent_count);
		}
		else { // faded (all transparent)
			pVoxelsOutTrans = voxels_trans.fetch_and_add<tbb::release>(vxl_count);
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

		uint32_t const vxl_offset(instance.getVoxelOffset());
		
		tbb::auto_partitioner part; /*load balancing - do NOT change - adapts to variance of whats in the voxel grid*/
		tbb::parallel_for(tbb::blocked_range<uint32_t>(vxl_offset, vxl_offset + vxl_count, eThreadBatchGrainSize::MODEL),
			RenderFuncBlockChunk(xmVoxelOrigin,
				_Voxels, 
				pVoxelsOutStatic, pVoxelsOutDynamic, pVoxelsOutTrans,
				instance
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceCounters
#endif
			), part
		);

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		PerformanceResult& result(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
		result += PerformanceCounters;	// add to global total this model instance performance counters
#endif

	}
} // end namespace voxB
} // end namespace Volumetric


#endif

