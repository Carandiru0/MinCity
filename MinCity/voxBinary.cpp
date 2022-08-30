/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

The VOX File format is Copyright to their respectful owners.

 */

#include "pch.h"
#include "globals.h"
#include "MinCity.h"
#define VOX_IMPL
#include "VoxBinary.h"
#include "IsoVoxel.h"
#include "voxelAlloc.h"
#include "eVoxelModels.h"

#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include <stdio.h> // C File I/O is 10x faster than C++ file stream I/O

#include <Utility/stringconv.h>

#include <density.h>	// https://github.com/centaurean/density - Density, fastest compression/decompression library out there with simple interface. must reproduce license file. attribution.

#define OPENVDB_USE_SSE42
#define OPENVDB_USE_AVX
#define OPENVDB_STATICLIB
#include <openvdb/openvdb.h>
#include <openvdb/io/Stream.h>

#ifdef VOX_DEBUG_ENABLED
#include <Utility/console_indicators/cursor_control.hpp> // https://github.com/p-ranav/indicators - Indicators for console applications. Progress bars! MIT license.
#include <Utility/console_indicators/progress_bar.hpp>
#endif

namespace { // anonymous, local to this file - automatically released at program close.
	static vector<mio::mmap_source>	_persistant_mmp;
}

/*
1x4      | char       | chunk id
4        | int        | num bytes of chunk content (N)
4        | int        | num bytes of children chunks (M)

N        |            | chunk content

M        |            | children chunks
*/
typedef struct ChunkHeader
{
	char				id[4];
	int32_t				numbytes;
	int32_t				numbyteschildren;
	
	ChunkHeader() 
	: id{'0','0','0','0'},
		numbytes(0), numbyteschildren(0)
	{}
		
} ChunkHeader;


/*
5. Chunk id 'SIZE' : model size
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | size x
4        | int        | size y
4        | int        | size z : gravity direction
-------------------------------------------------------------------------------
*/
typedef struct ChunkDimensions : ChunkHeader
{
	int32_t 		Width,
					Depth,
					Height;
	
	ChunkDimensions()
	: Width(0), Depth(0), Height(0)
	{}
		
} ChunkDimensions;

/*
6. Chunk id 'XYZI' : model voxels
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | numVoxels (N)
4 x N    | int        | (x, y, z, colorIndex) : 1 byte for each component
-------------------------------------------------------------------------------
*/
typedef struct VoxelData
{
	uint8_t			x, y, z, paletteIndex;
} VoxelData;

typedef struct ChunkVoxels : ChunkHeader
{
	int32_t 		numVoxels;
	
} ChunkVoxels;

/*
7. Chunk id 'RGBA' : palette
------------------------------------------------------------------------------ -
# Bytes | Type | Value
------------------------------------------------------------------------------ -
4 x 256 | int | (R, G, B, A) : 1 byte for each component
| *<NOTICE>
| *color[0 - 254] are mapped to palette index[1 - 255], e.g :
	|
	| for (int i = 0; i <= 254; i++) {
	| palette[i + 1] = ReadRGBA();
	|
}
------------------------------------------------------------------------------ -
*/
static constexpr uint32_t const PALETTE_SZ = 256;

constinit static inline uint32_t const default_palette[PALETTE_SZ] = {
	0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
	0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
	0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
	0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
	0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
	0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
	0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
	0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
	0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
	0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
	0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
	0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
	0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
	0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
	0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
	0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
};


