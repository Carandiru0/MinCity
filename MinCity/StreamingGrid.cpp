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
#include "StreamingGrid.h"
#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include "MinCity.h"
#include "IsoVoxel.h"
#include <Math/superfastmath.h>
#include <Utility/async_long_task.h>
#include <winioctl.h>
#include <density.h>	// https://github.com/centaurean/density - Density, fastest compression/decompression library out there with simple interface. must reproduce license file. attribution.

#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

#ifdef DEBUG_OUTPUT_STREAMING_STATS

#include "cNuklear.h"
#include "MinCity.h"

#endif

namespace // private to this file (anonymous)
{
	static inline tbb::queuing_rw_mutex grid_lock; // for the::grid

	static constexpr int8_t const
		CLOSED = 0,
		OPEN = 1;

	typedef struct no_vtable Chunk { // ordering - most frequently accessed - not size

		// temporal (time) //
		std::atomic_flag       _state; 
		tbb::spin_rw_mutex     _lock;
		std::atomic<tTime>     _last_access; // does not need to be atomic. Garbage Collection is the only other place this is accessed. Garbage Collection is run at a different time during the frame that does not overlap the normal chunk / grid usage - the "dead zone"

		// space //
		uint8_t*               _data; // data is compressed if CLOSED, or, data is decompressed if OPEN
		uint16_t               _compressed_size;
		
		Iso::Voxel const open(uint32_t const index);
		void update(uint32_t const index, Iso::Voxel const&& oVoxel);
		void close();

	} Chunk;

	static inline thread_local alignas(64) struct {

		static constexpr uint32_t const
			DECOMPRESS_SAFE_BUFFER_SIZE = 4416;  // 4400 is a safe decompression size for density for 4096 bytes below in chunk

		struct {
			uint8_t    buffer[DECOMPRESS_SAFE_BUFFER_SIZE]{};          // 4400 is a safe decompression size for density for 4096 bytes above in chunk
		} safe;

	} thread_local_decompress_chunks;
	
	static inline thread_local alignas(64) struct {

		static constexpr uint32_t const
			COMPRESS_SAFE_BUFFER_SIZE = 5344;    // 5344 ""    ""  compression    ""         ""        ""    ""    ""      ""

		struct {
			uint8_t    buffer[COMPRESS_SAFE_BUFFER_SIZE]{};          // 5344 is a safe compression size for density for 4096 bytes above in chunk
		} safe;

	} thread_local_compress_chunks;

	static inline constinit struct WorldGrid
	{
		static constexpr uint32_t const CHUNK_VOXELS = StreamingGrid::CHUNK_VOXELS; // Must always be power of 2
		static constexpr uint32_t const CHUNK_BITS = 6; // must be CHUNK_VOXELS = (1 << CHUNK_BITS) (manually set to match)
		static constexpr uint32_t const CHUNK_COUNT = (Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE) / CHUNK_VOXELS;
		
		Chunk* __restrict            _chunks = nullptr;

		density_context*             _context = nullptr; // re-usuable context for decompression/compression 
		size_t                       _common_header_size = 0;

		__declspec(safebuffers) __forceinline operator Chunk* const __restrict() const {
			return(_chunks);
		}

		STATIC_INLINE_PURE uint32_t const voxelToChunkIndex(uint32_t const offset) {
			// v1D = offset = 1D index from 2D voxel index
			// chunk1D = floor(v1D / chunkSize)
			return(offset >> CHUNK_BITS);
		}

		__declspec(safebuffers) __forceinline Chunk* const __restrict const voxelToChunk(uint32_t const offset) const {
			// v1D = offset = 1D index from 2D voxel index
			// chunk1D = floor(v1D / chunkSize)
			return(&_chunks[offset >> CHUNK_BITS]);
		}

	} world_grid{};

