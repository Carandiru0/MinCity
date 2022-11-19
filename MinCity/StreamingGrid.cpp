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

// dependent on speed of movement. the ships speed. faster = more cache required 
//     * keep in mind that the larger the maps size is, will generally affect performance as it grows larger - the size is equal to LRU_CACHE_SIZE eg.) 131072 voxels is 8MB @ 128, 262144 voxels is 16MB @ 256 ...
//     * CHUNK_VOXELS affects everything, set first then start adjusting the factor of LRU_CACHE_SIZE
//     * converge to ~map_size == ~LRU_CACHE_SIZE during *runtime* at the *speed* of camera translation that *balances* with the *maps* size *growth*
//     * stability from 0 to ~max_speed
#define LRU_CACHE_SIZE_HINT (Iso::SCREEN_VOXELS_XZ * 4 * 512) // 8MB @ 128, 16MB @ 256 ...    [set 2nd]

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
namespace {

	std::atomic_uint64_t
		_gridReads{},
		_lruReads{},
		_fileReads{};

	std::atomic_uint64_t
		_gridWrites{},
		_lruWrites{},
		_fileWrites{};

	std::atomic_int32_t
		_minVoxelIndexX{ 999999 },
		_minVoxelIndexY{ 999999 },
		_maxVoxelIndexX{ -999999 },
		_maxVoxelIndexY{ -999999 };
} // end ns
#endif
#endif

namespace // private to this file (anonymous)
{
	static inline tbb::queuing_rw_mutex grid_lock; // for the::grid

	static inline constinit struct // purposely anonymous union, protected pointer implementation for the::grid
	{
		Iso::Voxel* __restrict		 _protected = nullptr;

		__declspec(safebuffers) __forceinline operator Iso::Voxel* const __restrict() const {
			return(_protected);
		}
	} visible_grid{};

	static inline struct lru_cache
	{
	private:
		using lruCache = tbb::concurrent_unordered_map<uint32_t, Iso::Voxel>;  // 1D index, data   -- *note must use _cache_lock to protected concurrent access (only the hashmap is this neccessary)
		lruCache      _cache; // tbb:concurrent_unordered_map has better behaviour than robin_hood::unordered_flat_map, faster and better minimums in frame timing. Also the size of the map doesn't grow so fast.

		using lruQueue = tbb::concurrent_queue<uint32_t>; // optimization, & concurrent replacement for a list
		lruQueue      _queue;

		lruQueue      _free; // free spots in the main lru cache that can be used as they were marked for erasure (part of concurrent "erasure" requirement)

		std::atomic_uint64_t            _maximum_size = LRU_CACHE_SIZE_HINT; // this is adjusted in real-time
		std::atomic_uint64_t            _current_size{};
	public:
		bool const get(uint32_t const index, Iso::Voxel&& oVoxel);
		void       put(uint32_t const index, Iso::Voxel const&& oVoxel, HANDLE const persistantHandle);

		void garbage_collect(HANDLE const persistantHandle); // must be called / frame in a place where no access to the cache may be occuring, in the "dead zone" of the frame.
		void reset();

		lruCache const&            cache() const { return(_cache); }
		uint64_t const             cache_size() const { return(_cache.size()); }
		uint64_t const             queue_size() const { return(_current_size); }
	} _lru;

	static Iso::Voxel const readChunk(HANDLE const persistantHandle, size_t const offset)
	{
		Iso::Voxel oVoxel{};

		//auto const taskID = async_long_task::enqueue<background_critical>([&]
		//{
			std::error_code error{};

			mio::mmap_source mmap = mio::make_mmap_source_file_handle(persistantHandle, offset * sizeof(Iso::Voxel), StreamingGrid::CHUNK_VOXELS * sizeof(Iso::Voxel), error);
			if (!error) {
				if (mmap.is_open() && mmap.is_mapped()) {

					Iso::Voxel const* pVoxelIn(reinterpret_cast<Iso::Voxel const*>(mmap.data()));
					oVoxel = std::move(*pVoxelIn); // returned voxel that specifically was requested to read from persistant file.

					for (uint32_t vxl = 0; vxl < StreamingGrid::CHUNK_VOXELS; ++vxl) { // load chunk of read voxels into lru
						_lru.put((uint32_t)offset + vxl, std::forward<Iso::Voxel const&&>(*pVoxelIn), persistantHandle);
						++pVoxelIn;
					}
				}
			}
		//});
		//async_long_task::wait<background_critical>(taskID, "read chunk wait");

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
		++_fileReads;
#endif
#endif
		return(oVoxel);
	}