namespace Volumetric
{
namespace voxB
{
STATIC_INLINE void ReadData( void* const __restrict DestStruct, uint8_t const* const __restrict pReadPointer, uint32_t const SizeOfDestStruct )
{
	memcpy_s(DestStruct, SizeOfDestStruct, pReadPointer, SizeOfDestStruct);
}

STATIC_INLINE bool const CompareTag( uint32_t const TagSz, uint8_t const * __restrict pReadPointer, char const* const& __restrict szTag )
{
	int32_t iDx = TagSz - 1;
	
	// push read pointer from beginning to end instead for start, makes life easier in loop
	pReadPointer = pReadPointer + iDx;
	do
	{
		
		if ( szTag[iDx] != *pReadPointer-- )
			return(false);
			
	} while (--iDx >= 0);
	
	return(true);
}

// [deprecated] simple (slow) linear search
// ** replaced w/ fast parallel adjacency search, old code left as reference below
//static bool const BuildAdjacency( uint32_t numVoxels, VoxelData const& __restrict source, VoxelData const* __restrict pVoxels, 
//								  uint8_t& __restrict Adjacency)
//{
//	static constexpr uint32_t const uiMaxOcclusion( 9 + 8 ); // 9 above, 8 sides
//	
//	uint8_t pendingAdjacency(0);
//	Adjacency = 0;
//	
//	uint32_t uiOcclusion(uiMaxOcclusion - 1);
//
//	// only remove voxels that are completely surrounded above and too the sides (don't care about below)
//	do
//	{
//		VoxelData const Compare( *pVoxels++ );
//		
//		int32_t const HeightDelta = ((int32_t)Compare.z - (int32_t)source.z);
//		
//		if ( (0 == HeightDelta) | (1 == HeightDelta) ) { // same height or 1 unit above only
//			
//			int32_t const SXDelta = (int32_t)Compare.x - (int32_t)source.x,
//						  SYDelta = (int32_t)Compare.y - (int32_t)source.y;
//			int32_t const XDelta = SFM::abs(SXDelta),
//						  YDelta = SFM::abs(SYDelta);
//			
//			if ( (XDelta | YDelta) <= 1 ) // within 1 unit or same on x,y axis'	
//			{
//				if ( 0 == (HeightDelta | XDelta | YDelta) ) // same voxel ?
//					continue;
//				
//					if ( 0 != HeightDelta ) { // 1 unit above...
//						
//						if ( 0 == (XDelta | YDelta) ) { // same x,y
//							pendingAdjacency |= BIT_ADJ_ABOVE;
//						}
//					}
//					
//					if ( 0 != YDelta ) {
//						if ( 0 == (HeightDelta | XDelta) ) { // same slice and x axis
//							if ( SYDelta < 0 ) { // 1 unit infront...
//								pendingAdjacency |= BIT_ADJ_FRONT; 
//							}
//							else /*if ( 1 == SYDelta )*/ { // 1 unit behind...
//								pendingAdjacency |= BIT_ADJ_BACK; 
//							}
//						}
//					}
//					
//					if ( 0 != XDelta ) {
//						if ( 0 == (HeightDelta | YDelta) ) { // same slice and y axis
//							if ( SXDelta < 0 ) { // 1 unit left...
//								pendingAdjacency |= BIT_ADJ_LEFT; 
//							}
//							else /*if ( 1 == SXDelta )*/ { // 1 unit right...
//								pendingAdjacency |= BIT_ADJ_RIGHT; 
//							}
//						}
//					}
//					--uiOcclusion; // Fully remove
//			}
//		}
//			
//	} while ( 0 != --numVoxels && 0 != uiOcclusion);
//	
//	Adjacency = pendingAdjacency;	// face culling used for voxels that are not removed
//
//	return(0 != uiOcclusion);
//}

// builds the voxel model, loading from magicavoxel .vox format, returning the model with the voxel traversal
// supporting 256x256x256 size voxel model.
static bool const LoadVOX(voxelModelBase* const __restrict pDestMem, uint8_t const* const& __restrict pSourceVoxBinaryData)
{
	uint8_t const* pReadPointer(nullptr);

	static constexpr uint32_t const  OFFSET_MAIN_CHUNK = 8;				// n bytes to structures
	static constexpr uint32_t const  TAG_LN = 4;
	static constexpr char const      TAG_VOX[TAG_LN] = { 'V', 'O', 'X', ' ' },
									 TAG_MAIN[TAG_LN] = { 'M', 'A', 'I', 'N' },
									 TAG_DIMENSIONS[TAG_LN] = { 'S', 'I', 'Z', 'E' },
									 TAG_XYZI[TAG_LN] = { 'X', 'Y', 'Z', 'I' },
									 TAG_PALETTE[TAG_LN] = { 'R', 'G', 'B', 'A' };

	static constexpr uint8_t const TAG_DUMMY = 0xFF;

	uint32_t numVoxels(0);

	VoxelData const* __restrict pVoxelRoot{};

	ChunkDimensions const sizeChunk;

	uint32_t const* __restrict pPaletteRoot{};

	pReadPointer = pSourceVoxBinaryData;

	if (CompareTag(_countof(TAG_VOX), pReadPointer, TAG_VOX)) {
		// skip version #

		// read MAIN Chunk
		ChunkHeader const rootChunk;

		pReadPointer += OFFSET_MAIN_CHUNK;
		ReadData((void* const __restrict) & rootChunk, pReadPointer, sizeof(rootChunk));

		if (CompareTag(_countof(TAG_MAIN), (uint8_t const* const)rootChunk.id, TAG_MAIN)) { // if we have a valid structure for root chunk

			// ChunkData
			if (rootChunk.numbyteschildren > 0) {

				pReadPointer += sizeof(ChunkHeader);		// move to expected "SIZE" chunk

				// read pointer is at expected "SIZE" chunk
				ReadData((void* const __restrict) & sizeChunk, pReadPointer, sizeof(sizeChunk));

				if (CompareTag(_countof(TAG_DIMENSIONS), (uint8_t const* const)sizeChunk.id, TAG_DIMENSIONS)) { // if we have a valid structure for size chunk

					ChunkVoxels voxelsChunk;
					pReadPointer += sizeof(ChunkDimensions);	// move to expected "XYZI" chunk
					ReadData((void* const __restrict) & voxelsChunk, pReadPointer, sizeof(voxelsChunk));

					if (CompareTag(_countof(TAG_XYZI), (uint8_t const* const)voxelsChunk.id, TAG_XYZI)) { // if we have a valid structure for xyzi chunk

						pReadPointer += sizeof(ChunkVoxels);	// move to expected voxel array data

						// Use Target Memory for loading
						if ((sizeChunk.Width <= Volumetric::MODEL_MAX_DIMENSION_XYZ)
							&& (sizeChunk.Height <= Volumetric::MODEL_MAX_DIMENSION_XYZ) // .vox .z is up direction in file format // vox files map height to "z"
							&& (sizeChunk.Depth <= Volumetric::MODEL_MAX_DIMENSION_XYZ)
							&& voxelsChunk.numVoxels > 0) { // ensure less than maximum dimension suported

							numVoxels = voxelsChunk.numVoxels;

							// input linear access array
							pVoxelRoot = reinterpret_cast<VoxelData const* const>(pReadPointer);

							//-------------------------------------------------------------------------------------------------------------------------//
							ChunkHeader paletteChunk;
							pReadPointer += sizeof(VoxelData) * voxelsChunk.numVoxels;	// move to expected "RGBA" chunk
							ReadData((void* const __restrict) & paletteChunk, pReadPointer, sizeof(paletteChunk));

							if (CompareTag(_countof(TAG_PALETTE), (uint8_t const* const)paletteChunk.id, TAG_PALETTE)) { // if we have a valid structure for xyzi chunk

								pReadPointer += sizeof(ChunkHeader);	// move to expected RGBA PALETTE array data

								pPaletteRoot = reinterpret_cast<uint32_t const* const>(pReadPointer);
							}
							else { // palette chunk missing, should be default palette then
								pPaletteRoot = reinterpret_cast<uint32_t const* const>(default_palette);
							}
						}
#ifdef VOX_DEBUG_ENABLED
						else {
							FMT_LOG_FAIL(VOX_LOG, "vox chunk dimensions too large");
							return(false);
						}
#endif
					}
#ifdef VOX_DEBUG_ENABLED
					else {
						FMT_LOG_FAIL(VOX_LOG, "expected XYZI chunk fail");
						return(false);
					}
#endif	
				}
#ifdef VOX_DEBUG_ENABLED
				else {
					FMT_LOG_FAIL(VOX_LOG, "expected SIZE chunk fail");
					return(false);
				}
#endif		
			}
#ifdef VOX_DEBUG_ENABLED
			else {
				FMT_LOG_FAIL(VOX_LOG, "root has no children chunk data fail");
				return(false);
			}
#endif
		}
#ifdef VOX_DEBUG_ENABLED
		else {
			FMT_LOG_FAIL(VOX_LOG, "no MAIN tag");
			return(false);
		}
#endif
	}
#ifdef VOX_DEBUG_ENABLED
	else {
		FMT_LOG_FAIL(VOX_LOG, "no VOX tag");
		return(false);
	}
#endif	

	vector<Volumetric::voxB::voxelDescPacked> allVoxels;
	allVoxels.reserve(numVoxels); allVoxels.resize(numVoxels);
	
	{ // adjacency
		using model_volume = Volumetric::voxB::model_volume;

		// Here: accurate counts, cull voxels & encode adjacency w/ consideration of transparency, optimize model.
		model_volume* __restrict bits(nullptr);
		bits = model_volume::create();

		{ // adjacency

			struct { // avoid lambda heap

				Volumetric::voxB::voxelDescPacked* const __restrict pVoxels;
				VoxelData const* const __restrict					pVoxelData;
				uint32_t const* const __restrict					pPaletteRoot;

			} p = { allVoxels.data(), pVoxelRoot, pPaletteRoot };

			tbb::parallel_for(uint32_t(0), numVoxels, [&p, &bits](uint32_t const i) {

				VoxelData const curVoxel(*(p.pVoxelData + i));

				uint32_t color(0);
				if (0 != curVoxel.paletteIndex) {
					// resolve color from palette using the palette index of this voxel
					color = p.pPaletteRoot[curVoxel.paletteIndex - 1] & 0x00FFFFFF; // no alpha
				}

				p.pVoxels[i] = std::move<voxelDescPacked&&>(voxelDescPacked(voxCoord(curVoxel.x, curVoxel.z, curVoxel.y), // *note -> swizzle of y and z here
					                                                        0, color));

				bits->set_bit(curVoxel.x, curVoxel.z, curVoxel.y); // *note -> swizzle of y and z here also required
			});

			// encode adjacency
			tbb::parallel_for(uint32_t(0), numVoxels, [&p, &bits](uint32_t const i) {
				uvec4_v const localIndex(p.pVoxels[i].x, p.pVoxels[i].y, p.pVoxels[i].z);

				p.pVoxels[i].setAdjacency(encode_adjacency(localIndex, bits)); // apply adjacency
			});
		}

		// cleanup, volume no longer required - adjacency is encoded
		if (bits) {
			model_volume::destroy(bits);
			bits = nullptr;
		}
	}

	// get bounds manually
	uvec4_v mini(Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ),
		    maxi(0, 0, 0);
	Volumetric::voxB::voxelDescPacked const* pVoxels(allVoxels.data());

	for (uint32_t i = 0; i < numVoxels; ++i) {

		__m128i const xmPosition(pVoxels->getPosition());
		mini.v = SFM::min(mini.v, xmPosition);
		maxi.v = SFM::max(maxi.v, xmPosition);
		
		++pVoxels;
	}
			
	// Actual dimensiuons of model saved, bugfix for "empty space around true model extents"
	uvec4_v xmDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(maxi.v, mini.v)));  // bugfix: minimum 1 voxel dimension size on any axis

	// reset for dimensions from model (stacked) .vox file size chunk
	maxi = uvec4_v(0);
	maxi.v = SFM::max(maxi.v, uvec4_v(sizeChunk.Width, sizeChunk.Height, sizeChunk.Depth).v);
	
	uvec4_v const xmVOXDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(maxi.v, _mm_set1_epi32(1))));  // here the file dimensions size is made index based rather than count based then: 
																											// bugfix: minimum 1 voxel dimension size on any axis
#ifdef VOX_DEBUG_ENABLED
	uvec4_t dimensions, vox_dimensions;
	xmDimensions.xyzw(dimensions);
	xmVOXDimensions.xyzw(vox_dimensions);
		
	if (dimensions.x != vox_dimensions.x ||
		dimensions.y != vox_dimensions.y ||
		dimensions.z != vox_dimensions.z) {

		FMT_LOG_WARN(VOX_LOG, "dimension mismatch calculated({:d}, {:d}, {:d}) != file({:d}, {:d}, {:d})",
			dimensions.x, dimensions.y, dimensions.z,
			vox_dimensions.x, vox_dimensions.y, vox_dimensions.z);

	}
#endif
	// take maximum of calculated and file dimensions
	xmDimensions.v = SFM::max(xmDimensions.v, xmVOXDimensions.v);

	// store final dimensions
	XMStoreFloat3A(&pDestMem->_maxDimensionsInv, XMVectorReciprocal(xmDimensions.v4f()));
	xmDimensions.xyzw(pDestMem->_maxDimensions);

	// Sort the voxels by y "slices", then z "rows", then x "columns"
	allVoxels.shrink_to_fit();
	tbb::parallel_sort(allVoxels.begin(), allVoxels.end());
		
	pDestMem->_numVoxels = (uint32_t)allVoxels.size();

	pDestMem->_Voxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * pDestMem->_numVoxels, CACHE_LINE_BYTES);
	memcpy((void* __restrict)pDestMem->_Voxels, allVoxels.data(), pDestMem->_numVoxels * sizeof(voxelDescPacked const));

	pDestMem->ComputeLocalAreaAndExtents(); // local area is xz dimensions only (no height), extents are based off local area calculation inside function - along with the spherical radius
	
#ifdef VOX_DEBUG_ENABLED	
	FMT_LOG(VOX_LOG, "vox loaded ({:d}, {:d}, {:d}) ", pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z);
#endif	
	
	return(true);
}

// see voxelModel.h
typedef struct voxelModelDescHeader
{
	char	 fileTypeTag[4];
	uint32_t numVoxels;
	uint8_t  dimensionX, dimensionY, dimensionZ;

	uint32_t numVoxelsEmissive, 
		     numVoxelsTransparent;

	uint8_t	features;

	// last
	uint32_t reserved = 0;

} voxelModelDescHeader;

