#include "pch.h"
#include "cVoxelWorld.h"
#include "MinCity.h"
#include "data.h"
#include "CityInfo.h"
#include <Imaging\Imaging\Imaging.h>
#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include <vector>
#include <Utility/stringconv.h>
#include <density.h>	// https://github.com/centaurean/density - Density, fastest compression/decompression library out there with simple interface. must reproduce license file. attribution.

namespace fs = std::filesystem;

static inline struct {
	std::vector<std::string>	cityname;		// indices
	std::vector<std::wstring>	cityfile;		// match
} _loadList;

// ## LOADING
// this file contains the class methods of cVoxelWorld for loading, seperated due to cVoxelWorld.cpp size/length complexity
namespace world
{
	std::vector<std::string> const& cVoxelWorld::getLoadList() const
	{
		return(_loadList.cityname);
	}
	void cVoxelWorld::RefreshLoadList()
	{
		fs::path const save_extension(CITY_EXT);

		fs::path path_to_saves{ MinCity::getUserFolder() };
		path_to_saves += SAVE_DIR;

		// make sure SAVE_DIR directory exists //
		if (!std::filesystem::exists(path_to_saves)) {
			std::filesystem::create_directory(path_to_saves);
		}

		_loadList.cityname.clear();
		_loadList.cityfile.clear();

		for (auto const& entry : fs::directory_iterator(path_to_saves)) {

			if (entry.path().extension() == save_extension) {
				// remeber *unicode* path
				_loadList.cityfile.emplace_back(entry.path());

				std::wstring const& szCityFile(_loadList.cityfile.back());
				
				std::wstring szIsolateName(szCityFile.substr(szCityFile.find_last_of('\\') + 1)); // remove path
				szIsolateName = szIsolateName.substr(0, szIsolateName.length() - 5); // remove extension .c1ty

				// done isolating name from path and now converting to *ascii*
				// indices of both cityfile && cityname are synchronized. They map to each other.
				_loadList.cityname.emplace_back(stringconv::ws2s(szIsolateName));
			}

		}
	}
	
	STATIC_INLINE void ReadData(void* const __restrict DestStruct, uint8_t const* const __restrict pReadPointer, uint32_t const SizeOfDestStruct)
	{
		memcpy(DestStruct, pReadPointer, SizeOfDestStruct);
	}

	STATIC_INLINE bool const CompareTag(uint32_t const TagSz, uint8_t const* __restrict pReadPointer, char const* const& __restrict szTag)
	{
		int32_t iDx = TagSz - 1;

		// push read pointer from beginning to end instead for start, makes life easier in loop
		pReadPointer = pReadPointer + iDx;
		do
		{

			if (szTag[iDx] != *pReadPointer--)
				return(false);

		} while (--iDx >= 0);

		return(true);
	}

	bool const cVoxelWorld::PreviewWorld(std::string_view const szCityName, CityInfo&& __restrict info, ImagingMemoryInstance* const __restrict load_thumbnail) const
	{
		if (nullptr != load_thumbnail) {

			int32_t iFound(-1);
			int32_t const city_count((int32_t)_loadList.cityname.size());

			// find rembered *unicode* path from name

			// matching *ascii* "city names"
			// simple linear iterative search used here, not going to take significant time
			for (int32_t i = 0 ; i < city_count ; ++i) {
				if (szCityName == _loadList.cityname[i]) {
					iFound = i;
					break;
				}
			}
			// synchronized indices, lookup remebered *unicode* path
			if (iFound >= 0) {

				std::wstring const path(_loadList.cityfile[iFound]);

				std::error_code error{};

				mio::mmap_source mmap = mio::make_mmap_source(path, error);
				if (!error) {

					if (mmap.is_open() && mmap.is_mapped()) {
						__prefetch_vmem(mmap.data(), mmap.mapped_length());	// only prefetch a smaller size

						uint8_t const* pReadPointer((uint8_t*)mmap.data());

						// Check Header
						static constexpr uint32_t const  TAG_LN = 4;
						static constexpr char const      TAG_C1TY[TAG_LN] = { 'C', '1', 'T', 'Y' };

						if (CompareTag(_countof(TAG_C1TY), pReadPointer, TAG_C1TY)) {

							voxelWorldDesc headerChunk;

							// read header
							ReadData((void* const __restrict) & headerChunk, pReadPointer, sizeof(headerChunk));
							pReadPointer += sizeof(headerChunk);

							// read city name
							std::string szCityNameInFile; szCityNameInFile.reserve(headerChunk.name_length); szCityNameInFile.resize(headerChunk.name_length);
							ReadData((void* const __restrict)&szCityNameInFile[0], pReadPointer, headerChunk.name_length);
							pReadPointer += headerChunk.name_length;

							if (szCityName == szCityNameInFile) {
								
								// read city info
								CityInfo read_info{};
								ReadData((void* const __restrict)&read_info, pReadPointer, sizeof(CityInfo));
								info = std::move(read_info);
								pReadPointer += sizeof(CityInfo);

								// read thumbnail image
								constexpr uint32_t const offscreen_image_size(offscreen_thumbnail_width * offscreen_thumbnail_height * sizeof(uint32_t));

								memcpy(&load_thumbnail->block[0], pReadPointer, offscreen_image_size);
								
								return(true);
							}
						}
					}
				}
			}
		}
		return(false);
	}