	Iso::Voxel const Chunk::open(uint32_t const index)  // used by getVoxel() of StreamingGrid
	{
		static constexpr size_t const CHUNK_VOXEL_SIZE = WorldGrid::CHUNK_VOXELS * sizeof(Iso::Voxel);

		tbb::spin_rw_mutex::scoped_lock lock(_lock, false); // read-only access //

		if (!_state.test_and_set()) { // CLOSED = false/clear

			/**/ // OPEN = set // /**/
			lock.upgrade_to_writer(); // write access

			[[unlikely]] if (nullptr == _data) { // treat as OPEN & skip decompression
				_data = (uint8_t* const __restrict)scalable_malloc(CHUNK_VOXEL_SIZE);
				memset(_data, 0, CHUNK_VOXEL_SIZE);
			}
			else {
				density_processing_result const result = density_decompress_with_context(_data + ::world_grid._common_header_size, // _data is compressed, skip over header
																						 _compressed_size,
																						 thread_local_decompress_chunks.safe.buffer, thread_local_decompress_chunks.DECOMPRESS_SAFE_BUFFER_SIZE,
																						 ::world_grid._context);
				if (!result.state) {

					_data = (uint8_t* const __restrict)scalable_realloc(_data, CHUNK_VOXEL_SIZE); // _data becomes decompressed

					// copy out to chunk local cache //
					memcpy(_data, thread_local_decompress_chunks.safe.buffer, CHUNK_VOXEL_SIZE);
				}
			}
		}

		lock.downgrade_to_reader(); // read access
		
		_last_access = critical_now(); // atomic

		Iso::Voxel const* const decompressed(reinterpret_cast<Iso::Voxel const* const>(_data));

		return(decompressed[index]);
	}

	void Chunk::update(uint32_t const index, Iso::Voxel const&& oVoxel) // used by setVoxel() of StreamingGrid
	{
		static constexpr size_t const CHUNK_VOXEL_SIZE = WorldGrid::CHUNK_VOXELS * sizeof(Iso::Voxel);

		_state.test_and_set(); /**/ // OPEN = set // /**/
		{
			tbb::spin_rw_mutex::scoped_lock lock(_lock, true); // write-only access //

			if (nullptr == _data) { // treat as OPEN
				_data = (uint8_t* const __restrict)scalable_malloc(CHUNK_VOXEL_SIZE);
				memset(_data, 0, CHUNK_VOXEL_SIZE);
			}

			Iso::Voxel* const decompressed(reinterpret_cast<Iso::Voxel* const>(_data));
			decompressed[index] = std::move(oVoxel);
		}
		_last_access = critical_now();
	}

	void Chunk::close() // used by GarbageCollection() of StreamingGrid
	{
		static constexpr size_t const CHUNK_VOXEL_SIZE = WorldGrid::CHUNK_VOXELS * sizeof(Iso::Voxel);

		if (_state.test()) { // OPEN = true/set

			/**/ // CLOSED = clear // /**/
			_state.clear(); /**/

			tbb::spin_rw_mutex::scoped_lock lock(_lock, false); // read-only access //

			density_processing_result const result = density_compress_with_context(_data, // _data is decompressed
				                                                                   CHUNK_VOXEL_SIZE,
				                                                                   thread_local_compress_chunks.safe.buffer, thread_local_compress_chunks.COMPRESS_SAFE_BUFFER_SIZE,
				                                                                   ::world_grid._context);

			if (!result.state) {

				size_t const new_compressed_size(result.bytesWritten);

				lock.upgrade_to_writer(); // write access //

				_data = (uint8_t* const __restrict)scalable_realloc(_data, new_compressed_size); // _data becomes compressed

				memcpy(_data, thread_local_compress_chunks.safe.buffer, new_compressed_size);

				_compressed_size = (uint16_t)new_compressed_size; // small chunk size < UINT16_MAX
			}
		}
	}
} // end ns

StreamingGrid::StreamingGrid()
{
}

bool const StreamingGrid::Initialize()
{
	bool bReturn(true);

	// create world grid memory
	{
		::world_grid._chunks = (Chunk* const __restrict)scalable_aligned_malloc(sizeof(Chunk) * WorldGrid::CHUNK_COUNT, CACHE_LINE_BYTES);

		memset(::world_grid._chunks, 0, sizeof(Chunk) * WorldGrid::CHUNK_COUNT);

		FMT_LOG(VOX_LOG, "world chunk allocation: {:n} bytes", (sizeof(Chunk) * WorldGrid::CHUNK_COUNT));

		// Setup context
		::world_grid._context = density_alloc_context(DENSITY_ALGORITHM_CHAMELEON, false, scalable_malloc); //*bugfix - CHEETAH and LION are bugged "input buffer too small" error. Only CHAMELEON works properly it seems...
		::world_grid._common_header_size = density_header_size();

		// Determine safe buffer sizes ~4400 bytes
		static constexpr size_t const voxel_chunk_size(sizeof(Iso::Voxel) * CHUNK_VOXELS);
		 
		size_t const decompress_safe_size = density_decompress_safe_size(voxel_chunk_size);
		size_t const compressed_safe_size = density_compress_safe_size(voxel_chunk_size);

		FMT_LOG(VOX_LOG, "chunk size: {:n} bytes / chunk decompress safe size: {:n} bytes / chunk compress safe size: {:n} bytes", voxel_chunk_size, decompress_safe_size, compressed_safe_size);
		
		if (decompress_safe_size > thread_local_decompress_chunks.DECOMPRESS_SAFE_BUFFER_SIZE) {

			FMT_LOG_FAIL(VOX_LOG, "in-adequate chunk decompress safe size: {:n}/{:n} bytes", decompress_safe_size, thread_local_decompress_chunks.DECOMPRESS_SAFE_BUFFER_SIZE);
			bReturn = false;
		}
		if (compressed_safe_size > thread_local_compress_chunks.COMPRESS_SAFE_BUFFER_SIZE) {

			FMT_LOG_FAIL(VOX_LOG, "in-adequate chunk compress safe size: {:n}/{:n} bytes", compressed_safe_size, thread_local_compress_chunks.COMPRESS_SAFE_BUFFER_SIZE);
			bReturn = false;
		}
	}

	return(bReturn);
}