namespace fs = std::filesystem;

static auto const OptimizeVoxels(Volumetric::voxB::voxelDescPacked* const pVoxels, uint32_t numVoxels)
{
	{ // redo adjacency, to take transparency into account

		using model_volume = Volumetric::voxB::model_volume;

		// Here: accurate counts, cull voxels & encode adjacency w/ consideration of transparency, optimize model.
		model_volume* __restrict bits(nullptr);
		bits = model_volume::create();

		{ // adjacency

			struct { // avoid lambda heap

				Volumetric::voxB::voxelDescPacked* const __restrict pVoxels;

			} p = { pVoxels };

			tbb::parallel_for(uint32_t(0), numVoxels, [&p, &bits](uint32_t const i) {

				p.pVoxels[i].Hidden = false; // always false here on save, ensure hidden is not set on any voxel b4 saving file

				// add to temp volume *only if not transparent - this excludes transparent voxels so that when adjacency is considered for neighbours of any voxel, it now leaves the transparent ones out of the adjacency calculation.
				if (!p.pVoxels[i].Transparent) {
					bits->set_bit(p.pVoxels[i].x, p.pVoxels[i].y, p.pVoxels[i].z);
				}
				});

			// encode adjacency
			tbb::parallel_for(uint32_t(0), numVoxels, [&p, &bits](uint32_t const i) {
				uvec4_v const localIndex(p.pVoxels[i].x, p.pVoxels[i].y, p.pVoxels[i].z);

				if (!p.pVoxels[i].Transparent) { // want to keep existing adjacency for transparent voxels, as they are not considered for adjacency re-calculation.
					p.pVoxels[i].setAdjacency(encode_adjacency(localIndex, bits)); // apply adjacency
				}
				});
		}

		// cleanup, volume no longer required - adjacency is encoded
		if (bits) {
			model_volume::destroy(bits);
			bits = nullptr;
		}
	}

	{ // culling

		// A VecVoxels has a separate std::vector<T> per thread
		typedef tbb::enumerable_thread_specific< vector<voxelDescPacked>, tbb::cache_aligned_allocator<vector<voxelDescPacked>>, tbb::ets_key_per_instance > VecVoxels;

		// output linear access array
		VecVoxels tmpVectors;

		struct { // avoid lambda heap

			Volumetric::voxB::voxelDescPacked const* const __restrict pVoxels;

			VecVoxels& __restrict tmpVectors;

		} p = { pVoxels, tmpVectors };

		tbb::parallel_for(uint32_t(0), numVoxels, [&p](uint32_t const i) {

			static constexpr uint32_t const all(BIT_ADJ_LEFT | BIT_ADJ_RIGHT | BIT_ADJ_FRONT | BIT_ADJ_BACK | BIT_ADJ_ABOVE | BIT_ADJ_BELOW);

			uint32_t const adjacency(p.pVoxels[i].getAdjacency());
			if (0 != adjacency && all != adjacency) {	// 0 - cull all voxels that are "alone" with nothing adjacent.
														// all - cull all voxels that have been completely surrounded by other voxels, which implies this voxel is hidden regardless of transparency or other things.

				// finally "interior faces" removal
				voxelDescPacked voxel(p.pVoxels[i]);
				XMVECTOR const xmVoxel(uvec4_v(voxel.getPosition()).v4f());
				XMVECTOR const xmOrigin(XMVectorZero()); // origin placed at bottom of model
				
				XMVECTOR const xmDir(XMVector3Normalize(XMVectorSubtract(xmOrigin, xmVoxel)));
				
				if (!(BIT_ADJ_BACK & adjacency)) {

					voxel.Back = XMVectorGetX(XMVector3Dot(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), -xmDir)) > 0.0f; // if facing the origin, hide face by setting it "adjacent"
				}
				if (!(BIT_ADJ_FRONT & adjacency)) {

					voxel.Front = XMVectorGetX(XMVector3Dot(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), xmDir)) > 0.0f; // if facing the origin, hide face by setting it "adjacent"
				}
				
				if (!(BIT_ADJ_RIGHT & adjacency)) {

					voxel.Right = XMVectorGetX(XMVector3Dot(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), xmDir)) > 0.0f; // if facing the origin, hide face by setting it "adjacent"
				}
				if (!(BIT_ADJ_LEFT & adjacency)) {

					voxel.Left = XMVectorGetX(XMVector3Dot(XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), -xmDir)) > 0.0f; // if facing the origin, hide face by setting it "adjacent"
				}
				
				p.tmpVectors.local().emplace_back(voxel);
			}
			});

		// reset count and tally number of voxels
		uint32_t numVoxelsEmissive(0),
				 numVoxelsTransparent(0);

		numVoxels = 0;
		
		Volumetric::voxB::voxelDescPacked* pVoxel(pVoxels);
		
		tbb::flattened2d<VecVoxels> flat_view = tbb::flatten2d(tmpVectors);
		for (tbb::flattened2d<VecVoxels>::const_iterator
			i = flat_view.begin(); i != flat_view.end(); ++i) {

			numVoxelsEmissive += i->Emissive;			// will have the culled counts afterwards
			numVoxelsTransparent += i->Transparent;

			*pVoxel = *i;
			++pVoxel;
			++numVoxels; // will have the culled count afterwards
		}

		tbb::parallel_sort(pVoxels, pVoxel);

		// accurate counts - now only the culled set of voxels.
		struct voxelCount {
			uint32_t const numVoxels,
						   numVoxelsEmissive,
						   numVoxelsTransparent;

			voxelCount(uint32_t const numVoxels_, uint32_t const numVoxelsEmissive_, uint32_t const numVoxelsTransparent_)
				: numVoxels(numVoxels_), numVoxelsEmissive(numVoxelsEmissive_), numVoxelsTransparent(numVoxelsTransparent_)
			{}
		};

		return( voxelCount{ numVoxels, numVoxelsEmissive, numVoxelsTransparent } ); // returns structured binding
	}
}

bool const SaveV1XCachedFile(std::wstring_view const path, voxelModelBase* const __restrict pDestMem)
{
	auto const [numVoxels, numVoxelsEmissive, numVoxelsTransparent] = OptimizeVoxels(const_cast<Volumetric::voxB::voxelDescPacked* const>(pDestMem->_Voxels), pDestMem->_numVoxels); // optimizing entire model
	
	// update model voxel counts after optimization //
	pDestMem->_numVoxels = numVoxels;
	pDestMem->_numVoxelsEmissive = numVoxelsEmissive;
	pDestMem->_numVoxelsTransparent = numVoxelsTransparent;

	// save to file
	FILE* __restrict stream(nullptr);
	if ((0 == _wfopen_s(&stream, path.data(), L"wbS")) && stream) {

		{
			voxelModelDescHeader const header{ { 'V', '1', 'X', ' ' }, pDestMem->_numVoxels,
																	(uint8_t)pDestMem->_maxDimensions.x,
																	(uint8_t)pDestMem->_maxDimensions.y,
																	(uint8_t)pDestMem->_maxDimensions.z,
																	pDestMem->_numVoxelsEmissive, pDestMem->_numVoxelsTransparent,
																	(uint8_t)((pDestMem->_Features.videoscreen ? voxelModelFeatures::VIDEOSCREEN : 0))};  // features appear in sequential order after this header in file if that feature exists.
			//write file header
			_fwrite_nolock(&header, sizeof(voxelModelDescHeader), 1, stream);

			//write features, if they exist
			if (voxelModelFeatures::VIDEOSCREEN == (header.features & voxelModelFeatures::VIDEOSCREEN)) {
				if (pDestMem->_Features.videoscreen) {
					_fwrite_nolock(pDestMem->_Features.videoscreen, sizeof(voxelScreen), 1, stream);
				}
			}
		}
		//write all voxelDescPacked 
		_fwrite_nolock(&pDestMem->_Voxels[0], sizeof(voxelDescPacked), pDestMem->_numVoxels, stream);

		_fclose_nolock(stream);

#ifdef VOX_DEBUG_ENABLED	
		FMT_LOG_OK(VOX_LOG, "v1x saved ({:d}, {:d}, {:d}) " " mem usage {:d} bytes", pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z, (pDestMem->_numVoxels * sizeof(voxelDescPacked)));
#endif
		return(true);
	}
	
	return(false);
}