	static void writeChunk(HANDLE const persistantHandle, Iso::Voxel const&& oVoxel, size_t const offset)
	{
		async_long_task::enqueue<background>([=] // copy out of oVoxel happens here at the correct time before async op.
		{
			std::error_code error{};

			mio::mmap_sink mmap = mio::make_mmap_sink_file_handle(persistantHandle, offset * sizeof(Iso::Voxel), sizeof(Iso::Voxel), error);
			if (!error) {
				if (mmap.is_open() && mmap.is_mapped()) {

					Iso::Voxel* pVoxelOut(reinterpret_cast<Iso::Voxel*>(mmap.data()));
					*pVoxelOut = std::move(oVoxel);
				}
			}
		});

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS	
		++_fileWrites;
#endif
#endif
	}

	bool const lru_cache::get(uint32_t const index, Iso::Voxel&& oVoxel)
	{
		auto const it(_cache.find(index));
		if (_cache.end() == it) {
			return(false);
		}

		oVoxel = std::move(it->second);
		_queue.emplace(it->first);

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
		++_lruReads;
#endif
#endif
		return(true);
	}

	void lru_cache::put(uint32_t const index, Iso::Voxel const&& oVoxel, HANDLE const persistantHandle)
	{
		{
			auto const it(_cache.find(index));
			if (_cache.end() != it) {
				it->second = std::move(oVoxel);
				_queue.emplace(it->first); // most recently used

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS	
				++_lruWrites;
#endif
#endif
				return; // found existing in cache, current size no change!
			}
		}

		if (_current_size >= _maximum_size || _cache.size() > _maximum_size) {

			uint32_t least_recently_used_index(0);
			// only when there is no contention, size may go over maximum, briefly, until the next iteration with no contention}
			while (_queue.try_pop(least_recently_used_index)) { // least recently used

				auto const it(_cache.find(least_recently_used_index));

				if (_cache.end() != it) {

					// copy out, cause as soon as it's in the free queue, consider the data undefined as it could be used right now on another thread.
					uint32_t const erased_voxel_index(it->first);
					Iso::Voxel const erased_voxel(it->second);

					_free.emplace(erased_voxel_index); // add to free slot queue, marking this slot as erased. nothing actually happens to the data until "garbage collection"

					// dump to disk the data for this erased voxel
					writeChunk(persistantHandle, std::forward<Iso::Voxel const&&>(erased_voxel), erased_voxel_index);

					--_current_size;

					break; // loop continues until a valid least_recently_used_index voxel is found in cache, or there is contention on the try_pop
				}
			}
		}

		{ // known that the index/voxel pair does not exist in lru cache
			_cache.emplace(index, std::move(oVoxel));
		}

		_queue.emplace(index);  // most recently used
		++_current_size;

		// the optimal size converges to a balance between the current size (same as queue_size()) and the actual cache_size() in memory
		//    * adapting this at runtime is very important
		//    * speed decides the maximum growth or maximum size, if to big map performance will suffer
		//    * limit maximum camera translation speed to desired amount, that doesn't negatively impact the maps performance (lru cache too big)
		//    * a fixed maximum size is **inadequate**, as a failure occurs when the actual cache_size() starts exceeding the queue_size() too much
		//    * a dynamic adaptive maximum size always secures cache_size() and queue_size() are never exceeding the other (too much)
		//    * if the maximum size is to big, map performance will suffer. this is calculated at runtime - the maximum size
		//        * dependent on CHUNK_SIZE
		//        * will converge naturally to a maximum size that stops increasing with time, to a value with seemingly no change, this must be observed empiraclly to observe:
		//            1.) map performance
		//            2.) stability
		//            3.) desired maximum cache size -> LRU_CACHE_SIZE_HINT !! which should be close !!
		// 
		//               *
		//               *
		//               *
		//         ****  *
		//               *
		//               *
		//               *
		// 
		//          **cache_size() should periodically exceed the queue_size() and queue_size never exceeds maximum_size**
		//          maximum_size = maximum_size - (cache_size - queue_size)
		//          maximum_size < cache_size
		//          [maximum_size = maximum_size - 1]     **adequate to secure periodic trigger of dumping to disk the least recently used cache element**
		//
		//
		_maximum_size = ((_current_size + _cache.size()) >> 1) - 1;

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS	
		++_lruWrites;
#endif
#endif
	}

