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
#include <Utility/class_helper.h>
#include <atomic>
#include <Math/point2D_t.h>
#include "IsoVoxel.h"

#define GRID_FILE_EXT L".grid"

// forward decl
namespace Iso
{
	class no_vtable Voxel;
} // end ns

class StreamingGrid : no_copy
{
public:
	static constexpr uint32_t const  THREAD_BATCH_SIZE = 16;
	static constexpr uint32_t const  CHUNK_VOXELS = 16;   // 16 is 1KB, 64 is 4KB ... uncompressed size (CHUNK_VOXELS * sizeof(Iso::Voxel))  [set 1st] 
public:
	Iso::Voxel* __restrict visible_grid() const;
	tbb::queuing_rw_mutex& access_lock() const;

	Iso::Voxel const __vectorcall getVoxel(point2D_t const voxelIndex) const;
	void __vectorcall setVoxel(point2D_t const voxelIndex, Iso::Voxel const&& oVoxel);

	bool const Initialize();
	
	void CacheVisible(point2D_t const voxelBegin, point2D_t const voxelEnd);
	
	void GarbageCollect(); // see notes in cpp for proper usage
	
	void Reset();
	void Flush();

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
	void OutputDebugStats(fp_seconds const& tDelta);
#endif
#endif

private:
	bool const OpenStreamingGridFile();
	void CloseStreamingGridFile();

private:
	HANDLE            _filePersistantHandle;

	rect2D_t          _visibleGridBounds;
	point2D_t         _visibleGridBegin;
public:
	StreamingGrid();
	~StreamingGrid();
};