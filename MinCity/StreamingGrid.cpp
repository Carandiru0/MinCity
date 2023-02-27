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
#include <mimalloc.h>   // https://microsoft.github.io/mimalloc/modules.html - mimalloc - fastest allocator available. maintained by Microsoft. MIT License.

#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

#ifdef DEBUG_OUTPUT_STREAMING_STATS

#include "cNuklear.h"
#include "MinCity.h"

#endif

// ################ *bugfix - do *not* change the type of mutex used per Chunk ################# //
// ** During Garbage Collection or any closing of Chunks, there can be simultaneous access async with getVoxel / setVoxel ***

namespace // private to this file (anonymous)
{
	static constexpr int8_t const
		CLOSED = 0,
		OPEN = 1;

	typedef struct no_vtable alignas(64) Chunk { // ordering - most frequently accessed - not size

		// temporal (time) //
		struct alignas(64) {
			std::atomic_flag       _state;            // *bugfix - mutex no longer required. Concurrent access is only during RenderGrid, and all streaming grid access at that time is read-only. All other time during a frame there is no concurrent access to the grid.
			std::atomic<tTime>     _last_access;      //         - close() also has all unique write locations. the grid maintains an embarrisingly parallel coherence  (with the usage of thread_local decompression and compression buffers) 
		}; // 24 bytes
		
		// space //
		struct alignas(64) {
			uint8_t*               _data; // data is compressed if CLOSED, or, data is decompressed if OPEN
			uint16_t               _compressed_size;
		}; // 16 bytes

	private:
		__declspec(safebuffers) void open();

	public:
		__declspec(safebuffers) Iso::Voxel const open(uint32_t const index);
		__declspec(safebuffers) void update(uint32_t const index, Iso::Voxel const&& oVoxel);
		__declspec(safebuffers) void close();

	} Chunk; // 128 bytes
	static_assert(sizeof(Chunk) <= 128); // Ensure Chunk is correct size @ compile time

	static inline thread_local struct no_vtable alignas(64) decompressed {

		static constexpr uint32_t const
			DECOMPRESS_SAFE_BUFFER_SIZE = 2304;  // 2304 is a safe decompression size for density for 2048 bytes below in chunk

		struct {
			uint8_t    buffer[DECOMPRESS_SAFE_BUFFER_SIZE]{};          // 2304 is a safe decompression size for density for 2048 bytes above in chunk
		} safe;

	} thread_local_decompress_chunks;
	
	static inline thread_local struct no_vtable alignas(64) compressed {

		static constexpr uint32_t const
			COMPRESS_SAFE_BUFFER_SIZE = 2784;    // 2784 ""    ""  compression    ""         ""        ""    ""    ""      ""

		struct {
			uint8_t    buffer[COMPRESS_SAFE_BUFFER_SIZE]{};          // 2784 is a safe compression size for density for 2048 bytes above in chunk
		} safe;

	} thread_local_compress_chunks;