	void lru_cache::garbage_collect(HANDLE const persistantHandle)
	{
		if (!_free.empty()) {

			uint32_t erased_voxel_index(0);
			while (_free.try_pop(erased_voxel_index)) { // only until there is contention or end

				_cache.unsafe_erase(erased_voxel_index); // this only works properly if garbage_collect() is called in an area of the frame in the program loop where there is absolutely no access to the cache
			}
		}
	}
	void lru_cache::reset()
	{
		_free.clear();
		_cache.clear();
		_queue.clear();
		_current_size = 0;
	}
} // end ns

StreamingGrid::StreamingGrid()
	:_filePersistantHandle(nullptr), _visibleGridBegin(-1, -1), _visibleGridBounds(0, 0, 0, 0)
{
}

bool const StreamingGrid::Initialize()
{
	// create visible grid memory
	{
		::visible_grid._protected = (Iso::Voxel* const __restrict)scalable_aligned_malloc(sizeof(Iso::Voxel) * Iso::SCREEN_VOXELS_X * Iso::SCREEN_VOXELS_Z, CACHE_LINE_BYTES);

		memset(::visible_grid._protected, 0, sizeof(Iso::Voxel) * Iso::SCREEN_VOXELS_X * Iso::SCREEN_VOXELS_Z);

		FMT_LOG(VOX_LOG, "visible grid voxel allocation: {:n} bytes", (size_t)(sizeof(Iso::Voxel) * Iso::SCREEN_VOXELS_X * Iso::SCREEN_VOXELS_Z));
	}

	return(OpenStreamingGridFile());
		/*
		mio::mmap_sink& mmap = _persistant_mmp = (mio::make_mmap_sink(_filePersistantHandle, offset, length, false, error));
		if (!error) {
			if (mmap.is_open() && mmap.is_mapped()) {

				size = (uint32_t const)mmap.size();
				return(mmap.data());
			}
		}

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
		}*/
}

Iso::Voxel* __restrict StreamingGrid::visible_grid() const
{
	return(::visible_grid);
}

tbb::queuing_rw_mutex& StreamingGrid::access_lock() const
{
	return(::grid_lock);
}

Iso::Voxel const __vectorcall StreamingGrid::getVoxel(point2D_t const voxelIndex) const
{
	size_t const offset = voxelIndex.y * Iso::WORLD_GRID_SIZE + voxelIndex.x;

	// voxelIndex exists in direct visible grid cache
	if (r2D_contains(_visibleGridBounds, voxelIndex)) { 

		point2D_t const localIndex(p2D_sub(voxelIndex, _visibleGridBegin));
#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
		++_gridReads;
#endif
#endif
		return(*(::visible_grid._protected + localIndex.y * Iso::SCREEN_VOXELS_X + localIndex.x));
	}
	// voxelIndex exists in lru grid cache
	{
		Iso::Voxel lruVoxel;
		if (_lru.get((uint32_t)offset, std::forward<Iso::Voxel&&>(lruVoxel))) {
			return(lruVoxel);
		}
	}
#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
	// voxelIndex does not exist in caches, must access disk
	point2D_t minVoxelIndex(_minVoxelIndexX, _minVoxelIndexY),
		      maxVoxelIndex(_maxVoxelIndexX, _maxVoxelIndexY);

	minVoxelIndex = p2D_min(minVoxelIndex, voxelIndex);
	maxVoxelIndex = p2D_max(maxVoxelIndex, voxelIndex);

	_minVoxelIndexX = minVoxelIndex.x;
	_minVoxelIndexY = minVoxelIndex.y;
	_maxVoxelIndexX = maxVoxelIndex.x;
	_maxVoxelIndexY = maxVoxelIndex.y;
#endif
#endif
	return(readChunk(_filePersistantHandle, offset));
}

