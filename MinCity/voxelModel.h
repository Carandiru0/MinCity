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
#include <Math/quat_t.h>
#include <Utility/bit_volume.h>
#include <Utility/bit_row.h>
#include "IsoVoxel.h"
#include "voxelScreen.h"
#include "voxelSequence.h"
#include "voxLink.h"
#include "adjacency.h"

#include "Declarations.h" // must be defined b4 sBatched is included (defines __streaming_store specific to usage in this file)
#include "sBatchedByIndexOut.h"

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
} // end ns

// definitions //
namespace Volumetric
{
	template<typename VertexDeclaration, size_t const direct_buffer_size>
	struct voxelBufferReference
	{
		std::atomic<VertexDeclaration*>& __restrict                               voxels;
		VertexDeclaration const* const                                            voxels_start;
		bit_row_atomic<direct_buffer_size>* const __restrict& __restrict          bits; // *bugfix - must be atomic, otherwise random voxels are not rendered

		explicit voxelBufferReference(std::atomic<VertexDeclaration*>& __restrict voxels_, VertexDeclaration const* const& __restrict voxels_start_, bit_row_atomic<direct_buffer_size>* const __restrict& __restrict bits_)
		: voxels(voxels_), voxels_start(voxels_start_), bits(bits_)
		{}
	};

	constexpr size_t const terrain_direct_buffer_size = Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL; // terrain does not require multiplier
	constexpr size_t const static_direct_buffer_size  = Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL * Volumetric::DIRECT_BUFFER_SIZE_MULTIPLIER;
	constexpr size_t const dynamic_direct_buffer_size = Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * Volumetric::DIRECT_BUFFER_SIZE_MULTIPLIER;

	using voxelBufferReference_Terrain = voxelBufferReference<VertexDecl::VoxelNormal, terrain_direct_buffer_size>;
	using voxelBufferReference_Static  = voxelBufferReference<VertexDecl::VoxelNormal, static_direct_buffer_size>;
	using voxelBufferReference_Dynamic = voxelBufferReference<VertexDecl::VoxelDynamic, dynamic_direct_buffer_size>;
	
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
			: Nx((0 != (BIT_ADJ_LEFT & Adjacency))  ^ (0 != (BIT_ADJ_RIGHT & Adjacency))),
			  Ny((0 != (BIT_ADJ_ABOVE & Adjacency)) ^ (0 != (BIT_ADJ_BELOW & Adjacency))),
			  Nz((0 != (BIT_ADJ_FRONT & Adjacency)) ^ (0 != (BIT_ADJ_BACK & Adjacency))),
																	// (sign part, point in direction opposite of adjacency
			  SNx(0 != (BIT_ADJ_LEFT & Adjacency)),	// left occupied, point right (+1) , else point left (-1)	(dependent on absolute part Nx == 1, if Nx == 0 this is a don't care)				
			  SNy(0 != (BIT_ADJ_BELOW & Adjacency)),
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
					Left : 1,						// bits 25 : 30, Adjacency
					Right : 1,
					Front : 1,
					Back : 1,
					Above : 1,
					Below : 1,
					Hidden : 1,						// bit 31, Visibility
					Reserved : 1;					// bit 32, Unused
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