	static inline constinit struct no_vtable WorldGrid
	{
		static constexpr uint32_t const CHUNK_VOXELS = StreamingGrid::CHUNK_VOXELS; // Must always be power of 2
		static constexpr uint32_t const CHUNK_BITS = 6; // must be CHUNK_VOXELS = (1 << CHUNK_BITS) (manually set to match)
		static constexpr uint32_t const CHUNK_SIZE = CHUNK_VOXELS * sizeof(Iso::Voxel);
		static constexpr uint32_t const CHUNK_COUNT = (Iso::WORLD_GRID_WIDTH * Iso::WORLD_GRID_HEIGHT) / CHUNK_VOXELS;
		
		static constexpr uint32_t const HEADER_SIZE = 8; // same value as density_header_size(), constant does not change in density always equals 8 bytes.

		Chunk* __restrict            _chunks = nullptr;
		density_context*             _context = nullptr; // re-usuable context for decompression/compression 

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

	// private //
	__declspec(safebuffers) void Chunk::open()
	{
		static constexpr size_t const ALIGNMENT = alignof(Iso::Voxel);

		if (!_state.test_and_set(std::memory_order_relaxed)) { // CLOSED = false/clear

			/**/ // OPEN = set // /**/
			// write access

			[[unlikely]] if (nullptr == _data) { // treat as OPEN & skip decompression
				_data = (uint8_t* const __restrict)mi_zalloc_aligned(WorldGrid::CHUNK_SIZE, ALIGNMENT);
			}
			else {
				density_processing_result const result = density_decompress_with_context(_data + ::WorldGrid::HEADER_SIZE, // _data is compressed, skip over header
					                                                                     _compressed_size,
					                                                                     thread_local_decompress_chunks.safe.buffer, thread_local_decompress_chunks.DECOMPRESS_SAFE_BUFFER_SIZE,
					                                                                     ::world_grid._context);
				if (!result.state) {

					_data = (uint8_t* const __restrict)mi_realloc_aligned(_data, WorldGrid::CHUNK_SIZE, ALIGNMENT); // _data becomes decompressed

					// copy out to chunk local cache //
					memcpy(_data, thread_local_decompress_chunks.safe.buffer, WorldGrid::CHUNK_SIZE);
				}
			}
		}
		else if (nullptr == _data) { // treat as OPEN & skip decompression

			// write access
			_data = (uint8_t* const __restrict)mi_zalloc_aligned(WorldGrid::CHUNK_SIZE, ALIGNMENT);
		}

	}

	// public //
	__declspec(safebuffers) Iso::Voxel const Chunk::open(uint32_t const index)  // used by getVoxel() of StreamingGrid
	{
		// fast-path
		open(); // open chunk

		_last_access.store(critical_now(), std::memory_order_relaxed); // atomic  [after read access]

		Iso::Voxel const* const decompressed(reinterpret_cast<Iso::Voxel const* const>(_data));
		return(decompressed[index]);
	}

	__declspec(safebuffers) void Chunk::update(uint32_t const index, Iso::Voxel const&& oVoxel) // used by setVoxel() of StreamingGrid
	{
		// fast-path
		open(); // open chunk

		_last_access.store(critical_now(), std::memory_order_relaxed); // atomic  [before write access]

		Iso::Voxel* const decompressed(reinterpret_cast<Iso::Voxel* const>(_data));
		decompressed[index] = std::move(oVoxel);
	}

	// mutex always enabled
	__declspec(safebuffers) void Chunk::close() // used by GarbageCollection() of StreamingGrid
	{
		if (_state.test(std::memory_order_relaxed)) { // OPEN = true/set

			/**/ // CLOSED = clear // /**/
			_state.clear(std::memory_order_relaxed); /**/

			// read-only access //

			density_processing_result const result = density_compress_with_context(_data, // _data is decompressed
																					WorldGrid::CHUNK_SIZE,
																					thread_local_compress_chunks.safe.buffer, thread_local_compress_chunks.COMPRESS_SAFE_BUFFER_SIZE,
																					::world_grid._context);

			if (!result.state) {

				size_t const new_compressed_size(result.bytesWritten);

				// write access //

				_data = (uint8_t* const __restrict)mi_realloc(_data, new_compressed_size); // _data becomes compressed

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
		// set special allocator memory options
		mi_option_set_enabled_default(mi_option_show_errors, false);
		mi_option_disable(mi_option_show_errors);
		mi_option_set_enabled_default(mi_option_show_stats, false);
		mi_option_disable(mi_option_show_stats);
		mi_option_set_enabled_default(mi_option_verbose, false);
		mi_option_disable(mi_option_verbose);

#ifdef DEBUG_OUTPUT_STREAMING_STATS
		// for debugging:
		mi_option_enable(mi_option_show_errors);
		mi_option_enable(mi_option_show_stats);
		mi_option_enable(mi_option_verbose);
		mi_stats_reset();
#endif

		::world_grid._chunks = (Chunk* const __restrict)mi_zalloc_aligned(sizeof(Chunk) * WorldGrid::CHUNK_COUNT, CACHE_LINE_BYTES);

		FMT_LOG(VOX_LOG, "world chunk allocation: {:n} bytes", (sizeof(Chunk) * WorldGrid::CHUNK_COUNT));

		// Setup context
		::world_grid._context = density_alloc_context(DENSITY_ALGORITHM_CHAMELEON, false, mi_malloc); //*bugfix - CHEETAH and LION are bugged "input buffer too small" error. Only CHAMELEON works properly it seems...

		// Determine safe buffer sizes ~4400 bytes	 
		size_t const decompress_safe_size = density_decompress_safe_size(WorldGrid::CHUNK_SIZE);
		size_t const compressed_safe_size = density_compress_safe_size(WorldGrid::CHUNK_SIZE);

		FMT_LOG(VOX_LOG, "chunk size: {:n} bytes / chunk decompress safe size: {:n} bytes / chunk compress safe size: {:n} bytes", WorldGrid::CHUNK_SIZE, decompress_safe_size, compressed_safe_size);
		
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

__declspec(safebuffers) Iso::Voxel const __vectorcall StreamingGrid::getVoxel(point2D_t const voxelIndex) const
{
	uint32_t const offset(voxelIndex.y * Iso::WORLD_GRID_WIDTH + voxelIndex.x);

	Chunk* const __restrict chunk = ::world_grid.voxelToChunk(offset);

	// open chunk
	return(chunk->open(offset & (StreamingGrid::CHUNK_VOXELS - 1)));
}

__declspec(safebuffers) void __vectorcall StreamingGrid::setVoxel(point2D_t const voxelIndex, Iso::Voxel const&& oVoxel)
{
	uint32_t const offset(voxelIndex.y * Iso::WORLD_GRID_WIDTH + voxelIndex.x);

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

// ** During Garbage Collection or any closing of Chunks, there can be simultaneous access async with getVoxel / setVoxel ***
__declspec(safebuffers) void StreamingGrid::GarbageCollect(tTime const tNow, nanoseconds const tDelta, bool const bForce)
{
	static constexpr milliseconds const interval(GARBAGE_COLLECTION_INTERVAL);
	static constinit nanoseconds accumulator{};

	if ((accumulator += tDelta) >= interval || bForce) {

		static constinit uint32_t mode{};

		// load balancing //
		constexpr uint32_t const quadrant_size(WorldGrid::CHUNK_COUNT >> 2);
		constexpr uint32_t const batch_size((uint32_t const)SFM::ct_sqrt(quadrant_size) + 1); // maximize partioning performance by having NxN seperated among tasks. worth the compile time sqrt here. +1 to slightly overlap, however no chunks are missed

		uint32_t start(0), end(quadrant_size);
		// select quadrant to scan for closures
		for (uint32_t quadrant = 0; quadrant < mode; ++quadrant) {
			start = end;
			end = start + quadrant_size;
		}
		// update mode for next garbage collection
		if (++mode >= 4) {
			mode = 0;
		}

		// go thru all chunks, closing chunks that have exceeded the TTL (time to live)
		tbb::auto_partitioner part; // load-balancing
		tbb::parallel_for(tbb::blocked_range<uint32_t>(start, end, batch_size), [&](tbb::blocked_range<uint32_t> const& r) {

			uint32_t const	// pull out into registers from memory
			    begin(r.begin()),
			    end(r.end());

			for (uint32_t i = begin; i < end; ++i) {

				Chunk& chunk(world_grid._chunks[i]);

				tTime const last_access(chunk._last_access.load(std::memory_order_relaxed));

				if ((tNow - last_access) >= CHUNK_TTL || bForce) {

					chunk.close(); // close the chunk
				}
			}
		}, part);

		accumulator -= interval;

		if (nanoseconds(0) == accumulator || bForce) { // randomly engage the garbage collection of the dedicated allocator for the streaming grid during run-time. Reduces memory usage, and fragmentation.
			mi_collect(bForce);                        // If Forced, there could be a significant delay - used only during load-time to reduce memory pressure / usage. Do not use force during run-time.
		}
	}
	
#ifdef DEBUG_OUTPUT_STREAMING_STATS // Has a large impact on performance, update infrequently!  ** causes a visual "hitch" every second. ***
	{
		static constexpr milliseconds const debug_interval(1111); // prevent successive rapid execution of the streaming stats
		static constinit tTime tLast{};

		if (tNow - tLast > debug_interval) {

			tLast = tNow;

			_chunksOpen = 0;
			_chunksClosed = 0;
			_bytesOpen = 0;
			_bytesClosed = 0;

			for (uint32_t i = 0; i < WorldGrid::CHUNK_COUNT; ++i) {

				// nothing should be initially pending when Garbage Collect is called //
				Chunk& chunk(world_grid._chunks[i]);
				if (chunk._state.test()) { // OPEN

					++_chunksOpen;
					_bytesOpen += WorldGrid::CHUNK_SIZE;
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
		count = 0;
		chunksOpen = 0;
		chunksClosed = 0;
		bytesOpen = 0;
		bytesClosed = 0;
		elapsed -= interval;
	}
}
#endif

static void mi_output_function(const char* msg, void* arg)
{
	fmt::print(fg(fmt::color::orange_red), "{:s}", msg);
}

void StreamingGrid::CleanUp()
{
	// free context
	if (::world_grid._context) {
		density_free_context(::world_grid._context, mi_free); ::world_grid._context = nullptr;
	}

	// free all chunks
	if (::world_grid._chunks) {

		// *bugfix - on program exit this causes a massive amount of memory to release memory
		// very strange - since this is @ program exit, this memory will be re-claimed by the OS anyways

		/*
		static constexpr size_t const
			WORLD_CHUNKS_SIZE = (Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE) / CHUNK_VOXELS;

		constexpr uint32_t const chunk_count(WORLD_CHUNKS_SIZE);

		for (uint32_t i = 0; i < chunk_count; ++i) {
			if (::world_grid._chunks[i]._data) {
				mi_free(::world_grid._chunks[i]._data); ::world_grid._chunks[i]._data = nullptr;
			}
		}
		*/

		mi_free_aligned(::world_grid._chunks, CACHE_LINE_BYTES); ::world_grid._chunks = nullptr;
	}

#ifdef DEBUG_OUTPUT_STREAMING_STATS
	mi_stats_merge();
	mi_stats_print_out(mi_output_function, nullptr);
#endif

}

StreamingGrid::~StreamingGrid()
{
	CleanUp();
}