void __vectorcall StreamingGrid::setVoxel(point2D_t const voxelIndex, Iso::Voxel const&& oVoxel)
{
	size_t const offset = voxelIndex.y * Iso::WORLD_GRID_SIZE + voxelIndex.x;

	// voxelIndex exists in direct visible grid cache
	if (r2D_contains(_visibleGridBounds, voxelIndex)) {   // voxelIndex exists in direct visible grid cache

		point2D_t const localIndex(p2D_sub(voxelIndex, _visibleGridBegin));

		*(::visible_grid._protected + localIndex.y * Iso::SCREEN_VOXELS_X + localIndex.x) = std::move(oVoxel);
#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS	
		++_gridWrites;
#endif
#endif
		return;
	}
	// voxelIndex in lru grid cache
	{
		_lru.put((uint32_t)offset, std::forward<Iso::Voxel const&&>(oVoxel), _filePersistantHandle);
	}
}

void StreamingGrid::CacheVisible(point2D_t const voxelBegin, point2D_t const voxelEnd)
{
	if (_visibleGridBegin == voxelBegin) // same grid loaded?
		return;

	{
		tbb::queuing_rw_mutex::scoped_lock lock(::grid_lock, true); // write grid access

		tbb::auto_partitioner part; /*load balancing - do NOT change - adapts to variance of whats in the voxel grid*/
		tbb::parallel_for(tbb::blocked_range2d<int32_t, int32_t>(voxelBegin.y, voxelEnd.y, THREAD_BATCH_SIZE, voxelBegin.x, voxelEnd.x, THREAD_BATCH_SIZE),
			[&](tbb::blocked_range2d<int32_t, int32_t> const& r)
			{
				int32_t const	// pull out into registers from memory
				y_begin(r.rows().begin()),
				y_end(r.rows().end()),
				x_begin(r.cols().begin()),
				x_end(r.cols().end());

				Iso::Voxel* const pVoxelOut(::visible_grid._protected);
				point2D_t const voxelBegin(x_begin, y_begin);

				point2D_t voxelIndex;
				for (voxelIndex.y = y_begin; voxelIndex.y < y_end; ++voxelIndex.y)
				{
					for (voxelIndex.x = x_begin; voxelIndex.x < x_end; ++voxelIndex.x)
					{
						point2D_t const voxelIndexInWrapped(p2D_wrap(voxelIndex, Iso::WORLD_GRID_SIZE));

						Iso::Voxel const oVoxel(getVoxel(voxelIndexInWrapped));

						point2D_t const voxelIndexOut(p2D_sub(voxelIndex, voxelBegin));

						// before replacing, update lru with most recent value (shoulD already be existing in lru
						// visible grid direct cache //
						*((pVoxelOut + ((voxelIndexOut.y << Iso::WORLD_GRID_SIZE_BITS) + voxelIndexOut.x))) = std::move(oVoxel);
					}
				}
			}, part);
	}
	_visibleGridBegin = voxelBegin;
	_visibleGridBounds = r2D_set_by_width_height(_visibleGridBegin, point2D_t(Iso::SCREEN_VOXELS_XZ));
}

