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
#define VOX_IMPL
#include "VoxBinary.h"
#include "IsoVoxel.h"
#include "voxelAlloc.h"
#include "eVoxelModels.h"

#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include <stdio.h> // C File I/O is 10x faster than C++ file stream I/O

#include <Utility/stringconv.h>

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

// simple (slow) linear search
static bool const BuildAdjacency( uint32_t numVoxels, VoxelData const& __restrict source, VoxelData const* __restrict pVoxels, 
								  uint8_t& __restrict Adjacency)
{
	static constexpr uint32_t const uiMaxOcclusion( 9 + 8 ); // 9 above, 8 sides
	
	uint8_t pendingAdjacency(0);
	Adjacency = 0;
	
	uint32_t uiOcclusion(uiMaxOcclusion - 1);

	// only remove voxels that are completely surrounded above and too the sides (don't care about below)
	do
	{
		VoxelData const Compare( *pVoxels++ );
		
		int32_t const HeightDelta = ((int32_t)Compare.z - (int32_t)source.z);
		
		if ( (0 == HeightDelta) | (1 == HeightDelta) ) { // same height or 1 unit above only
			
			int32_t const SXDelta = (int32_t)Compare.x - (int32_t)source.x,
						  SYDelta = (int32_t)Compare.y - (int32_t)source.y;
			int32_t const XDelta = SFM::abs(SXDelta),
						  YDelta = SFM::abs(SYDelta);
			
			if ( (XDelta | YDelta) <= 1 ) // within 1 unit or same on x,y axis'	
			{
				if ( 0 == (HeightDelta | XDelta | YDelta) ) // same voxel ?
					continue;
				
					if ( 0 != HeightDelta ) { // 1 unit above...
						
						if ( 0 == (XDelta | YDelta) ) { // same x,y
							pendingAdjacency |= BIT_ADJ_ABOVE;
						}
					}
					
					if ( 0 != YDelta ) {
						if ( 0 == (HeightDelta | XDelta) ) { // same slice and x axis
							if ( SYDelta < 0 ) { // 1 unit infront...
								pendingAdjacency |= BIT_ADJ_FRONT; 
							}
							else /*if ( 1 == SYDelta )*/ { // 1 unit behind...
								pendingAdjacency |= BIT_ADJ_BACK; 
							}
						}
					}
					
					if ( 0 != XDelta ) {
						if ( 0 == (HeightDelta | YDelta) ) { // same slice and y axis
							if ( SXDelta < 0 ) { // 1 unit left...
								pendingAdjacency |= BIT_ADJ_LEFT; 
							}
							else /*if ( 1 == SXDelta )*/ { // 1 unit right...
								pendingAdjacency |= BIT_ADJ_RIGHT; 
							}
						}
					}
					--uiOcclusion; // Fully remove
			}
		}
			
	} while ( 0 != --numVoxels && 0 != uiOcclusion);
	
	Adjacency = pendingAdjacency;	// face culling used for voxels that are not removed

	return(0 != uiOcclusion);
}