tbb::queuing_rw_mutex& StreamingGrid::access_lock() const
{
	return(::grid_lock);
}

Iso::Voxel const __vectorcall StreamingGrid::getVoxel(point2D_t const voxelIndex) const
{
	uint32_t const offset(voxelIndex.y * Iso::WORLD_GRID_SIZE + voxelIndex.x);

	Chunk* const __restrict chunk = ::world_grid.voxelToChunk(offset);

	// open chunk
	return(chunk->open(offset & (StreamingGrid::CHUNK_VOXELS - 1)));
}

void __vectorcall StreamingGrid::setVoxel(point2D_t const voxelIndex, Iso::Voxel const&& oVoxel)
{
	uint32_t const offset(voxelIndex.y * Iso::WORLD_GRID_SIZE + voxelIndex.x);

	Chunk* const __restrict chunk = ::world_grid.voxelToChunk(offset);

	chunk->update(offset & (StreamingGrid::CHUNK_VOXELS - 1), std::forward<Iso::Voxel const&&>(oVoxel));
}

#ifdef DEBUG_OUTPUT_STREAMING_STATS
namespace {

	uint64_t
		_chunksOpen{},
		_chunksClosed{},
		_bytesOpen{},
		_bytesClosed{};

} // end ns
#endif

void StreamingGrid::Flush() // closes all chunks to "flush" any chunks that are open/
{
	tbb::queuing_rw_mutex::scoped_lock lock(::grid_lock, true);

	tbb::parallel_for(uint32_t(0), uint32_t(WorldGrid::CHUNK_COUNT), [&](uint32_t const i) {

		world_grid._chunks[i].close(); // close the chunk

	});

#ifdef DEBUG_OUTPUT_STREAMING_STATS
	_chunksOpen = 0;
	_chunksClosed = WorldGrid::CHUNK_COUNT;
	_bytesOpen = 0;
	_bytesClosed = 0;
#endif

}
void StreamingGrid::GarbageCollect(bool const bForce)
{
	static constexpr milliseconds const interval(GARBAGE_COLLECTION_INTERVAL);
	static constinit nanoseconds accumulator{};

	if ((accumulator += critical_delta()) >= interval) {

		static constinit bool mode{};

		// load balancing //
		uint32_t start(0), end(WorldGrid::CHUNK_COUNT >> 1);
		if (mode) {
			start = end;
			end = WorldGrid::CHUNK_COUNT;
		}
		mode = !mode;
		
		// go thru all chunks, closing chunks that have exceeded the TTL (time to live)
		tbb::parallel_for(start, end, [&](uint32_t const i) {

			// nothing should be initially pending when Garbage Collect is called //
			Chunk& chunk(world_grid._chunks[i]);
			if (chunk._state.test()) { /**/ // OPEN = set // /**/ 
	
				tTime const last_access(chunk._last_access);

				if ((critical_now() - last_access) >= CHUNK_TTL || bForce) {

					chunk.close(); // close the chunk
				}
			}
		});

		accumulator -= interval;
	}
	
#ifdef DEBUG_OUTPUT_STREAMING_STATS
	{
		static constexpr milliseconds const debug_interval(5); // prevent successive rapid execution of the streaming stats
		static constinit tTime tLast{};

		if (critical_now() - tLast > debug_interval) {

			tLast = critical_now();

			_chunksOpen = 0;
			_chunksClosed = 0;
			_bytesOpen = 0;
			_bytesClosed = 0;

			for (uint32_t i = 0; i < WorldGrid::CHUNK_COUNT; ++i) {

				// nothing should be initially pending when Garbage Collect is called //
				Chunk& chunk(world_grid._chunks[i]);
				if (chunk._state.test()) { // OPEN

					++_chunksOpen;
					_bytesOpen += WorldGrid::CHUNK_COUNT * sizeof(Iso::Voxel);
				}
				else { // CLOSED

					++_chunksClosed;
					_bytesClosed += chunk._compressed_size;
				}
			}
		}
	}
#endif
    
}