	void cVoxelWorld::LoadWorld()
	{
		std::string_view const szCityName(MinCity::getCityName());

		int32_t iFound(-1);
		int32_t const city_count((int32_t)_loadList.cityname.size());

		// find rembered *unicode* path from name

		// matching *ascii* "city names"
		// simple linear iterative search used here, not going to take significant time
		for (int32_t i = 0 ; i < city_count ; ++i) {
			if (szCityName == _loadList.cityname[i]) {
				iFound = i;
				break;
			}
		}
		// synchronized indices, lookup remebered *unicode* path
		if (iFound >= 0) {

			std::wstring const path(_loadList.cityfile[iFound]);

			std::error_code error{};

			mio::mmap_source mmap = mio::make_mmap_source(path, error);
			if (!error) {

				if (mmap.is_open() && mmap.is_mapped()) {
					__prefetch_vmem(mmap.data(), mmap.size());

					MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(10));

					uint8_t const* pReadPointer((uint8_t*)mmap.data());

					// Check Header
					static constexpr uint32_t const  TAG_LN = 4;
					static constexpr char const      TAG_C1TY[TAG_LN] = { 'C', '1', 'T', 'Y' };

					if (CompareTag(_countof(TAG_C1TY), pReadPointer, TAG_C1TY)) {

						MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(20));

						voxelWorldDesc headerChunk;

						// read header
						ReadData((void* const __restrict) & headerChunk, pReadPointer, sizeof(headerChunk));
						pReadPointer += sizeof(headerChunk);

						// read city name
						std::string szCityNameInFile; szCityNameInFile.reserve(headerChunk.name_length); szCityNameInFile.resize(headerChunk.name_length);
						ReadData((void* const __restrict) & szCityNameInFile[0], pReadPointer, headerChunk.name_length);
						pReadPointer += headerChunk.name_length;

						// skip over city info 
						pReadPointer += sizeof(CityInfo);

						// skip over offscreen image
						constexpr uint32_t const offscreen_image_size(offscreen_thumbnail_width * offscreen_thumbnail_height * sizeof(uint32_t));
						pReadPointer += offscreen_image_size;

						static constexpr uint32_t const voxel_count(Iso::WORLD_GRID_SIZE * Iso::WORLD_GRID_SIZE);
						static constexpr size_t const gridSz(sizeof(Iso::Voxel) * size_t(voxel_count));

						// verify
						if (szCityName == szCityNameInFile && headerChunk.voxel_count == voxel_count) {

							MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(30));

							// read compressed grid, using decompression - its already memory mapped

							// Determine safe buffer sizes
							size_t const decompress_safe_size = density_decompress_safe_size(gridSz);

							uint8_t* __restrict outDecompressed((uint8_t * __restrict)scalable_malloc(decompress_safe_size));

							density_processing_result const result = density_decompress((uint8_t* const __restrict) & pReadPointer[0], headerChunk.grid_compressed_size, outDecompressed, decompress_safe_size);

							if (!result.state) {

								MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(50));

								cVoxelWorld::GridSnapshotLoad((Iso::Voxel const* const __restrict)outDecompressed);
								pReadPointer += headerChunk.grid_compressed_size;

								MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(70));

								for (size_t voxel = 0; voxel < voxel_count; ++voxel) {

									// todo
								}
								MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(100));
							}

							if (outDecompressed) {
								scalable_free(outDecompressed);
								outDecompressed = nullptr;
							}
						}
					}
				}
			}
		}
	}

} // end ns