// builds the voxel model, loading from magicavoxel .vox format, returning the model with the voxel traversal
// supporting 256x256x256 size voxel model.
static bool const LoadVOX( voxelModelBase* const __restrict pDestMem, uint8_t const * const (& __restrict pSourceVoxBinaryData)[2])
{
	uint8_t const * pReadPointer(nullptr);
	
	static constexpr uint32_t const  OFFSET_MAIN_CHUNK = 8;				// n bytes to structures
	static constexpr uint32_t const  TAG_LN = 4;
	static constexpr char const      TAG_VOX[TAG_LN] 				= { 'V', 'O', 'X', ' ' },
									 TAG_MAIN[TAG_LN] 				= { 'M', 'A', 'I', 'N' },
									 TAG_DIMENSIONS[TAG_LN] 		= { 'S', 'I', 'Z', 'E' },
									 TAG_XYZI[TAG_LN] 				= { 'X', 'Y', 'Z', 'I' },
									 TAG_PALETTE[TAG_LN]			= { 'R', 'G', 'B', 'A' };	

	static constexpr uint8_t const TAG_DUMMY = 0xFF;

	uint32_t const uiNumModels = (nullptr == pSourceVoxBinaryData[1]) ? 1 : 2;  // Only supportimg up to 2 models - the base and above "models". Default is 1 or the base model
	uint32_t uiNumModelsLoaded(0);
	uint32_t uiNumVoxelsPerStack[2]{ 0 };
	uint32_t uiTotalNumVoxels(0);

	VoxelData const* __restrict pVoxelRoot[2]{};

	ChunkDimensions const sizeChunk[2];

	uint32_t const* __restrict pPaletteRoot[2]{};

	for (uint32_t stack = 0; stack < uiNumModels; ++stack)
	{
		pReadPointer = pSourceVoxBinaryData[stack];

		if (CompareTag(_countof(TAG_VOX), pReadPointer, TAG_VOX)) {
			// skip version #

			// read MAIN Chunk
			ChunkHeader const rootChunk;

			pReadPointer += OFFSET_MAIN_CHUNK;
			ReadData((void* const __restrict) &rootChunk, pReadPointer, sizeof(rootChunk));

			if (CompareTag(_countof(TAG_MAIN), (uint8_t const* const)rootChunk.id, TAG_MAIN)) { // if we have a valid structure for root chunk

				// ChunkData
				if (rootChunk.numbyteschildren > 0) {

					pReadPointer += sizeof(ChunkHeader);		// move to expected "SIZE" chunk

					// read pointer is at expected "SIZE" chunk
					ReadData((void* const __restrict) &sizeChunk[stack], pReadPointer, sizeof(sizeChunk));

					if (CompareTag(_countof(TAG_DIMENSIONS), (uint8_t const* const)sizeChunk[stack].id, TAG_DIMENSIONS)) { // if we have a valid structure for size chunk

						ChunkVoxels voxelsChunk;
						pReadPointer += sizeof(ChunkDimensions);	// move to expected "XYZI" chunk
						ReadData((void* const __restrict) &voxelsChunk, pReadPointer, sizeof(voxelsChunk));

						if (CompareTag(_countof(TAG_XYZI), (uint8_t const* const)voxelsChunk.id, TAG_XYZI)) { // if we have a valid structure for xyzi chunk

							pReadPointer += sizeof(ChunkVoxels);	// move to expected voxel array data

							// Use Target Memory for loading
							if ((sizeChunk[stack].Width <= Volumetric::MODEL_MAX_DIMENSION_XYZ)
								&& (sizeChunk[stack].Height <= Volumetric::MODEL_MAX_DIMENSION_XYZ) // .vox .z is up direction in file format
								&& (sizeChunk[stack].Depth <= Volumetric::MODEL_MAX_DIMENSION_XYZ)
								&& voxelsChunk.numVoxels > 0) { // ensure less than maximum dimension suported

								uiNumVoxelsPerStack[stack] = voxelsChunk.numVoxels;

								uiTotalNumVoxels += uiNumVoxelsPerStack[stack];

								// input linear access array
								pVoxelRoot[stack] = reinterpret_cast<VoxelData const* const>(pReadPointer);

								//-------------------------------------------------------------------------------------------------------------------------//
								ChunkHeader paletteChunk;
								pReadPointer += sizeof(VoxelData) * uiNumVoxelsPerStack[stack];	// move to expected "RGBA" chunk
								ReadData((void* const __restrict) &paletteChunk, pReadPointer, sizeof(paletteChunk));

								if (CompareTag(_countof(TAG_PALETTE), (uint8_t const* const)paletteChunk.id, TAG_PALETTE)) { // if we have a valid structure for xyzi chunk
								
									pReadPointer += sizeof(ChunkHeader);	// move to expected RGBA PALETTE array data

									pPaletteRoot[stack] = reinterpret_cast<uint32_t const* const>(pReadPointer);
								}
								else { // palette chunk missing, should be default palette then
									pPaletteRoot[stack] = reinterpret_cast<uint32_t const* const>(default_palette);
								}

								++uiNumModelsLoaded;
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

	} // for

	if (uiNumModels == uiNumModelsLoaded)
	{
		vector<VoxelData> allVoxelData;
		allVoxelData.reserve(uiTotalNumVoxels); allVoxelData.resize(uiTotalNumVoxels);

		{
			VoxelData* __restrict pBuffer = allVoxelData.data();

			uint32_t remaining_count((uint32_t const)allVoxelData.size());

			// copy data into temporary memory buffer
			for (uint32_t stack = 0; stack < uiNumModels; ++stack)
			{
				uint32_t const current_count(uiNumVoxelsPerStack[stack]);

				memcpy_s(pBuffer, remaining_count * sizeof(VoxelData), pVoxelRoot[stack], current_count * sizeof(VoxelData));
				pBuffer += current_count;
				remaining_count -= current_count;
			}
		}

		// if stacked, must offset all height by base stack
		if (uiNumModels > 1) {
			VoxelData* __restrict pCurSecondStack(allVoxelData.data() + uiNumVoxelsPerStack[0]);
			uint32_t numVoxelsSecondStack(uiNumVoxelsPerStack[1]);

			while (0 != numVoxelsSecondStack) {

				pCurSecondStack->z += sizeChunk[0].Height;		// vox files map height to "z"

				++pCurSecondStack;
				--numVoxelsSecondStack;
			}
		}

		// A VecVoxels has a separate std::vector<T> per thread
		typedef tbb::enumerable_thread_specific< vector<voxelDescPacked> > VecVoxels;

		const struct FuncAdjacency {
			uint32_t const						numVoxels,
												numVoxelsFirstStack;
			VoxelData const* const __restrict	rootVoxel;
			VecVoxels& __restrict				tmpVectors;
			uint32_t const* const __restrict	pPaletteRoot[2];

			__inline FuncAdjacency(
				uint32_t const numVoxels_,
				uint32_t const _numVoxelsFirstStack, 
				VoxelData const* const __restrict rootVoxel_,
				VecVoxels& __restrict tmpVectors_,
				uint32_t const* const __restrict pPaletteRootFirstStack,
				uint32_t const* const __restrict pPaletteRootSecondStack)
				: numVoxels(numVoxels_), numVoxelsFirstStack(_numVoxelsFirstStack), rootVoxel(rootVoxel_), tmpVectors(tmpVectors_),
				pPaletteRoot{ pPaletteRootFirstStack, pPaletteRootSecondStack }
			{}

			void operator()(const tbb::blocked_range<uint32_t>& r) const {

				VecVoxels::reference v = tmpVectors.local();
				for (uint32_t i = r.begin(); i != r.end(); ++i) {

					uint8_t pendingAdjacency;
					VoxelData curVoxel(*(rootVoxel + i));

					if (BuildAdjacency(numVoxels, curVoxel, rootVoxel, pendingAdjacency)) {

						uint32_t color(0);
						if (0 != curVoxel.paletteIndex) {
							// resolve color from palette using the palette index of this voxel
							color = (i < numVoxelsFirstStack ? pPaletteRoot[0][curVoxel.paletteIndex - 1] : pPaletteRoot[1][curVoxel.paletteIndex - 1]) & 0x00FFFFFF; // no alpha
						}
						v.emplace_back(voxelDescPacked(voxCoord(curVoxel.x, curVoxel.z, curVoxel.y), // *note -> swizzle of y and z here
													   pendingAdjacency, color));
					}
				}
			}
		};
		// output linear access array
		VecVoxels tmpVectors;

		// load all voxels	// size = (numVoxels - voxelsCulled) * [sizeof(voxelDescPacked) (4bytes)]
		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, uiTotalNumVoxels), 
			FuncAdjacency(uiTotalNumVoxels, uiNumVoxelsPerStack[0], 
						  allVoxelData.data(), tmpVectors,
						  pPaletteRoot[0], pPaletteRoot[1]));

		// free temporary concatenated buffer
		allVoxelData.resize(0); allVoxelData.shrink_to_fit();

		vector<voxelDescPacked> culledVoxels;
		culledVoxels.reserve(uiTotalNumVoxels);

		// move from all voxels to culled voxels
		uvec4_v xmMin(UINT32_MAX, UINT32_MAX, UINT32_MAX,0),
			    xmMax(0);

		tbb::flattened2d<VecVoxels> flat_view = tbb::flatten2d(tmpVectors);
		for (tbb::flattened2d<VecVoxels>::const_iterator
			i = flat_view.begin(); i != flat_view.end(); ++i) {
			voxelDescPacked const voxel(*i);

			__m128i const xmPosition(voxel.getPosition());
			xmMin.v = SFM::min(xmMin.v, xmPosition);
			xmMax.v = SFM::max(xmMax.v, xmPosition);

			// add to culled voxels vector
			culledVoxels.emplace_back(voxel);
		}
		
		// Actual dimensiuons of model saved, bugfix for "empty space around true model extents"
		uvec4_v xmDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(xmMax.v, xmMin.v)));  // bugfix: minimum 1 voxel dimension size on any axis

		// reset for dimensions from model (stacked) .vox file size chunk
		xmMax = uvec4_v(0);

		for (uint32_t stack = 0; stack < uiNumModels; ++stack)
		{
			xmMax.v = SFM::max(xmMax.v, uvec4_v(sizeChunk[stack].Width, sizeChunk[stack].Height, sizeChunk[stack].Depth).v);
		}
		uvec4_v const xmVOXDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(xmMax.v, _mm_set1_epi32(1))));  // here the file dimensions size is made index based rather than count based then: 
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

		// Sort the voxels by "slices" on .y (height offset) axis
		culledVoxels.shrink_to_fit();
		tbb::parallel_sort(culledVoxels.begin(), culledVoxels.end());
		
		pDestMem->_numVoxels = (uint32_t)culledVoxels.size(); // save actual number of voxels - culled voxel total

		pDestMem->_Voxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * pDestMem->_numVoxels, alignof(voxelDescPacked)); // destination memory is aligned to 16 bytes to enhance performance on having voxels aligned to cache line boundaries.
		memcpy((void* __restrict)pDestMem->_Voxels, culledVoxels.data(), pDestMem->_numVoxels * sizeof(voxelDescPacked const));

		pDestMem->ComputeLocalAreaAndExtents(); // local area is xz dimensions only (no height), extents are based off local area calculation inside function - along with the spherical radius
#ifdef VOX_DEBUG_ENABLED	
		FMT_LOG(VOX_LOG, "vox loaded ({:d}, {:d}, {:d}) " " mem usage {:d} bytes, {:d} voxels culled", pDestMem->_maxDimensions.x, pDestMem->_maxDimensions.z, pDestMem->_maxDimensions.y, (pDestMem->_numVoxels * sizeof(voxelDescPacked)), (uiTotalNumVoxels - pDestMem->_numVoxels));
#endif		
		return(true);
	}
	else {
		FMT_LOG_FAIL(VOX_LOG, "unable to load a set of vox model(s)");
		return(false);
	}

	FMT_LOG_FAIL(VOX_LOG, "unknown error loading a set of vox model(s)");
	return(false);
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