bool const LoadV1XCachedFile(std::wstring_view const path, voxelModelBase* const __restrict pDestMem)
{
	std::error_code error{};

	mio::mmap_source mmap = mio::make_mmap_source(path, false, error);
	if (!error) {

		if (mmap.is_open() && mmap.is_mapped()) {
			___prefetch_vmem(mmap.data(), mmap.size());

			uint8_t const * pReadPointer((uint8_t*)mmap.data());

			// Check Header
			static constexpr uint32_t const  TAG_LN = 4;
			static constexpr char const      TAG_V1X[TAG_LN] = { 'V', '1', 'X', ' ' };

			if (CompareTag(_countof(TAG_V1X), pReadPointer, TAG_V1X)) {

				voxelModelDescHeader const headerChunk{};
				
				ReadData((void* const __restrict)&headerChunk, pReadPointer, sizeof(headerChunk));

				// Ensure valid
				if (0 != headerChunk.numVoxels && headerChunk.dimensionX < Volumetric::MODEL_MAX_DIMENSION_XYZ 
					&& headerChunk.dimensionY < Volumetric::MODEL_MAX_DIMENSION_XYZ 
					&& headerChunk.dimensionZ < Volumetric::MODEL_MAX_DIMENSION_XYZ)
				{
					pDestMem->_numVoxels = headerChunk.numVoxels;

					uvec4_v xmDimensions(headerChunk.dimensionX, headerChunk.dimensionY, headerChunk.dimensionZ);

					xmDimensions.xyzw(pDestMem->_maxDimensions);

					XMVECTOR const maxDimensions(xmDimensions.v4f());
					XMStoreFloat3A(&pDestMem->_maxDimensionsInv, XMVectorReciprocal(maxDimensions));

					pDestMem->_numVoxelsEmissive = headerChunk.numVoxelsEmissive;
					pDestMem->_numVoxelsTransparent = headerChunk.numVoxelsTransparent;

					// directly allocate voxel array for model
					pReadPointer += sizeof(headerChunk);		// move to expected features (if they exist) data chunk, or if they do not exist at all the voxel data chunk

					if (voxelModelFeatures::VIDEOSCREEN == (headerChunk.features & voxelModelFeatures::VIDEOSCREEN)) {

						if (nullptr == pDestMem->_Features.videoscreen) {
							voxelScreen readScreen;
							
							ReadData((void* const __restrict)&readScreen, pReadPointer, sizeof(readScreen));

							// validate the input before allocating memory (protection)
							if (readScreen.screen_rect.left < readScreen.screen_rect.right && readScreen.screen_rect.top < readScreen.screen_rect.bottom) { // simple but effective vallidation of data
								pDestMem->_Features.videoscreen = new voxelScreen(readScreen);
							}
							else {
								// silently fail back to the original .vox model
								return(false);
							}
							pReadPointer += sizeof(readScreen);		// move to expected voxel data chunk
						}
					}

					pDestMem->_Voxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * pDestMem->_numVoxels, CACHE_LINE_BYTES);
					memcpy_s((void* __restrict)pDestMem->_Voxels, pDestMem->_numVoxels * sizeof(voxelDescPacked const), pReadPointer, pDestMem->_numVoxels * sizeof(voxelDescPacked const));

					pDestMem->ComputeLocalAreaAndExtents();
#ifdef VOX_DEBUG_ENABLED	
					FMT_LOG(VOX_LOG, "vox loaded ({:d}, {:d}, {:d}) " " mem usage {:d} bytes, {:d} voxels total", (headerChunk.dimensionX), (headerChunk.dimensionY), (headerChunk.dimensionZ), (pDestMem->_numVoxels * sizeof(voxelDescPacked)), pDestMem->_numVoxels);
#endif		
					return(true);
				}

			}

			FMT_LOG_FAIL(VOX_LOG, "unable to parse cache file: {:s}", stringconv::ws2s(path));
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "unable to open or mmap cache file: {:s}", stringconv::ws2s(path));
		}
	}
	else {
		FMT_LOG_FAIL(VOX_LOG, "unable to open cache file: {:s}", stringconv::ws2s(path));
	}

	return(false);
}


// builds the voxel model, loading from magickavoxel .vox format, returning the model with the voxel traversal
// *** will save a cached version of culled model if it doesn't exist
// *** will load cached "culled" version if newer than matching .vox to speedify loading voxel models
int const LoadVOX(std::filesystem::path const path, voxelModelBase* const __restrict pDestMem)
{
	std::error_code error{};

	// original .vox model required regardless of whether the cached version exists. (supports modifications)
	
	std::wstring szOrigPathFilename(path.wstring().substr(0, path.wstring().find_last_of(L'/') + 1)); // isolate path to just the folder
	szOrigPathFilename += path.stem();
	szOrigPathFilename += VOX_FILE_EXT;

	if (!fs::exists(szOrigPathFilename)) {
		return(0);
	}

	// determine if cached version exists
	std::wstring szCachedPathFilename(VOX_CACHE_DIR);
	szCachedPathFilename += path.stem();
	szCachedPathFilename += V1X_FILE_EXT;

	if (fs::exists(szCachedPathFilename)) {
		// if .VOX file is not newer than cached .v1x file 
		auto const cachedmodifytime = fs::last_write_time(szCachedPathFilename);
		auto const voxmodifytime = fs::last_write_time(szOrigPathFilename);

		if (cachedmodifytime > voxmodifytime) {
			if (LoadV1XCachedFile(szCachedPathFilename, pDestMem)) {
				FMT_LOG_OK(VOX_LOG, " < {:s} > (cache) loaded", stringconv::ws2s(szCachedPathFilename));
				return(1); // returns existing cached model loaded
			}
			else {
				FMT_LOG_FAIL(VOX_LOG, "unable to load cached .V1X file: {:s}, reverting to reload .VOX ....", stringconv::ws2s(szCachedPathFilename));
			}
		}
		else
			FMT_LOG(VOX_LOG, " newer .VOX found, reverting to reload .VOX ....", stringconv::ws2s(szCachedPathFilename));

		// otherwise fallthrough and reload vox file
	}

	mio::mmap_source mmap{};
	mmap = mio::make_mmap_source(szOrigPathFilename, false, error);

	if (!error) {

		if (mmap.is_open() && mmap.is_mapped()) {
			___prefetch_vmem(mmap.data(), mmap.size());

			uint8_t const* __restrict mmap_data( (uint8_t const*)mmap.data() );

			if (LoadVOX(pDestMem, mmap_data)) {
				FMT_LOG_OK(VOX_LOG, " < {:s} > loaded", stringconv::ws2s(szOrigPathFilename));

				return(-1); // returns new model loaded
			}
			else {
				FMT_LOG_FAIL(VOX_LOG, "unable to parse .VOX file: {:s}", stringconv::ws2s(szOrigPathFilename));
			}
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "unable to open or mmap file: {:s}", stringconv::ws2s(szOrigPathFilename));
		}
	}
	else {

		FMT_LOG_FAIL(VOX_LOG, "unable to open file(s): {:s}", stringconv::ws2s(szOrigPathFilename));
	}

	return(0); // fail
}

typedef struct vdbFrameData
{
	static constexpr uint32_t const MAX_CHANNELS = 3;
	
	uint32_t				order;
	std::string				path;
	
	uint32_t				max_voxel_count[MAX_CHANNELS];
	
	vdbFrameData(std::string_view const path_, uint32_t const index_)
		: order(index_), path(path_), max_voxel_count{}
	{}
	
	vdbFrameData(vdbFrameData&&) = default;
	vdbFrameData& operator=(vdbFrameData&&) = default;

	bool const operator<(vdbFrameData const& rhs) const
	{
		return(order < rhs.order);
	}
private:
	vdbFrameData(vdbFrameData const&) = delete;
	vdbFrameData& operator=(vdbFrameData const&) = delete;

} vdbFrameData;

//void foo(int (&x)[100]);
static void PrepareVDBFrame(vdbFrameData& __restrict frame_data, float(&__restrict min_value)[vdbFrameData::MAX_CHANNELS], float(&__restrict max_value)[vdbFrameData::MAX_CHANNELS])
{	
	using GridType = openvdb::FloatGrid;
	using TreeType = typename GridType::TreeType;
	
	std::ifstream					file(frame_data.path, std::ios_base::in | std::ios_base::binary);
	openvdb::io::Stream	const		stream(file, false); // *bugfix - parameter false, disables "delayed" loading of voxel data. If delayed-loading is on, the performance tanks! speedup++
	openvdb::GridPtrVecPtr const	grids(stream.getGrids());
	
	file.close();
	
	uint32_t max_count[vdbFrameData::MAX_CHANNELS]{};
	
	uint32_t const grid_count(SFM::min(vdbFrameData::MAX_CHANNELS, (uint32_t)grids->size()));
	for (uint32_t grid_index = 0; grid_index < grid_count; ++grid_index) {

		GridType::Ptr const grid(openvdb::GridBase::grid<GridType>((*grids)[grid_index]));

		// pre-processing required to determine range of data values to normalize .vdb values
		for (GridType::ValueOnCIter iter = grid->cbeginValueOn(); iter; ++iter) {

			if (iter.isVoxelValue()) {

				float const value(iter.getValue());

				min_value[grid_index] = SFM::min(min_value[grid_index], value);
				max_value[grid_index] = SFM::max(max_value[grid_index], value);

				++max_count[grid_index];
			}
		}
	}
	
	memcpy(frame_data.max_voxel_count, max_count, sizeof(max_count));
}