		inline uint32_t const			  getAdjacency() const { return((Left << 5U) | (Right << 4U) | (Front << 3U) | (Back << 2U) | (Above << 1U) | (Below)); }
		inline void						  setAdjacency(uint32_t const Adj) { 
			Left	= (0 != (BIT_ADJ_LEFT & Adj));
			Right	= (0 != (BIT_ADJ_RIGHT & Adj));
			Front	= (0 != (BIT_ADJ_FRONT & Adj));
			Back	= (0 != (BIT_ADJ_BACK & Adj));
			Above	= (0 != (BIT_ADJ_ABOVE & Adj));
			Below	= (0 != (BIT_ADJ_BELOW & Adj));
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
		
		inline bool const				  isEmissive() const { return(Emissive); }
		inline void					      setEmissive(bool const emissive) { Emissive = emissive; }

		inline bool const				  isTransparent() const { return(Transparent); }
		inline void					      setTransparent(bool const transparent) { Transparent = transparent; }


		inline voxelNormal const		  getNormal() const { return(voxelNormal(getAdjacency())); }

		__inline __declspec(noalias) bool const operator<(voxelDescPacked const& rhs) const
		{
			// slices ordered by Z 
			// (z * xMax * yMax) + (y * xMax) + x;

			// slices ordered by Y: <---- USING Y
			// (y * xMax * zMax) + (z * xMax) + x;

			uint32_t const index((y * Volumetric::MODEL_MAX_DIMENSION_XYZ * Volumetric::MODEL_MAX_DIMENSION_XYZ) + (z * Volumetric::MODEL_MAX_DIMENSION_XYZ) + x);
			uint32_t const index_rhs((rhs.y * Volumetric::MODEL_MAX_DIMENSION_XYZ * Volumetric::MODEL_MAX_DIMENSION_XYZ) + (rhs.z * Volumetric::MODEL_MAX_DIMENSION_XYZ) + rhs.x);

			return(index >= index_rhs); // *bugfix: this must be greater-than-equal for correct ordering of voxels, zbuffer depends on this to increase precision & performance (culling, zero overdraw)
		}
		
		__inline __declspec(noalias) bool const operator!=(voxelDescPacked const& rhs) const
		{	
			return(Data != rhs.Data);
		}
		inline voxelDescPacked(voxCoord const Coord, uint8_t const Adj, uint32_t const inColor)
			: x(Coord.x), y(Coord.y), z(Coord.z),
				Left(0 != (BIT_ADJ_LEFT & Adj)), Right(0 != (BIT_ADJ_RIGHT & Adj)), Front(0 != (BIT_ADJ_FRONT & Adj)),
			    Back(0 != (BIT_ADJ_BACK & Adj)), Above(0 != (BIT_ADJ_ABOVE & Adj)), Below(0 != (BIT_ADJ_BELOW & Adj)),
				Hidden(0), Reserved(0),
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
		if (iIndex.y - 1 >= 0) {
			adjacent |= bits->read_bit(iIndex.x, iIndex.y - 1, iIndex.z) << Volumetric::adjacency::below;
		}
		if (iIndex.y + 1 < model_volume::height()) {
			adjacent |= bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z) << Volumetric::adjacency::above;
		}
		return(adjacent);
	}
	
	typedef struct voxelModelFeatures
	{
		static constexpr uint8_t const
			RESERVED_0  = (1 << 0),
			RESERVED_1  = (1 << 1),
			SEQUENCE    = (1 << 2),
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
		XMFLOAT3A		_Extents;						// xz = localarea bounds, y = maxdimensions.y (Extents are 0.5f * (width/height/depth) as in origin at very center of model on all 3 axis) (unit: voxels) *note this is not in minivoxels
		float			_Radius;						// pre-calculated radius - based directly off of extents (unit: voxels) *note this is not in minivoxels
		rect2D_t		_LocalArea;						// Rect defining local area in grid units (converted from minivoxels to voxels) (unit: voxels) *note this is not in minivoxels
		
		voxelModelFeatures _Features;
		
		inline voxelModelBase() 
			: _maxDimensions{}, _maxDimensionsInv{}, _Extents{}, _Radius{}, _numVoxels(0), _numVoxelsEmissive(0), _numVoxelsTransparent(0), _Voxels(nullptr)
		{}

		voxelModelBase(voxelModelBase&& src)
			: _maxDimensions(src._maxDimensions), _maxDimensionsInv(src._maxDimensionsInv), _Extents(src._Extents), _Radius(src._Radius), _LocalArea(src._LocalArea), _Features(std::move(src._Features)),
			_numVoxels(src._numVoxels), _numVoxelsEmissive(src._numVoxelsEmissive), _numVoxelsTransparent(src._numVoxelsTransparent), _Voxels(nullptr)
		{
			std::swap<voxelDescPacked const* __restrict>(_Voxels, src._Voxels);
		}

		voxelModelBase(uint32_t const width, uint32_t const height, uint32_t const depth)
			: _maxDimensions{}, _maxDimensionsInv{}, _Extents{}, _Radius{}, _numVoxels(0), _numVoxelsEmissive(0), _numVoxelsTransparent(0), _Voxels(nullptr)
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
		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, FXMVECTOR xmVoxelOrient, 
										 voxelModelInstance<Dynamic> const& __restrict instance,
										 voxelBufferReference_Static& __restrict statics,
										 voxelBufferReference_Dynamic& __restrict dynamics,
										 voxelBufferReference_Dynamic& __restrict trans,
										 tbb::affinity_partitioner& __restrict part) const;

	private:
		voxelModel(voxelModel<Dynamic> const&) = delete; 
		voxelModel<Dynamic>& operator=(voxelModel<Dynamic> const&) = delete;
	};
		
	STATIC_INLINE_PURE XMVECTOR XM_CALLCONV getMiniVoxelGridIndex(FXMVECTOR maxDimensions, FXMVECTOR maxDimensionsInv,
		                                                          FXMVECTOR xmVoxelRelativeModelPosition)
	{
		// Use relative coordinates in signed fashion so origin is forced to middle of model

		// normalize relative coordinates (mul)  -  change from [0.0f ... 1.0f] tp [-1.0f ... 1.0f]
		XMVECTOR xmPlotGridSpace = SFM::__fms(xmVoxelRelativeModelPosition, maxDimensionsInv, XMVectorReplicate(0.5f));

		// now scale by dimension size (this is halved by now scaling above by two - already * 2.0f dont worry)
		xmPlotGridSpace = XMVectorMultiply(xmPlotGridSpace, maxDimensions);

		return(xmPlotGridSpace);
	}