bool const SaveV1XCachedFile(std::wstring_view const path, voxelModelBase const* const __restrict pDestMem)
{
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
		uint32_t const numVoxels(pDestMem->_numVoxels);
		voxelDescPacked const* __restrict pVoxels = pDestMem->_Voxels;

		_fwrite_nolock(&pVoxels[0], sizeof(voxelDescPacked), numVoxels, stream);

		_fclose_nolock(stream);

		return(true);
	}
	
	return(false);
}

bool const LoadV1XCachedFile(std::wstring_view const path, voxelModelBase* const __restrict pDestMem)
{
	std::error_code error{};

	mio::mmap_source mmap = mio::make_mmap_source(path, error);
	if (!error) {

		if (mmap.is_open() && mmap.is_mapped()) {
			__prefetch_vmem(mmap.data(), mmap.size());

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

					pDestMem->_Voxels = (voxelDescPacked* const __restrict)scalable_aligned_malloc(sizeof(voxelDescPacked) * pDestMem->_numVoxels, alignof(voxelDescPacked)); // destination memory is aligned to 16 bytes to enhance performance on having voxels aligned to cache line boundaries.
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

// @todo temporary - to be removed //
bool const AddEmissiveVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);
bool const AddVideoscreenVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);
bool const AddTransparentVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);
// @todo temporary - to be removed //