static void LoadVDBFrame(vdbFrameData const& __restrict frame_data, voxelModelBase* const __restrict pDestMem, voxelSequence& __restrict sequence, float const(&__restrict min_value)[3], float const(&__restrict max_value)[3], __m128i& __restrict mini, __m128i& __restrict maxi)
{		
	uint32_t frameOffset(0),
			 numVoxels(0);

	{ // convert vdb to linear array of voxels
		
		vector<Volumetric::voxB::voxelDescPacked> allVoxels;
		
		{ // output linear access array

			using GridType = openvdb::FloatGrid;
			using TreeType = typename GridType::TreeType;

			using model_volume = Volumetric::voxB::model_volume;

			model_volume* __restrict bits(nullptr);
			bits = model_volume::create();
				
			vector<uint32_t> allIndices; // 3d lookup volume (temporary)
			allIndices.reserve(model_volume::width() * model_volume::height() * model_volume::depth());
			allIndices.resize(model_volume::width() * model_volume::height() * model_volume::depth());
			memset(&allIndices[0], 0, sizeof(uint32_t) * model_volume::width() * model_volume::height() * model_volume::depth()); // ensure memory is zeroed
			
			// speedup ++ reserve maximum count of voxels possible
			allVoxels.reserve(frame_data.max_voxel_count[0] + frame_data.max_voxel_count[1] + frame_data.max_voxel_count[2]);

			std::ifstream					file(frame_data.path, std::ios_base::in | std::ios_base::binary);
			openvdb::io::Stream	const		stream(file, false); // *bugfix - parameter false, disables "delayed" loading of voxel data. If delayed-loading is on, the performance tanks! speedup++
			openvdb::GridPtrVecPtr const	grids(stream.getGrids());
			
			file.close();
			
			uint32_t active_channel_offset(0);
			
			uint32_t const grid_count(SFM::min(vdbFrameData::MAX_CHANNELS, (uint32_t)grids->size()));
			for (uint32_t grid_index = 0; grid_index < grid_count; ++grid_index) { // file compatability of .vdb's exported from embergen. want to access "flames" first, then "density" and there packing into rgba channels instead.

				GridType::Ptr const grid(openvdb::GridBase::grid<GridType>((*grids)[grid_index]));
				
				sequence.addChannel(grid_index, grid->getName());
				
				float const grid_min_value(min_value[grid_index]),
							grid_max_value(max_value[grid_index]);

				for (GridType::ValueOnCIter iter = grid->cbeginValueOn(); iter; ++iter) {

					if (iter.isVoxelValue()) {

						float const value(SFM::saturate((iter.getValue() - grid_min_value) / (grid_max_value - grid_min_value))); // data value is *now* normalized to [0, 1]
						uint32_t const channel_value(SFM::saturate_to_u8(value * 255.0f)); // re-scale value to [0, 255]
							
						openvdb::Coord const vdbCoord(iter.getCoord()); // signed 32bit integer components (vdb coord)

						uvec4_t curVoxel; 
						SFM::saturate_to_u8(_mm_setr_epi32(vdbCoord.x(), vdbCoord.y(), vdbCoord.z(), 0), curVoxel); // makes sure that all coordinates are clamped to [0, 255], within the limits for model dimensions.

						// only add the voxel if it hasn't already been added
						size_t const index(model_volume::get_index(curVoxel.x, curVoxel.z, curVoxel.y));  // *note -> swizzle of y and z here required

						if (bits->read_bit(index)) {  // existing

							// slices ordered by Y: <---- USING Y
							// (y * xMax * zMax) + (z * xMax) + x;
								
							// add channel to existing voxel
							voxB::voxelDescPacked& __restrict voxel(allVoxels[allIndices[index]]);
							
							voxel.Color &= ~(0xff << active_channel_offset) & 0x00ffffff; // clear channel and clamp total possible value for the 24bits that Color can contain.
							voxel.Color |= (channel_value << active_channel_offset) & 0x00ffffff; // set channel,    ""  ""    ""     ""       ""             ""        ""         ""
						}
						else { // new ?
							bits->set_bit(index);

							// each color channel contains a value from [0, 255] representing a volume temperature, density, etc. voxel color is instead packed data.
							allIndices[index] = (uint32_t)allVoxels.size(); // potential lookup
							allVoxels.emplace_back(voxCoord(curVoxel.x, curVoxel.z, curVoxel.y), 0, (channel_value << active_channel_offset) & 0x00ffffff); // *note -> swizzle of y and z here required

							// bounding box calculation //
							__m128i const xmPosition(allVoxels.back().getPosition());
							mini = SFM::min(mini, xmPosition);
							maxi = SFM::max(maxi, xmPosition);
						}
					}
				}

				active_channel_offset += 8; // next channel (rgba)
			}

			numVoxels = (uint32_t)allVoxels.size(); // actual voxel count

			// cleanup, volume no longer required
			if (bits) {
				model_volume::destroy(bits);
				bits = nullptr;
			}
		}

		if (numVoxels) { // check
			// now have count of all active voxels for this frame
			frameOffset = pDestMem->_numVoxels; // existing count

			uint32_t const new_count(pDestMem->_numVoxels + numVoxels); // always growing so reallocation doesn't bother existing data //

			// new allocation of target size
			voxelDescPacked* pVoxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * new_count, CACHE_LINE_BYTES); // destination memory is aligned to 16 bytes to enhance performance on having voxels aligned to cache line boundaries.

			// copy over existing data
			memcpy(pVoxels, pDestMem->_Voxels, sizeof(voxelDescPacked) * frameOffset); // frameOffset contains the total size b4 this vdb frame.

			// copy over new data
			memcpy(pVoxels + frameOffset, allVoxels.data(), sizeof(voxelDescPacked) * numVoxels);

			// swap pointers
			voxelDescPacked* pDelVoxels(const_cast<voxelDescPacked*>(pDestMem->_Voxels)); // to be released
			pDestMem->_Voxels = pVoxels;

			// release the old allocation
			if (pDelVoxels) {
				scalable_aligned_free(pDelVoxels);
				pDelVoxels = nullptr;
			}
		}
	} // release of temporary memory happens at this end of scope.
	
	if (numVoxels) { // check
		auto const [numVoxelsFrame, numVoxelsEmissiveFrame, numVoxelsTransparentFrame] = OptimizeVoxels(const_cast<Volumetric::voxB::voxelDescPacked* const>(pDestMem->_Voxels + frameOffset), numVoxels); // optimizing *only* voxels for this frame

		if (numVoxelsFrame) { // check

			// update model voxel counts after optimization - counts returned are isolated to include this frame only, must append to total voxel count for all frames //
			pDestMem->_numVoxels += numVoxelsFrame;
			pDestMem->_numVoxelsEmissive += numVoxelsEmissiveFrame;
			pDestMem->_numVoxelsTransparent += numVoxelsTransparentFrame;

			// finally add frame metadatas
			sequence.addFrame(frameOffset, numVoxelsFrame);
		}
	}
}

// animation sequence (voxel frames), assumes that voxelModelBase is actually a sequence (no checking)
static bool const SaveV1XACachedFile(std::wstring_view const path, voxelModelBase* const __restrict pDestMem)
{
	// save to file
	FILE* __restrict stream(nullptr);
	if ((0 == _wfopen_s(&stream, path.data(), L"wbS")) && stream) {

		{
			voxelModelDescHeader const header{ { 'V', '1', 'X', 'A' }, pDestMem->_numVoxels,
																	(uint8_t)pDestMem->_maxDimensions.x,
																	(uint8_t)pDestMem->_maxDimensions.y,
																	(uint8_t)pDestMem->_maxDimensions.z,
																	pDestMem->_numVoxelsEmissive, pDestMem->_numVoxelsTransparent,
																	(uint8_t)voxelModelFeatures::SEQUENCE };  // features appear in sequential order after this header in file if that feature exists.
			//write file header
			_fwrite_nolock(&header, sizeof(voxelModelDescHeader), 1, stream);

			//write features
			voxelSequence const* const sequence(pDestMem->_Features.sequence);
			if (sequence) {

				uint32_t const numFrames(sequence->numFrames());

				_fwrite_nolock(&numFrames, sizeof(numFrames), 1, stream);

				for (uint32_t frame = 0; frame < numFrames; ++frame) {

					uint32_t const offset(sequence->getOffset(frame));
					uint32_t const numVoxels(sequence->numVoxels(frame));

					_fwrite_nolock(&offset, sizeof(offset), 1, stream);
					_fwrite_nolock(&numVoxels, sizeof(numVoxels), 1, stream);
				}
				
				// write channel names
				uint32_t numChannels(sequence->numChannels());
				_fwrite_nolock(&numChannels, sizeof(numChannels), 1, stream);
				
				for (uint32_t channel = 0; channel < numChannels; ++channel) {

					uint32_t const name_length((uint32_t)sequence->getChannelName(channel).length());
					_fwrite_nolock(&name_length, sizeof(name_length), 1, stream);
					_fwrite_nolock(sequence->getChannelName(channel).data(), sizeof(char), name_length, stream);
				}
			}

		}

		//write all voxelDescPacked 

		// compression
		// Determine safe buffer sizes
		size_t const data_size(sizeof(voxelDescPacked) * pDestMem->_numVoxels);
		size_t const compress_safe_size = density_compress_safe_size(data_size);

		uint8_t* __restrict outCompressed((uint8_t * __restrict)scalable_malloc(compress_safe_size));

		density_processing_result const result = density_compress((uint8_t* const __restrict)&pDestMem->_Voxels[0], data_size, outCompressed, compress_safe_size, DENSITY_ALGORITHM_CHEETAH);

		bool bReturn(true);

		if (!result.state) {
			_fwrite_nolock(&result.bytesWritten, sizeof(result.bytesWritten), 1, stream);
			_fwrite_nolock(&outCompressed[0], sizeof(outCompressed[0]), result.bytesWritten, stream);
		}
		else {

			FMT_LOG_FAIL(VOX_LOG, "unable to compress cache file: {:s}", stringconv::ws2s(path));
			bReturn = false;
		}

		_fclose_nolock(stream);

		if (outCompressed) {
			scalable_free(outCompressed);
			outCompressed = nullptr;
		}

#ifdef VOX_DEBUG_ENABLED
		if (bReturn) {
			FMT_LOG_OK(VOX_LOG, "{:s} saved ({:d}, {:d}, {:d}) " " mem usage {:d} bytes", stringconv::ws2s(path), pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z, (pDestMem->_numVoxels * sizeof(voxelDescPacked)));
		}
#endif
		return(bReturn);
	}

	return(false);
}