// ***** left as reference to SPARSE FILE SUPPORT #############################################################################

/*
bool const StreamingGrid::OpenStreamingGridFile() {

	bool bSuccess(false);

	if (nullptr == _filePersistantHandle || INVALID_HANDLE_VALUE == _filePersistantHandle) {

		std::wstring szMemoryMappedPathFilename(cMinCity::getUserFolder());
		szMemoryMappedPathFilename += VIRTUAL_DIR;
		szMemoryMappedPathFilename += L"streaming";
		szMemoryMappedPathFilename += GRID_FILE_EXT;

		std::error_code error{};
		
		mio::file_handle_type fileHandle = mio::detail::open_file(szMemoryMappedPathFilename, mio::access_mode::write, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_RANDOM_ACCESS, error);

		if (!error && INVALID_HANDLE_VALUE != fileHandle) {
	
			// FILE_ATTRIBUTE_SPARSE_FILE
			bSuccess = DeviceIoControl(
				fileHandle,                         // handle to a file
				FSCTL_SET_SPARSE,                         // dwIoControlCode
				nullptr,                                  // input buffer
				0,                    // size of input buffer
				NULL,                                     // lpOutBuffer
				0,                                        // nOutBufferSize
				nullptr,                // number of bytes returned
				nullptr              // OVERLAPPED structure
			);

			if (bSuccess) {
				_filePersistantHandle = fileHandle;
			}
		}
	}

	return(nullptr != _filePersistantHandle && bSuccess);
}

void StreamingGrid::CloseStreamingGridFile()
{
	if (_filePersistantHandle) {
		FlushFileBuffers(_filePersistantHandle); // required
		CloseHandle(_filePersistantHandle);
		_filePersistantHandle = nullptr;
	}
}
*/

#ifdef DEBUG_OUTPUT_STREAMING_STATS
void StreamingGrid::OutputDebugStats(fp_seconds const& tDelta)
{
	static constexpr fp_seconds interval = fp_seconds(milliseconds(33));
	constinit static fp_seconds elapsed{};
	constinit static size_t count{};
	constinit static size_t
		chunksOpen{},
		chunksClosed{},
		bytesOpen{},
	    bytesClosed{};

	++count;

	chunksOpen += _chunksOpen;
	chunksClosed += _chunksClosed;
	bytesOpen += _bytesOpen;
	bytesClosed += _bytesClosed;

	elapsed += tDelta;
	if (elapsed >= interval) {

#ifndef NDEBUG
		FMT_NUKLEAR_DEBUG(false, "open {:n} - {:n} bytes)  /  closed {:n} - {:n} bytes",
			(chunksOpen / count), (bytesOpen / count), (chunksClosed / count), (bytesClosed / count)
		);
#endif
		MinCity::Nuklear->debug_update_streaming(chunksClosed / count, chunksClosed / count, chunksOpen / count);

		//reset for averaging stats
		count = 0.0f;
		chunksOpen = 0;
		chunksClosed = 0;
		bytesOpen = 0;
		bytesClosed = 0;
		elapsed -= interval;
	}
}
#endif

StreamingGrid::~StreamingGrid()
{
	// free context
	if (::world_grid._context) {
		density_free_context(::world_grid._context, scalable_free); ::world_grid._context = nullptr;
	}

	// free all chunks
	if (::world_grid._chunks) {

		static constexpr size_t const
			WORLD_CHUNKS_SIZE = (Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE) / CHUNK_VOXELS;

		constexpr uint32_t const chunk_count(WORLD_CHUNKS_SIZE);

		for (uint32_t i = 0; i < chunk_count; ++i) {
			if (::world_grid._chunks[i]._data) {
				scalable_free(::world_grid._chunks[i]._data); ::world_grid._chunks[i]._data = nullptr;
			}
		}

		scalable_aligned_free(::world_grid._chunks); ::world_grid._chunks = nullptr;
	}

}