// builds the voxel model, loading from magickavoxel .vox format, returning the model with the voxel traversal
// *** will save a cached version of culled model if it doesn't exist
// *** will load cached "culled" version if newer than matching .vox to speedify loading voxel models
int const LoadVOX(std::filesystem::path const path, voxelModelBase* const __restrict pDestMem, bool const stacked)
{
	std::error_code error[2]{};

	std::wstring szOrigPathFilename(path.wstring().substr(0, path.wstring().find_last_of(L'/') + 1)); // isolate path to just the folder
	szOrigPathFilename += path.stem();
	if (stacked) {
		szOrigPathFilename += L"-0";
	}
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

	mio::mmap_source mmap[2]{};
	mmap[0] = mio::make_mmap_source(szOrigPathFilename, error[0]);

	if (stacked) {
		std::wstring szOrigPathFilenameStacked(path.wstring().substr(0, path.wstring().find_last_of(L'/') + 1));
		szOrigPathFilenameStacked += path.stem();
		szOrigPathFilenameStacked += L"-1";
		szOrigPathFilenameStacked += VOX_FILE_EXT;
		mmap[1] = mio::make_mmap_source(szOrigPathFilenameStacked, error[1]);
	}

	if (!error[0] && !error[1]) {

		if (mmap[0].is_open() && mmap[0].is_mapped()) {
			__prefetch_vmem(mmap[0].data(), mmap[0].size());

			uint8_t const* __restrict mmap_data[2]{ nullptr };
			mmap_data[0] = (uint8_t*)mmap[0].data();

			if (stacked) {
				if (!mmap[1].is_open() || !mmap[1].is_mapped()) {
					FMT_LOG_FAIL(VOX_LOG, "unable to open stacked .VOX file: {:s}", stringconv::ws2s(szOrigPathFilename + L"-1"));
					return(0);
				}

				mmap_data[1] = (uint8_t*)mmap[1].data();
				// loading in correct order requires swap so that top model is above base model
				std::swap(mmap_data[0], mmap_data[1]);
			}

			if (LoadVOX(pDestMem, mmap_data)) {
				FMT_LOG_OK(VOX_LOG, " < {:s} > loaded", stringconv::ws2s(szOrigPathFilename));

				/*
				// @todo temporary - to be removed //
				std::wstring file_no_extension(path.wstring().substr(0, path.wstring().find_last_of(L'/') + 1));
				file_no_extension += path.stem();

				AddEmissiveVOX(file_no_extension, pDestMem); // optional
				AddTransparentVOX(file_no_extension, pDestMem); // optional
				AddVideoscreenVOX(file_no_extension, pDestMem); // optional
				// @todo temporary - to be removed //

				if (SaveV1XCachedFile(szCachedPathFilename, pDestMem)) {
					FMT_LOG_OK(VOX_LOG, " < {:s} > cached", stringconv::ws2s(szCachedPathFilename));
				}
				else {
					FMT_LOG_FAIL(VOX_LOG, "unable to cache .VOX file to .V1X file: {:s}", stringconv::ws2s(szCachedPathFilename));
				}
				*/
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

static void ApplyEmissive(voxelModelBase const* const __restrict pEmissive, voxelModelBase* const __restrict pOwner)
{
	struct FuncMatch {
		voxelModelBase const* const __restrict Emissive;
		voxelModelBase* const __restrict	   Owner;
		tbb::atomic<uint32_t>& __restrict	   NumMatches;

		__inline FuncMatch(voxelModelBase const* const __restrict Emissive_, voxelModelBase* const __restrict Owner_, tbb::atomic<uint32_t>& __restrict NumMatches_)
			: Emissive(Emissive_), Owner(Owner_), NumMatches(NumMatches_)
		{}

		void operator()(const tbb::blocked_range<uint32_t>& r) const {
			
			for (uint32_t i = r.begin(); i != r.end(); ++i) {

				voxelDescPacked const* __restrict pCurVoxel(&Emissive->_Voxels[i]);

				uint32_t const x(pCurVoxel->x), y(pCurVoxel->y), z(pCurVoxel->z);

				voxelDescPacked const* __restrict pOwnerVoxel(Owner->_Voxels);
				uint32_t numVoxelsOwner(Owner->_numVoxels);
				do { // slow ass search but were sitting in multiple threads
					if (pOwnerVoxel->x == x && pOwnerVoxel->y == y && pOwnerVoxel->z == z) {

						voxelDescPacked* const __restrict State(Owner->_Voxels + (pOwnerVoxel - Owner->_Voxels));
						State->Emissive = true;
						NumMatches.fetch_and_increment<tbb::relaxed>();
						break;
					}

					++pOwnerVoxel;
				} while (--numVoxelsOwner);
				
			}
		}
	};

	{
		uint32_t const numVoxels(pEmissive->_numVoxels);
		tbb::atomic<uint32_t> numMatches = 0;
		FuncMatch const findMatches(pEmissive, pOwner, numMatches);
		// find matches and apply
		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, numVoxels), findMatches);

		pOwner->_numVoxelsEmissive = numMatches;
	}
}
bool const AddEmissiveVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked)
{
	voxelModelBase emissivePart(pOwner);  // only needs to be loaded and applied, all edata is contained in StateGroup of Owner Vox Model afterwards

	std::wstring szEmissiveIntensityFilename(file_no_extension); szEmissiveIntensityFilename += FILE_WILD_EMISSIVE;

	if (LoadVOX(szEmissiveIntensityFilename, &emissivePart, stacked) ) {

		ApplyEmissive(&emissivePart, pOwner);

		FMT_LOG_OK(VOX_LOG, " < {:s} > emissive parts loaded", stringconv::ws2s(file_no_extension));
		return(true);
	}

	return(false);
}


static bool const ApplyVideoscreen(voxelModelBase const* const __restrict pVideoscreen, voxelModelBase* const __restrict pOwner)
{
	// A VecVoxels has a separate std::vector<T> per thread
	typedef tbb::enumerable_thread_specific< vector<voxelDescPacked const*> > VecVoxels;

	struct FuncMatch {
		voxelModelBase const* const __restrict Videoscreen;
		voxelModelBase* const __restrict	   Owner;
		VecVoxels& __restrict				   tmpVectors;

		__inline FuncMatch(voxelModelBase const* const __restrict Videoscreen_, voxelModelBase* const __restrict Owner_, VecVoxels& __restrict tmpVectors_)
			: Videoscreen(Videoscreen_), Owner(Owner_), tmpVectors(tmpVectors_)
		{}

		void operator()(const tbb::blocked_range<uint32_t>& r) const {

			VecVoxels::reference v = tmpVectors.local();
			for (uint32_t i = r.begin(); i != r.end(); ++i) {

				voxelDescPacked const* __restrict pCurVoxel(&Videoscreen->_Voxels[i]);

				uint32_t const x(pCurVoxel->x), y(pCurVoxel->y), z(pCurVoxel->z);

				voxelDescPacked const* __restrict pOwnerVoxel(Owner->_Voxels);
				uint32_t numVoxelsOwner(Owner->_numVoxels);
				do { // slow ass search but were sitting in multiple threads
					if (pOwnerVoxel->x == x && pOwnerVoxel->y == y && pOwnerVoxel->z == z) {

						voxelDescPacked* const __restrict State(Owner->_Voxels + (pOwnerVoxel - Owner->_Voxels));
						State->Video = true;
						v.emplace_back(pOwnerVoxel); // reference to voxel for flattened min/max op
						break;
					}

					++pOwnerVoxel;
				} while (--numVoxelsOwner);

			}
		}
	};

	{
		uint32_t const numVoxels(pVideoscreen->_numVoxels);
		VecVoxels tmpVectors;
		FuncMatch const findMatches(pVideoscreen, pOwner, tmpVectors);
		// find matches and apply
		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, numVoxels), findMatches);

		uvec4_v 
			xmMin(99999),
			xmMax(0);

		tbb::flattened2d<VecVoxels> flat_view = tbb::flatten2d(tmpVectors);
		for (tbb::flattened2d<VecVoxels>::const_iterator
			i = flat_view.begin(); i != flat_view.end(); ++i) {
			voxelDescPacked const* const voxel(*i);

			__m128i const xmPosition(voxel->getPosition());
			xmMin.v = SFM::min(xmMin.v, xmPosition);
			xmMax.v = SFM::max(xmMax.v, xmPosition);
		}
		uvec4_t cube;
		uvec4_v(_mm_sub_epi32(xmMax.v, xmMin.v)).xyzw(cube);

		uvec4_t minimum, maximum;
		xmMin.xyzw(minimum);
		xmMax.xyzw(maximum);

		// only screens that are xz or zx orientation are acceptable, supports curved screen - if containing bbox dimensions symmetrical major axis defaults to X axis 
		// find axis with lowest "thickness", flag major axis for screen
		if (cube.x < cube.z) {
			rect2D_t const screen_rect = rect2D_t(minimum.z, minimum.y, maximum.z + 1, maximum.y + 1);
			pOwner->_Features.videoscreen = new voxelScreen(screen_rect, voxelScreen::MAJOR_AXIS_Z);
		}
		else {
			rect2D_t const screen_rect = rect2D_t(minimum.x, minimum.y, maximum.x + 1, maximum.y + 1);
			pOwner->_Features.videoscreen = new voxelScreen(screen_rect, voxelScreen::MAJOR_AXIS_X);
		}
	}

	return(true);
}
bool const AddVideoscreenVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked)
{
	voxelModelBase videoscreenPart(pOwner);  // only needs to be loaded and applied, all edata is contained in StateGroup of Owner Vox Model afterwards

	std::wstring szVideoscreenFilename(file_no_extension); szVideoscreenFilename += FILE_WILD_VIDEO;

	if (LoadVOX(szVideoscreenFilename, &videoscreenPart, stacked)) {

		if (ApplyVideoscreen(&videoscreenPart, pOwner)) {

			FMT_LOG_OK(VOX_LOG, " < {:s} > videoscreen part loaded", stringconv::ws2s(file_no_extension));
			return(true);
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "< {:s} > videoscreen is not one voxel thick or is top-down flat", stringconv::ws2s(file_no_extension));
		}
	}

	return(false);
}