// animation sequence (voxel frames), loads voxelModelBase aa a sequence directly from a file
bool const LoadV1XACachedFile(std::wstring_view const path, voxelModelBase* const __restrict pDestMem)
{
	std::error_code error{};

	mio::mmap_source mmap = mio::make_mmap_source(path, false, error);
	if (!error) {

		if (mmap.is_open() && mmap.is_mapped()) {
			___prefetch_vmem(mmap.data(), mmap.size());

			uint8_t const* pReadPointer((uint8_t*)mmap.data());

			// Check Header
			static constexpr uint32_t const  TAG_LN = 4;
			static constexpr char const      TAG_V1XA[TAG_LN] = { 'V', '1', 'X', 'A' };

			if (CompareTag(_countof(TAG_V1XA), pReadPointer, TAG_V1XA)) {

				voxelModelDescHeader const headerChunk{};

				ReadData((void* const __restrict)&headerChunk, pReadPointer, sizeof(headerChunk));

				// Ensure valid
				if (0 != headerChunk.numVoxels && headerChunk.dimensionX < Volumetric::MODEL_MAX_DIMENSION_XYZ
					&& headerChunk.dimensionY < Volumetric::MODEL_MAX_DIMENSION_XYZ
					&& headerChunk.dimensionZ < Volumetric::MODEL_MAX_DIMENSION_XYZ)
				{
					pDestMem->_numVoxels = headerChunk.numVoxels;

					uvec4_v xmDimensions(headerChunk.dimensionX, headerChunk.dimensionY, headerChunk.dimensionZ);

					xmDimensions.xyzw(pDestMem->_maxDimensions);

					XMVECTOR const maxDimensions(xmDimensions.v4f());
					XMStoreFloat3A(&pDestMem->_maxDimensionsInv, XMVectorReciprocal(maxDimensions));

					pDestMem->_numVoxelsEmissive = headerChunk.numVoxelsEmissive;
					pDestMem->_numVoxelsTransparent = headerChunk.numVoxelsTransparent;

					// directly allocate voxel array for model
					pReadPointer += sizeof(headerChunk);		// move to expected features (if they exist) data chunk, or if they do not exist at all the voxel data chunk

					if (voxelModelFeatures::SEQUENCE == (headerChunk.features & voxelModelFeatures::SEQUENCE)) {

						if (nullptr == pDestMem->_Features.sequence) {

							uint32_t numFrames;
							ReadData((void* const __restrict) & numFrames, pReadPointer, sizeof(numFrames));
							pReadPointer += sizeof(numFrames);

							voxelSequence sequence;

							for (uint32_t frame = 0; frame < numFrames; ++frame) {

								uint32_t offset, numVoxels;

								ReadData((void* const __restrict) & offset, pReadPointer, sizeof(offset));
								pReadPointer += sizeof(offset);

								ReadData((void* const __restrict) & numVoxels, pReadPointer, sizeof(numVoxels));
								pReadPointer += sizeof(numVoxels);

								sequence.addFrame(offset, numVoxels);
							}

							// read channel names
							static constexpr uint32_t const MAX_BUFFER_SZ = 64;
							char szBuffer[MAX_BUFFER_SZ]{};
							uint32_t numChannels(0);

							ReadData((void* const __restrict) & numChannels, pReadPointer, sizeof(numChannels));
							pReadPointer += sizeof(numChannels);

							// validate 
							if (numChannels <= voxelSequence::MAX_CHANNELS) {
								
								for (uint32_t channel = 0; channel < numChannels; ++channel) {

									uint32_t channel_name_length(0);
									ReadData((void* const __restrict) & channel_name_length, pReadPointer, sizeof(channel_name_length));
									pReadPointer += sizeof(channel_name_length);

									if (channel_name_length <= MAX_BUFFER_SZ) { // validate
										ReadData((void* const __restrict) & szBuffer, pReadPointer, sizeof(char) * channel_name_length);
										pReadPointer += sizeof(char) * channel_name_length;
									}
									else {
										FMT_LOG_FAIL(VOX_LOG, "not a valid vox sequence cache file: {:s}", stringconv::ws2s(path));
										return(false);
									}
									
									sequence.addChannel(channel, szBuffer);
									memset(szBuffer, 0, sizeof(szBuffer)); // reset (provides null termination for each channel string automatically)
								}
							}
							else {
								FMT_LOG_FAIL(VOX_LOG, "not a valid vox sequence cache file: {:s}", stringconv::ws2s(path));
								return(false);
							}
							pDestMem->_Features.sequence = new voxelSequence(sequence);
							
							// @ expected voxel data chunk for all frames (contigous)
						}
					}
					else {
						FMT_LOG_FAIL(VOX_LOG, "not a valid vox sequence cache file: {:s}", stringconv::ws2s(path));
						return(false);
					}
					
					// decompression
					// Determine safe buffer sizes
					size_t compressed_size;
					ReadData((void* const __restrict)&compressed_size, pReadPointer, sizeof(compressed_size));
					pReadPointer += sizeof(compressed_size);
					
					size_t const decompressed_size(pDestMem->_numVoxels * sizeof(voxelDescPacked));
					size_t const decompress_safe_size = density_decompress_safe_size(decompressed_size);

					uint8_t* __restrict outDecompressed((uint8_t * __restrict)scalable_malloc(decompress_safe_size));

					density_processing_result const result = density_decompress((uint8_t* const __restrict)&pReadPointer[0], compressed_size, outDecompressed, decompress_safe_size);

					bool bReturn(true);
					
					if (!result.state) {

						pDestMem->_Voxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * pDestMem->_numVoxels, CACHE_LINE_BYTES);
						memcpy((void* __restrict)pDestMem->_Voxels, outDecompressed, decompressed_size);
					}
					else {

						FMT_LOG_FAIL(VOX_LOG, "unable to decompress cache file: {:s}", stringconv::ws2s(path));
						bReturn = false;
					}
					pDestMem->ComputeLocalAreaAndExtents();

					if (outDecompressed) {
						scalable_free(outDecompressed);
						outDecompressed = nullptr;
					}
					
					return(bReturn);
				}

			}

			FMT_LOG_FAIL(VOX_LOG, "unable to parse cache file: {:s}", stringconv::ws2s(path));
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "unable to open or mmap cache file: {:s}", stringconv::ws2s(path));
		}
	}
	else {
		FMT_LOG_FAIL(VOX_LOG, "unable to open cache file: {:s}", stringconv::ws2s(path));
	}

	return(false);
}