#ifndef NDEBUG // force enable optimizations - affects debug builds only
#pragma optimize( "s", on )
#endif
	template<bool const Dynamic>
	template<bool const EmissionOnly, bool const Faded>
	__inline void XM_CALLCONV voxelModel<Dynamic>::Render(FXMVECTOR xmVoxelOrigin, FXMVECTOR xmVoxelOrient,
														  voxelModelInstance<Dynamic> const& __restrict instance,
														  voxelBufferReference_Static& __restrict statics,
														  voxelBufferReference_Dynamic& __restrict dynamics,
														  voxelBufferReference_Dynamic& __restrict trans,
														  tbb::affinity_partitioner& __restrict part) const
	{
		typedef struct no_vtable sRenderFuncBlockChunk {

		private:
			XMVECTOR const											                xmVoxelOrigin;
			[[maybe_unused]] XMVECTOR const                                         xmVoxelOrient;
			uint32_t const                                                          vxl_offset;
			voxB::voxelDescPacked const* const __restrict  			                voxelsIn;
			VertexDecl::VoxelNormal* const __restrict          		                voxels_static;
			VertexDecl::VoxelDynamic* const __restrict		                        voxels_dynamic;
			VertexDecl::VoxelDynamic* const __restrict		                        voxels_trans;
			bit_row_reference_atomic<static_direct_buffer_size>&& __restrict        voxels_static_bits;
			bit_row_reference_atomic<dynamic_direct_buffer_size>&& __restrict       voxels_dynamic_bits;
			bit_row_reference_atomic<dynamic_direct_buffer_size>&& __restrict       voxels_trans_bits;
			voxelModelInstance<Dynamic> const& __restrict                           instance;

			XMVECTOR const  maxDimensionsInv, maxDimensions;
			[[maybe_unused]] float const Sign;  // packing/encoding of quaternion and color
			float const		YDimension;
			uint32_t const	Transparency;

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			PerformanceType& PerformanceCounters;
#endif		

			sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
		public:
			__forceinline explicit sRenderFuncBlockChunk(FXMVECTOR xmVoxelOrigin_, FXMVECTOR xmVoxelOrient_,
				uint32_t const vxl_offset_,
				voxB::voxelDescPacked const* const __restrict& __restrict voxelsIn_,
				VertexDecl::VoxelNormal* const __restrict& __restrict voxels_static_,
				VertexDecl::VoxelDynamic* const __restrict& __restrict voxels_dynamic_,
				VertexDecl::VoxelDynamic* const __restrict& __restrict voxels_trans_,
				bit_row_reference_atomic<static_direct_buffer_size>&& __restrict voxels_static_bits_,
				bit_row_reference_atomic<dynamic_direct_buffer_size>&& __restrict voxels_dynamic_bits_,
				bit_row_reference_atomic<dynamic_direct_buffer_size>&& __restrict voxels_trans_bits_,
				voxelModelInstance<Dynamic> const& __restrict instance_
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				, PerformanceType& PerformanceCounters_
#endif
			) 
				:
				xmVoxelOrigin(xmVoxelOrigin_), xmVoxelOrient(xmVoxelOrient_), vxl_offset(vxl_offset_),
				voxelsIn(voxelsIn_), 
				voxels_static(voxels_static_), voxels_dynamic(voxels_dynamic_), voxels_trans(voxels_trans_),
				voxels_static_bits(std::forward<bit_row_reference_atomic<static_direct_buffer_size>&&>(voxels_static_bits_)),
				voxels_dynamic_bits(std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(voxels_dynamic_bits_)),
				voxels_trans_bits(std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(voxels_trans_bits_)),
				instance(instance_),
				maxDimensionsInv(XMLoadFloat3A(&instance_.getModel()._maxDimensionsInv)), 
				maxDimensions(uvec4_v(instance_.getModel()._maxDimensions).v4f()),
				YDimension(XMVectorGetY(maxDimensions)),
				Transparency(instance_.getTransparency()),
				Sign((XMVectorGetW(xmVoxelOrient_) < 0.0f) ? -1.0f : 1.0f) // trick, the first 3 components x,y,z are sent to vertex shader where the quaternion is then decoded. see uniforms.vert - decode_quaternion() [bandwidth optimization]
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION                                  // default is positive. the sign is packed into color. *color* must not equal zero for sign to be preserved
				, PerformanceCounters(PerformanceCounters_)
#endif
			{}
			sRenderFuncBlockChunk(sRenderFuncBlockChunk const& rhs)
				: xmVoxelOrigin(rhs.xmVoxelOrigin), xmVoxelOrient(rhs.xmVoxelOrient), vxl_offset(rhs.vxl_offset),
				voxelsIn(rhs.voxelsIn),
				voxels_static(rhs.voxels_static), voxels_dynamic(rhs.voxels_dynamic), voxels_trans(rhs.voxels_trans),
				voxels_static_bits(std::forward<bit_row_reference_atomic<static_direct_buffer_size>&&>(rhs.voxels_static_bits)),
				voxels_dynamic_bits(std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(rhs.voxels_dynamic_bits)),
				voxels_trans_bits(std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(rhs.voxels_trans_bits)),
				instance(rhs.instance),
				maxDimensionsInv(rhs.maxDimensionsInv),
				maxDimensions(rhs.maxDimensions),
				YDimension(rhs.YDimension),
				Transparency(rhs.Transparency),
				Sign(rhs.Sign)
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION                                    
				, PerformanceCounters(rhs.PerformanceCounters)
#endif
			{}

			void operator()(tbb::blocked_range<uint32_t> const& r) const {

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				PerformanceType::reference local_perf = PerformanceCounters.local();
				tTime const tStartOp = high_resolution_clock::now();

				++local_perf.operations;
#endif
				struct no_vtable {
					    
					using VoxelLocalBatchNormal = sBatchedByIndexOut<VertexDecl::VoxelNormal, eStreamingBatchSize::MODEL>;
					using VoxelLocalBatchDynamic = sBatchedByIndexOut<VertexDecl::VoxelDynamic, eStreamingBatchSize::MODEL>;

					std::conditional_t<Dynamic, VoxelLocalBatchDynamic, VoxelLocalBatchNormal> voxels{};
					VoxelLocalBatchDynamic voxels_trans{};
				} local; // *bugfix - no need for thread_local global variable(s), skips the lookup (hashmap for thread_locals) aswell. this reserve a little stack memory instead.

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

					if constexpr (Dynamic) { // rotation quaternion (optimized out depending on "Dynamic")
						// orient voxel by quaternion
						xmMiniVox = v3_rotate(xmMiniVox, xmVoxelOrient);
					}

					XMVECTOR xmStreamOut = XMVectorAdd(xmVoxelOrigin, XMVectorScale(XMVectorSetY(xmMiniVox, SFM::__fms(YDimension, -0.5f, XMVectorGetY(xmMiniVox))), Iso::MINI_VOX_STEP)); // relative to current ROOT voxel origin, precise height offset for center of model
					//xmStreamOut = XMVectorAdd(xmStreamOut, XMLoadFloat3A(&VolumetricLink->fractional_offset));

					XMVECTOR xmIndex(XMVectorMultiplyAdd(xmStreamOut, Volumetric::_xmTransformToIndexScale, Volumetric::_xmTransformToIndexBias));

					uint32_t color(0);
					bool seed_a_light(false);

					[[likely]] if (XMVector3GreaterOrEqual(xmIndex, XMVectorZero())
						&& XMVector3Less(xmIndex, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ)) // prevent crashes if index is negative or outside of bounds of visible mini-grid : voxel vertex shader depends on this clipping!
					{
						voxel = instance.OnVoxel(xmIndex, voxel, vxl);  // per voxel operations!

						if (voxel.Hidden)
							continue;

						color = voxel.getColor(); // (srgb 8bpc)
						seed_a_light = (voxel.Emissive & !Faded); // only on successful bounds check can an actual light be added safetly

						// update xmStreamOut if xmIndex is modified in instance.OnVoxel
						xmStreamOut = SFM::__fms(xmIndex, Volumetric::_xmInvTransformToIndexScale, _xmTransformToIndexBiasOverScale);
					}
					else {
						continue;
					}

					constexpr bool const faded = Faded;
					constexpr bool const emission_only = EmissionOnly; // so that compiler can know beforehand that this is specifically compile-time and the below 
					                                                   // if statement can combine with a non-constexpr (seed_a_light). The if statewment drops the "if constexpr" safetly here.
					                                                   // https://stackoverflow.com/questions/55492042/combining-if-constexpr-with-non-const-condition
					// finally submit voxel //
					if constexpr (!emission_only) {

						// Build hash //

						// ** see uniforms.vert for definition of constants used here **
						uint32_t hash(voxel.getAdjacency());                //           0000 0000 0011 1111
						hash |= (seed_a_light << 6);			            //           0000 0000 01xx xxxx    // no light, no emission
						hash |= (voxel.Metallic << 7);						// 0000 0000 0000 xxxx 1xxx xxxx
						hash |= (voxel.Roughness << 8);						// 0000 0000 0000 1111 xxxx xxxx

						uint32_t const index(vxl - vxl_offset);

						// transparency cannot be dynamically set, as the number of transparent voxels for the entire voxel model must be known and its already registered.
						if (!(faded | voxel.Transparent)) {
							
							if constexpr (Dynamic) {

								local.voxels.emplace_back(            
									voxels_dynamic, index,
									xmStreamOut,
									XMVectorSetW(xmVoxelOrient, Sign * (float)SFM::max(1u, color)),  // ensure color not equal to zero so packed sign is valid, srgb is passed to vertex shader which converts it to linear; which is faster than here with cpu
									hash
								);
								voxels_dynamic_bits.set_bit(index);
							}
							else {
								local.voxels.emplace_back(
									voxels_static, index,
									xmStreamOut,
									XMVectorSet(0.0f, 0.0f, 0.0f, (float)color),
									hash
								);
								voxels_static_bits.set_bit(index);
							}

						}
						else { // transparency enabled

							hash |= ((Transparency >> 6) << 13);				// 0000 0000 011U xxxx xxxx xxxx

							if constexpr (Dynamic) {

								local.voxels_trans.emplace_back(
									voxels_trans, index,
									xmStreamOut,
									XMVectorSetW(xmVoxelOrient, Sign * (float)SFM::max(1u, color)),  // ensure color not equal to zero so packed sign is valid, srgb is passed to vertex shader which converts it to linear; which is faster than here with cpu
									hash
								);

							}
							else { // sneaky override for *static* transparent voxels

								local.voxels_trans.emplace_back(
									voxels_trans, index,
									xmStreamOut,
									XMVectorSet(0.0f, 0.0f, 0.0f, (float)color),
									hash
								);

							}
							voxels_trans_bits.set_bit(index);
						}
					}

					if (seed_a_light) {										  // crash prevented at beginning of function

						// the *World position* of the light is stored, so it should be used with a corresponding *world* point in calculations
						// te lightmap volume however is sampled with the uv relative coordinates of a range between 0...VOXEL_MINIGRID_VISIBLE_X
						// and is in the fragment shaderrecieved swizzled in xzy form
						VolumetricLink->Opacity.getMappedVoxelLights().seed(xmIndex, color);
					}
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
					local_perf.iteration_duration = std::max(local_perf.iteration_duration, high_resolution_clock::now() - tStartIter);
#endif
				} // end for

				// ####################################################################################################################
				// ensure all batches are  output (residual/remainder)
				if constexpr (Dynamic) {
					local.voxels.out(voxels_dynamic);
				}
				else {
					local.voxels.out(voxels_static);
				}
				
				local.voxels_trans.out(voxels_trans);
				// ####################################################################################################################

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
				local_perf.operation_duration = high_resolution_clock::now() - tStartOp;
				local_perf.thread_id = tbb::this_tbb_thread::get_id();

#endif
			}
		} const RenderFuncBlockChunk;

		//##########################################################################################################################################//

		VertexDecl::VoxelNormal* pVoxelsOutStatic{};
		VertexDecl::VoxelDynamic* pVoxelsOutDynamic{};
		VertexDecl::VoxelDynamic* pVoxelsOutTrans{};

		uint32_t const vxl_count(instance.getCount());
		uint32_t const vxl_transparent_count(instance.getTransparentCount());

		// *bugfix
		// reserve enough memory in the direct buffer for direct addressing output
		// -- due to direct addressing all pointers must advance by the total count
		//    not the difference between the total count and transparent count as it was before
		//    so that the outputs into either buffer are at the correct locations in the direct buffer
		//    the locations are deterministic, always the same, and stream compaction is used later 
		//    to remove all "dead space" between voxels in the buffer.
		// require all light voxels to send to gpu. this is required for the linear light buffer (gpu only)
		
		if constexpr (!Faded) {

			if constexpr (Dynamic) {
				pVoxelsOutDynamic = dynamics.voxels.fetch_add(vxl_count);
			}
			else {
				pVoxelsOutStatic = statics.voxels.fetch_add(vxl_count);
			}
			if (0 != vxl_transparent_count) {
				pVoxelsOutTrans = trans.voxels.fetch_add(vxl_count);
			}
		}
		else { // faded (all transparent)
			pVoxelsOutTrans = trans.voxels.fetch_add(vxl_count);
		}
		
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		PerformanceType PerformanceCounters;
#endif
		uint32_t const vxl_offset(instance.getOffset());

		/*
		// serial
		RenderFuncBlockChunk(xmVoxelOrigin, xmVoxelOrient, vxl_offset,
							_Voxels,
							pVoxelsOutStatic, pVoxelsOutDynamic, pVoxelsOutTrans,
							std::forward<bit_row_reference<static_direct_buffer_size>&&>(bit_row_reference<static_direct_buffer_size>::create(*statics.bits, pVoxelsOutStatic - statics.voxels_start)),
							std::forward<bit_row_reference<dynamic_direct_buffer_size>&&>(bit_row_reference<dynamic_direct_buffer_size>::create(*dynamics.bits, pVoxelsOutDynamic - dynamics.voxels_start)),
							std::forward<bit_row_reference<dynamic_direct_buffer_size>&&>(bit_row_reference<dynamic_direct_buffer_size>::create(*trans.bits, pVoxelsOutTrans - trans.voxels_start)),
							instance
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
			, PerformanceCounters
#endif
		)(tbb::blocked_range<uint32_t>(vxl_offset, vxl_offset + vxl_count));
		*/

		// parallel
		tbb::this_task_arena::isolate([&] {

			tbb::parallel_for(tbb::blocked_range<uint32_t>(vxl_offset, vxl_offset + vxl_count, eThreadBatchGrainSize::MODEL),
			    std::forward<RenderFuncBlockChunk&&>(RenderFuncBlockChunk(xmVoxelOrigin, xmVoxelOrient, vxl_offset,
					_Voxels,
					pVoxelsOutStatic, pVoxelsOutDynamic, pVoxelsOutTrans,
					std::forward<bit_row_reference_atomic<static_direct_buffer_size>&&>(bit_row_reference_atomic<static_direct_buffer_size>::create(*statics.bits, pVoxelsOutStatic - statics.voxels_start)),
					std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(bit_row_reference_atomic<dynamic_direct_buffer_size>::create(*dynamics.bits, pVoxelsOutDynamic - dynamics.voxels_start)),
					std::forward<bit_row_reference_atomic<dynamic_direct_buffer_size>&&>(bit_row_reference_atomic<dynamic_direct_buffer_size>::create(*trans.bits, pVoxelsOutTrans - trans.voxels_start)),
					instance
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
					, PerformanceCounters
#endif
				)), part
			);

		});

#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
		PerformanceResult& result(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
		result += PerformanceCounters;	// add to global total this model instance performance counters
#endif
	}
#ifndef NDEBUG // revert optimizations - affects debug builds only
#pragma optimize( "", off )
#endif
} // end namespace voxB
} // end namespace Volumetric


#endif