void ApplyAllTransparent(voxelModelBase* const __restrict pModel)
{
	struct FuncTransparentAll {
		
		voxelModelBase* const __restrict	   Model;

		__inline FuncTransparentAll(voxelModelBase* const __restrict Model_)
			: Model(Model_)
		{}

		void operator()(const tbb::blocked_range<uint32_t>& r) const {

			for (uint32_t i = r.begin(); i != r.end(); ++i) {

				Model->_Voxels[i].Transparent = true;
				
			}
		}
	};

	{
		uint32_t const numVoxels(pModel->_numVoxels);
		FuncTransparentAll const applyAll(pModel);
		// apply to all voxels of model (transparecy capability)
		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, numVoxels), applyAll);
		// important to update this count!!!!
		pModel->_numVoxelsTransparent = numVoxels;
	}
}

static void ApplyTransparency(voxelModelBase const* const __restrict pTransparent, voxelModelBase* const __restrict pOwner)
{
	struct FuncMatch {
		voxelModelBase const* const __restrict Transparent;
		voxelModelBase* const __restrict	   Owner;
		tbb::atomic<uint32_t>& __restrict	   NumMatches;

		__inline FuncMatch(voxelModelBase const* const __restrict Transparent_, voxelModelBase* const __restrict Owner_, tbb::atomic<uint32_t>& __restrict NumMatches_)
			: Transparent(Transparent_), Owner(Owner_), NumMatches(NumMatches_)
		{}

		void operator()(const tbb::blocked_range<uint32_t>& r) const {

			for (uint32_t i = r.begin(); i != r.end(); ++i) {

				voxelDescPacked const* __restrict pCurVoxel(&Transparent->_Voxels[i]);

				uint32_t const x(pCurVoxel->x), y(pCurVoxel->y), z(pCurVoxel->z);

				voxelDescPacked const* __restrict pOwnerVoxel(Owner->_Voxels);
				uint32_t numVoxelsOwner(Owner->_numVoxels);
				do { // slow ass search but were sitting in multiple threads
					if (pOwnerVoxel->x == x && pOwnerVoxel->y == y && pOwnerVoxel->z == z) {

						voxelDescPacked* const __restrict State(Owner->_Voxels + (pOwnerVoxel - Owner->_Voxels));
						State->Transparent = true;
						NumMatches.fetch_and_increment<tbb::relaxed>();
						break;
					}

					++pOwnerVoxel;
				} while (--numVoxelsOwner);

			}
		}
	};

	{
		uint32_t const numVoxels(pTransparent->_numVoxels);
		tbb::atomic<uint32_t> numMatches = 0;
		FuncMatch const findMatches(pTransparent, pOwner, numMatches);
		// find matches and apply
		tbb::parallel_for(tbb::blocked_range<uint32_t>(0, numVoxels), findMatches);

		// // need actuasl matched count unlike emissives, transparents totsl count affects vertex buffer offset and has to be accurate
		// can't simply use numVoxels, could be larger due to culling (removal of all interior hidden voxels) or other possibilities where
		// the total count has changed - this is the current accurate count:
		pOwner->_numVoxelsTransparent = numMatches;
	}
}
bool const AddTransparentVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked)
{
	voxelModelBase transparentPart(pOwner);  // only needs to be loaded and applied, all edata is contained in StateGroup of Owner Vox Model afterwards

	std::wstring szTransparencyFilename(file_no_extension); szTransparencyFilename += FILE_WILD_TRANSPARENT;

	if (LoadVOX(szTransparencyFilename, &transparentPart, stacked)) {

		ApplyTransparency(&transparentPart, pOwner);

		FMT_LOG_OK(VOX_LOG, " < {:s} > transparent parts loaded", stringconv::ws2s(file_no_extension));
		return(true);
	}

	return(false);
}

void voxelModelBase::ComputeLocalAreaAndExtents()
{
	// only care about x, z axis ... y axis / height is not part of local area 

	// +1 cause maxdimensions is equal to the max index, ie 0 - 31, instead of a size like 32
	point2D_t vRadii(p2D_half(point2D_t(_maxDimensions.x + 1, _maxDimensions.z + 1)));

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
	if (_Voxels) {
		scalable_aligned_free(const_cast<voxelDescPacked* __restrict>(_Voxels)); _Voxels = nullptr;
	}
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
											
											