// builds the voxel model, loading from academysoftwarefoundation .vdb format, returning the model with the voxels loaded for a sequence folder.
int const LoadVDB(std::filesystem::path const path, voxelModelBase* const __restrict pDestMem)
{
	constinit static bool bOpenVDBInitialized(false);

	// original vdb file not required if shipped only with the .v1xa (cached version) (does not support modifications)

	// determine if cached version exists
	std::wstring szCachedPathFilename(VOX_CACHE_DIR);
	std::wstring szFolderName;
	{ // string handling

		szFolderName = path.wstring().substr(0, path.wstring().find_last_of(L'/')); // isolate path to just the folder name
		size_t const start(szFolderName.find_last_of(L'/') + 1);
		szFolderName = szFolderName.substr(start, szFolderName.length() - start); // isolate path to just the folder name
		szCachedPathFilename += szFolderName;
		szCachedPathFilename += V1XA_FILE_EXT;
	}

	if (fs::exists(szCachedPathFilename)) {
		// if .VDB file is not newer than cached .v1xa file 
		auto const cachedmodifytime = fs::last_write_time(szCachedPathFilename);
		auto const vdbmodifytime = fs::last_write_time(path.wstring());

		if (cachedmodifytime > vdbmodifytime) {
			if (LoadV1XACachedFile(szCachedPathFilename, pDestMem)) {

				// save uncompressed (loaded) voxel data (raw data) to temporary file in cached folder
				std::wstring szMemoryMappedPathFilename(cMinCity::getUserFolder());
				szMemoryMappedPathFilename += VIRTUAL_DIR;
				szMemoryMappedPathFilename += szFolderName;
				szMemoryMappedPathFilename += MMP_FILE_EXT;

				FILE * __restrict stream(nullptr);
				if ((stream = _wfsopen(szMemoryMappedPathFilename.c_str(), L"wbST", _SH_DENYWR))) {

					_fwrite_nolock(&pDestMem->_Voxels[0], sizeof(voxelDescPacked), pDestMem->_numVoxels, stream);
					_fflush_nolock(stream);
					_fclose_nolock(stream); // handover to mmio
					stream = nullptr;

					// memory map the temporary file, using the global vector that keeps track of the memory mapped files
					std::error_code error{};

					_persistant_mmp.emplace_back(std::forward<mio::mmap_source&&>(mio::make_mmap_source(szMemoryMappedPathFilename, true, error))); // temporary hidden readonly file is automatically deleted on destruction of memory mapped object in persistant map @ program close.

					if (!error) { // ideally everything still lives in the windows file cache. And it knows that the data does not actually need to be written to disk. Leveraging that and deletion of the virtual file is transferred to the memory mapped file handle ownership now.

						if (_persistant_mmp.back().is_open() && _persistant_mmp.back().is_mapped()) {

							// release the uncompressed voxel data in the model
							if (pDestMem->_Voxels) {
								scalable_aligned_free(const_cast<voxelDescPacked*>(pDestMem->_Voxels));
								pDestMem->_Voxels = nullptr;
							}

							// update "_Voxels" to point to beginning of memory mapped file
							pDestMem->_Voxels = (voxelDescPacked const* const __restrict)_persistant_mmp.back().data();
							pDestMem->_Mapped = true;

							// model voxel data is now read-only and is "virtual memory", so it saves physical memory by only keeping whats active from virtual memory.
							// all management of this memory is handled by the OS, and the memory mapped file handle is automatically deleted on program close.
						}
					}
				}

#ifdef VOX_DEBUG_ENABLED
				FMT_LOG(VOX_LOG, " < {:s} > [sequence] {:d} channels " " [{:s}]  [{:s}]  [{:s}]", stringconv::ws2s(szFolderName + V1XA_FILE_EXT), pDestMem->_Features.sequence->numChannels(), pDestMem->_Features.sequence->getChannelName(0), pDestMem->_Features.sequence->getChannelName(1), pDestMem->_Features.sequence->getChannelName(2));
#endif
				
				if (pDestMem->_Mapped) {
					FMT_LOG_OK(VOX_LOG, " < {:s} > [sequence] (cache) (virtual) loaded ({:d}, {:d}, {:d})", stringconv::ws2s(szFolderName + V1XA_FILE_EXT), pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z);
				}
				else {
					FMT_LOG_OK(VOX_LOG, " < {:s} > [sequence] (cache) loaded ({:d}, {:d}, {:d})", stringconv::ws2s(szFolderName + V1XA_FILE_EXT), pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z);
				}

				return(1); // indicating existing (cached) sequence loaded
			}
			else {
				FMT_LOG_FAIL(VOX_LOG, "unable to load cached .V1XA file: {:s}, reverting to reload .VDB ....", stringconv::ws2s(szCachedPathFilename));
			}
		}
		else
			FMT_LOG(VOX_LOG, " newer .VDB found, reverting to reload .VDB ....", stringconv::ws2s(szCachedPathFilename));

		// otherwise fallthrough and reload vdb file
	}

	// stop going any further if vdb folder does not exist, thus never loading/initializing the openvdb library in normal application usage. openvdb .vdb files are only required once (importing). after conversion to .v1xa, the .v1xa file is the only one required.
	if (!fs::exists(path)) {
		FMT_LOG_FAIL(VOX_LOG, "unable to open vdb file: {:s} does not exist", path.string());
		return(0);
	}

#ifdef VOX_DEBUG_ENABLED
	FMT_LOG(VOX_LOG, "{:s} [sequence] importing...", path.string());
#endif

	if (!bOpenVDBInitialized) {
		openvdb::initialize();
		bOpenVDBInitialized = true;
	}

	vector<vdbFrameData> ordered_frame_data;
	float min_value[3]{ 9999999.9f,  9999999.9f,  9999999.9f },		// for all frames the min/max value per grid
		  max_value[3]{ -9999999.9f, -9999999.9f, -9999999.9f };	// normalization of values considers all frames for each grid now

	{ // scope memory control

		// get sequence file/frame count & all file names
		vector<std::string> sequence_filenames;
		for (auto const& entry : fs::directory_iterator(path)) {	// path contains the sequence folder name in the named directory

			if (entry.exists() && !entry.is_directory()) {
				if (stringconv::case_insensitive_compare(VDB_FILE_EXT, entry.path().extension().wstring())) // only vdb files 
				{
					sequence_filenames.push_back(entry.path().string());
				}
			}
		}
		tbb::parallel_sort(sequence_filenames.begin(), sequence_filenames.end());  // directory_iterator does not guarantee order, so sort the filenames

#ifdef VOX_DEBUG_ENABLED		
		using namespace indicators;

		// Hide cursor
		show_console_cursor(false);

		indicators::ProgressBar bar{
		  option::BarWidth{50},
		  option::Start{" |"},
		  option::Fill{"-"},
		  option::Lead{"-"},
		  option::Remainder{"_"},
		  option::End{"|"},
		  option::PrefixText{"Preparing"},
		  option::ForegroundColor{Color::yellow},
		  option::ShowElapsedTime{true},
		  option::FontStyles{std::vector<FontStyle>{FontStyle::bold}},
		  option::MaxProgress{sequence_filenames.size()}
		};
#endif

		typedef tbb::enumerable_thread_specific< vector<vdbFrameData>, tbb::cache_aligned_allocator<vector<vdbFrameData>>, tbb::ets_key_per_instance > vec_vdbFrameData;

		vec_vdbFrameData frame_data;

		typedef struct minmax_value {

			float min_value[vdbFrameData::MAX_CHANNELS],
				  max_value[vdbFrameData::MAX_CHANNELS];

			minmax_value()
				: min_value{ 9999999.9f,  9999999.9f,  9999999.9f }, max_value{ -9999999.9f, -9999999.9f, -9999999.9f }
			{}
		} minmax_value;

		typedef tbb::enumerable_thread_specific< vector<minmax_value>, tbb::cache_aligned_allocator<vector<minmax_value>>, tbb::ets_key_per_instance > minmax_value_thread_specific;

		minmax_value_thread_specific minmax_values;

		// prepare streams
		tbb::parallel_for(uint32_t(0), uint32_t(sequence_filenames.size()), [&](uint32_t const i) {

			vector<vdbFrameData>& frame_data_local(frame_data.local());

			frame_data_local.emplace_back(sequence_filenames[i], i); // ctor creates stream (opens file, streams grids in one shot, closes file)

			vector<minmax_value>& value_local(minmax_values.local());

			value_local.emplace_back();

			PrepareVDBFrame(frame_data_local.back(), value_local.back().min_value, value_local.back().max_value);

#ifdef VOX_DEBUG_ENABLED
			bar.tick();
#endif
		});

		{ // flatten min/max values
			tbb::flattened2d<minmax_value_thread_specific> flat_view = tbb::flatten2d(minmax_values);
			for (tbb::flattened2d<minmax_value_thread_specific>::const_iterator
				i = flat_view.begin(); i != flat_view.end(); ++i) {

				for (uint32_t j = 0; j < vdbFrameData::MAX_CHANNELS; ++j) {

					min_value[j] = SFM::min(min_value[j], i->min_value[j]);
					max_value[j] = SFM::max(max_value[j], i->max_value[j]);
				}
			}
		}

		{ // flatten and sort frame data

			tbb::flattened2d<vec_vdbFrameData> flat_view = tbb::flatten2d(frame_data);
			for (tbb::flattened2d<vec_vdbFrameData>::iterator
				i = flat_view.begin(); i != flat_view.end(); ++i)
			{
				ordered_frame_data.emplace_back(std::forward<vdbFrameData&&>(*i));
			}

			tbb::parallel_sort(ordered_frame_data.begin(), ordered_frame_data.end());
		}
#ifdef VOX_DEBUG_ENABLED
		bar.mark_as_completed();
#endif
	} // scope memory control

#ifdef VOX_DEBUG_ENABLED
	using namespace indicators;

	indicators::ProgressBar bar2{
	  option::BarWidth{50},
	  option::Start{" |"},
	  option::Fill{"-"},
	  option::Lead{"-"},
	  option::Remainder{"_"},
	  option::End{"|"},
	  option::PrefixText{"Loading"},
	  option::ForegroundColor{Color::yellow},
	  option::ShowElapsedTime{true},
	  option::FontStyles{std::vector<FontStyle>{FontStyle::bold}},
	  option::MaxProgress{ordered_frame_data.size()}
	};
#endif

	// min/max bounds for every voxel in all frames and all grids. the absolute limits for the entire sequence.
	uvec4_v mini(Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ),
			maxi(0, 0, 0);
	
	voxelSequence sequence;
	
	for(auto const& frame : ordered_frame_data) 
	{
		if (frame.max_voxel_count[0] || frame.max_voxel_count[1] || frame.max_voxel_count[2]) {  // ensure frame is valid / prepared

			LoadVDBFrame(frame, pDestMem, sequence, min_value, max_value, mini.v, maxi.v);
		}
#ifdef VOX_DEBUG_ENABLED
		bar2.tick();
#endif
	}
	
#ifdef VOX_DEBUG_ENABLED
	bar2.mark_as_completed();
	// restore/show cursor
	show_console_cursor(true);
#endif
	
	// finalize and store sequence metadata
	pDestMem->_Features.sequence = new voxelSequence(sequence);
	
#ifdef VOX_DEBUG_ENABLED
	FMT_LOG(VOX_LOG, "{:d} channels " " [{:s}]  [{:s}]  [{:s}]", pDestMem->_Features.sequence->numChannels(), pDestMem->_Features.sequence->getChannelName(0), pDestMem->_Features.sequence->getChannelName(1), pDestMem->_Features.sequence->getChannelName(2));
#endif
	// final maximum dimensions calculation - always equals the maximum extents of the frame with the largest extents.
	
	// Actual dimensiuons of model saved, bugfix for "empty space around true model extents"
	uvec4_v xmDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(maxi.v, mini.v)));  // bugfix: minimum 1 voxel dimension size on any axis

	uvec4_v const xmVOXDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(maxi.v, _mm_set1_epi32(1))));  // here the file dimensions size is made index based rather than count based then: 
																											// bugfix: minimum 1 voxel dimension size on any axis
	// take maximum of calculated and file dimensions
	xmDimensions.v = SFM::max(xmDimensions.v, xmVOXDimensions.v);

	// store final dimensions
	XMStoreFloat3A(&pDestMem->_maxDimensionsInv, XMVectorReciprocal(xmDimensions.v4f()));
	xmDimensions.xyzw(pDestMem->_maxDimensions);
	
	pDestMem->ComputeLocalAreaAndExtents();
	