void StreamingGrid::GarbageCollect()
{
	_lru.garbage_collect(_filePersistantHandle); // this only works properly if garbage_collect() is called in an area of the frame in the program loop where there is absolutely no access to the cache
}
void StreamingGrid::Reset()
{
	_lru.reset();

	/*static constexpr size_t const length = Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE;

	CloseStreamingGridFile();

	size_t const batch_size(PAGE_SIZE / sizeof(Iso::Voxel));
	Iso::Voxel* const pBatch = (Iso::Voxel* const __restrict)scalable_aligned_malloc(batch_size * sizeof(Iso::Voxel), CACHE_LINE_BYTES);

	Iso::Voxel oVoxel{};
	oVoxel.Desc = Iso::TYPE_GROUND;
	oVoxel.MaterialDesc = 0;		// Initially visibility is set off on all voxels until ComputeGroundOcclusion()
	Iso::clearColor(oVoxel);		// *bugfix - must clear to default color used for ground on generation.

	{
		Iso::Voxel* pVoxel(pBatch);
		for (size_t i = 0; i < batch_size; ++i) {

			*pVoxel = oVoxel;
			++pVoxel;
		}
	}

	std::wstring szMemoryMappedPathFilename(cMinCity::getUserFolder());
	szMemoryMappedPathFilename += VIRTUAL_DIR;
	szMemoryMappedPathFilename += L"streaming";
	szMemoryMappedPathFilename += GRID_FILE_EXT;

	FILE* stream(nullptr);

	if ((stream = _wfsopen(szMemoryMappedPathFilename.c_str(), L"wbS", _SH_DENYNO))) {

		size_t const batches = (length * sizeof(Iso::Voxel)) / PAGE_SIZE;
		for (size_t batch = 0; batch < batches; ++batch) {
			_fwrite_nolock(pBatch, sizeof(Iso::Voxel), batch_size, stream);
		}
		_fflush_nolock(stream);
		_fclose_nolock(stream); // handover to mmio
		stream = nullptr;
	}

	if (pBatch) {

		scalable_aligned_free(pBatch);
	}
	// re-open persistant mmaping
	OpenStreamingGridFile();*/
}

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

void StreamingGrid::Flush()
{
	// dump lru grid cache to disk (least recent)
	{
		using lruCache = tbb::concurrent_unordered_map<uint32_t, Iso::Voxel>;  // 1D index, data

		lruCache::const_iterator iterMap(_lru.cache().cbegin());
		while (_lru.cache().cend() != iterMap) {

			uint32_t const offset = iterMap->first; // map keys are actually the 1D index

			//                                                            // columns (x)                // rows (y)
			// not required but good reference point2D_t const voxelIndex(offset % Iso::WORLD_GRID_SIZE, offset / Iso::WORLD_GRID_SIZE); // 1D -> 2D coordinate conversion
			
			std::error_code error{};

			mio::mmap_sink mmap = mio::make_mmap_sink_file_handle(_filePersistantHandle, offset * sizeof(Iso::Voxel), sizeof(Iso::Voxel), error);
			if (!error) {
				if (mmap.is_open() && mmap.is_mapped()) {

					Iso::Voxel* const pVoxelOut(reinterpret_cast<Iso::Voxel*>(mmap.data()));

					// lru grid cache -> file//
					*pVoxelOut = std::move(iterMap->second);
				}
			}

			++iterMap;
		}

	}

	// dump direct visible cache to disk (most recent)
	{

		point2D_t const voxelBegin(_visibleGridBounds.left_top()), voxelEnd(_visibleGridBounds.right_bottom());

		size_t const offset = voxelBegin.y * Iso::WORLD_GRID_SIZE + voxelBegin.x;
		constexpr size_t const length = Iso::SCREEN_VOXELS_X * Iso::SCREEN_VOXELS_Z;

		std::error_code error{};

		mio::mmap_sink mmap = mio::make_mmap_sink_file_handle(_filePersistantHandle, offset * sizeof(Iso::Voxel), length * sizeof(Iso::Voxel), error);
		if (!error) {
			if (mmap.is_open() && mmap.is_mapped()) {

				tbb::queuing_rw_mutex::scoped_lock lock(::grid_lock, false); // read-only grid access

				tbb::auto_partitioner part; /*load balancing - do NOT change - adapts to variance of whats in the voxel grid*/
				tbb::parallel_for(tbb::blocked_range2d<int32_t, int32_t>(voxelBegin.y, voxelEnd.y, THREAD_BATCH_SIZE, voxelBegin.x, voxelEnd.x, THREAD_BATCH_SIZE),
					[&](tbb::blocked_range2d<int32_t, int32_t> const& r)
					{
						int32_t const	// pull out into registers from memory
						    y_begin(r.rows().begin()),
							y_end(r.rows().end()),
							x_begin(r.cols().begin()),
							x_end(r.cols().end());

						Iso::Voxel const* pVoxelIn(::visible_grid._protected);
						Iso::Voxel* pVoxelOut(reinterpret_cast<Iso::Voxel*>(mmap.data()));

						point2D_t const voxelBegin(x_begin, y_begin);

						point2D_t voxelIndex;
						for (voxelIndex.y = y_begin; voxelIndex.y < y_end; ++voxelIndex.y)
						{
							for (voxelIndex.x = x_begin; voxelIndex.x < x_end; ++voxelIndex.x)
							{
								point2D_t const voxelIndexInWrapped(p2D_wrap(voxelIndex, Iso::WORLD_GRID_SIZE));

								Iso::Voxel const oVoxel(getVoxel(voxelIndexInWrapped));

								point2D_t const voxelIndexOut(p2D_sub(voxelIndex, voxelBegin));

								// visible grid direct cache -> file//
								(*(pVoxelOut + ((voxelIndexOut.y << Iso::WORLD_GRID_SIZE_BITS) + voxelIndexOut.x))) = std::move(oVoxel);
							}
						}
					}, part);
			}
		}
	}
	// *********** file completely up to date / synchronized with caches ************* //
}