#ifdef VOX_DEBUG_ENABLED
	FMT_LOG_OK(VOX_LOG, "{:s} [sequence] imported ({:d}, {:d}, {:d})", path.string(), pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.y, pDestMem->_maxDimensions.z);
#endif
	
	// cache sequence to .v1xa file always
	SaveV1XACachedFile(szCachedPathFilename, pDestMem);
	
	return(-1); // indicating new sequence loaded
}

void voxelModelBase::ComputeLocalAreaAndExtents()
{
	// only care about x, z axis ... y axis / height is not part of local area 

	// +1 cause maxdimensions is equal to the max index, ie 0 - 31, instead of a size like 32
	point2D_t vRadii(p2D_half(p2D_half(point2D_t(_maxDimensions.x + 1, _maxDimensions.z + 1))));

	// determine if there is a remainder
	point2D_t const vRemainder(vRadii.x % MINIVOXEL_FACTOR, vRadii.y % MINIVOXEL_FACTOR);

	// radii factor for mini voxels
	vRadii = p2D_shiftr(vRadii, MINIVOXEL_FACTOR_BITS);

	// if remainder is odd add 1 at iterative end, otherwise split and add 2
	point2D_t vSideBegin{}, vSideEnd{};
	if (0 != vRemainder.x) {
		if (1 & vRemainder.x) {
			vSideEnd.x = 1;
		}
		else {
			vSideBegin.x = 1;
			vSideEnd.x = 1;
		}
	}
	if (0 != vRemainder.y) {
		if (1 & vRemainder.y) {
			vSideEnd.y = 1;
		}
		else {
			vSideBegin.y = 1;
			vSideEnd.y = 1;
		}
	}
	// now have area of effect for root voxel in voxel grid space
	_LocalArea = rect2D_t(p2D_negate(p2D_add(vRadii, vSideBegin)),
						  p2D_add(vRadii, vSideEnd));
	
	// Extents are 0.5f * (width/height/depth) as in origin at very center of model on all 3 axis
	XMVECTOR xmExtents = p2D_to_v2(p2D_sub(_LocalArea.right_bottom(), _LocalArea.left_top()));
	xmExtents = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmExtents); // move z to correct position from v2 to v3
	xmExtents = XMVectorSetY(xmExtents, float(_maxDimensions.y + 1));
	XMStoreFloat3A(&_Extents, XMVectorScale(xmExtents, 0.5f));
}

voxelModelBase::~voxelModelBase()
{
	if (!_Mapped) { // if memory mapped to a file, don't delete it as this memory is managed elsewhere
		if (_Voxels) {
			scalable_aligned_free(const_cast<voxelDescPacked * __restrict>(_Voxels));
		}
	}
	_Voxels = nullptr;
}

} // end namespace voxB
} // end namespace Volumetric


/*
MagicaVoxel .vox File Format [10/18/2016]

1. File Structure : RIFF style
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
1x4      | char       | id 'VOX ' : 'V' 'O' 'X' 'space', 'V' is first
4        | int        | version number : 150

Chunk 'MAIN'
{
    // pack of models
    Chunk 'PACK'    : optional

    // models
    Chunk 'SIZE'
    Chunk 'XYZI'

    Chunk 'SIZE'
    Chunk 'XYZI'

    ...

    Chunk 'SIZE'
    Chunk 'XYZI'

    // palette
    Chunk 'RGBA'    : optional

    // materials
    Chunk 'MATT'    : optional
    Chunk 'MATT'
    ...
    Chunk 'MATT'
}
-------------------------------------------------------------------------------


2. Chunk Structure
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
1x4      | char       | chunk id
4        | int        | num bytes of chunk content (N)
4        | int        | num bytes of children chunks (M)

N        |            | chunk content

M        |            | children chunks
-------------------------------------------------------------------------------


3. Chunk id 'MAIN' : the root chunk and parent chunk of all the other chunks


4. Chunk id 'PACK' : if it is absent, only one model in the file
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | numModels : num of SIZE and XYZI chunks
-------------------------------------------------------------------------------


5. Chunk id 'SIZE' : model size
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | size x
4        | int        | size y
4        | int        | size z : gravity direction
-------------------------------------------------------------------------------


6. Chunk id 'XYZI' : model voxels
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | numVoxels (N)
4 x N    | int        | (x, y, z, colorIndex) : 1 byte for each component
-------------------------------------------------------------------------------


7. Chunk id 'RGBA' : palette
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4 x 256  | int        | (R, G, B, A) : 1 byte for each component
                      | * <NOTICE>
                      | * color [0-254] are mapped to palette index [1-255], e.g : 
                      | 
                      | for ( int i = 0; i <= 254; i++ ) {
                      |     palette[i + 1] = ReadRGBA(); 
                      | }
-------------------------------------------------------------------------------


8. Default Palette : if chunk 'RGBA' is absent
-------------------------------------------------------------------------------
unsigned int default_palette[256] = {
	0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
	0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
	0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
	0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
	0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
	0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
	0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
	0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
	0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
	0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
	0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
	0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
	0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
	0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
	0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
	0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
};
-------------------------------------------------------------------------------


9. Chunk id 'MATT' : material, if it is absent, it is diffuse material
-------------------------------------------------------------------------------
# Bytes  | Type       | Value
-------------------------------------------------------------------------------
4        | int        | id [1-255]

4        | int        | material type
                      | 0 : diffuse
                      | 1 : metal
                      | 2 : glass
                      | 3 : emissive
 
4        | float      | material weight
                      | diffuse  : 1.0
                      | metal    : (0.0 - 1.0] : blend between metal and diffuse material
                      | glass    : (0.0 - 1.0] : blend between glass and diffuse material
                      | emissive : (0.0 - 1.0] : self-illuminated material

4        | int        | property bits : set if value is saved in next section
                      | bit(0) : Plastic
                      | bit(1) : Roughness
                      | bit(2) : Specular
                      | bit(3) : IOR
                      | bit(4) : Attenuation
                      | bit(5) : Power
                      | bit(6) : Glow
                      | bit(7) : isTotalPower (*no value)

4 * N    | float      | normalized property value : (0.0 - 1.0]
                      | * need to map to real range
                      | * Plastic material only accepts {0.0, 1.0} for this version
-------------------------------------------------------------------------------
											
											*/
											
											