#ifndef NDEBUG
#ifdef DEBUG_OUTPUT_STREAMING_STATS
void StreamingGrid::OutputDebugStats(fp_seconds const& tDelta)
{
	static constexpr fp_seconds interval = fp_seconds(milliseconds(256 * 6 * 2));
	constinit static fp_seconds elapsed{};
	constinit static float count{}, count2{};
	constinit static size_t
		gridReads{},
		gridWrites{},
		lruReads{},
		lruWrites{},
		fileReads{},
		fileWrites{};
	static point2D_t
		minVoxelIndex{},
		maxVoxelIndex{};
	constinit static bool mode{};

	++count;

	gridReads += _gridReads;
	gridWrites += _gridWrites;
	lruReads += _lruReads;
	lruWrites += _lruWrites;
	fileReads += _fileReads;
	fileWrites += _fileWrites;

	if (((-999999 != _maxVoxelIndexX) & (-999999 != _maxVoxelIndexY)) && ((999999 != _minVoxelIndexX) & (999999 != _minVoxelIndexY))) {

		++count2;

		minVoxelIndex = p2D_add(minVoxelIndex, point2D_t(_minVoxelIndexX, _minVoxelIndexY));
		maxVoxelIndex = p2D_add(maxVoxelIndex, point2D_t(_maxVoxelIndexX, _maxVoxelIndexY));
	}

	elapsed += tDelta;
	if (elapsed >= interval) {

		if (mode && !minVoxelIndex.isZero() && !minVoxelIndex.isZero()) {

			FMT_NUKLEAR_DEBUG(false, "min {:5.1f},{:5.1f}  |  max {:5.1f},{:5.1f}",
				(((float)minVoxelIndex.x) / count2), (((float)minVoxelIndex.y) / count2),
				(((float)maxVoxelIndex.x) / count2), (((float)maxVoxelIndex.y) / count2)
			);

			//reset for averaging stats
			count2 = 0.0f;
			minVoxelIndex = point2D_t{};
			maxVoxelIndex = point2D_t{};
		}
		else {

			FMT_NUKLEAR_DEBUG(false, "grid {:6.1f},{:6.1f}  |  lru  {:6.1f},{:6.1f}  |  file  {:6.1f},{:6.1f} |  size  {:d} / {:d}",
				((float)gridReads) / count, ((float)gridWrites) / count,
				((float)lruReads) / count, ((float)lruWrites) / count,
				((float)fileReads) / count, ((float)fileWrites) / count,
				_lru.cache_size(), _lru.queue_size()
			);

			//reset for averaging stats
			count = 0.0f;
			gridWrites = gridReads = 0;
			lruWrites = lruReads = 0;
			fileWrites = fileReads = 0;
		}

		elapsed -= interval;
		mode = !mode;
	}
	//reset for per frame stats
	_gridWrites = _gridReads = 0;
	_lruWrites = _lruReads = 0;
	_fileWrites = _fileReads = 0;

	//reset for per min/max read voxel area per frame stats
	_minVoxelIndexX = 999999;
	_minVoxelIndexY = 999999;
	_maxVoxelIndexX = -999999;
	_maxVoxelIndexY = -999999;
}
#endif
#endif

StreamingGrid::~StreamingGrid()
{
	// dump cache to disk 1st
	Flush();

	// close all 
	CloseStreamingGridFile();

	if (::visible_grid._protected) {
		scalable_aligned_free(::visible_grid._protected);
	}

}