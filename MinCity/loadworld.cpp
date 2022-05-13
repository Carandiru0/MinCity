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
#include "cNonUpdateableGameObject.h"

namespace fs = std::filesystem;

static inline struct {
	vector<std::string>		cityname;		// indices
	vector<std::wstring>	cityfile;		// match
} _loadList;

// ## LOADING
// this file contains the class methods of cVoxelWorld for loading, seperated due to cVoxelWorld.cpp size/length complexity
namespace world
{
	vector<std::string> const& cVoxelWorld::getLoadList() const
	{
		return(_loadList.cityname);
	}
	void cVoxelWorld::RefreshLoadList()
	{
		fs::path const save_extension(CITY_EXT);

		fs::path path_to_saves{ MinCity::getUserFolder() };
		path_to_saves += SAVE_DIR;

		_loadList.cityname.clear();
		_loadList.cityfile.clear();

		for (auto const& entry : fs::directory_iterator(path_to_saves)) {

			if (entry.path().extension() == save_extension) {
				// remeber *unicode* path
				_loadList.cityfile.emplace_back(entry.path());

				std::wstring const& szCityFile(_loadList.cityfile.back());
				
				std::wstring szIsolateName(szCityFile.substr(szCityFile.find_last_of('/') + 1)); // remove path
				szIsolateName = szIsolateName.substr(0, szIsolateName.length() - 5); // remove extension .c1ty

				// done isolating name from path and now converting to *ascii*
				// indices of both cityfile && cityname are synchronized. They map to each other.
				_loadList.cityname.emplace_back(stringconv::ws2s(szIsolateName));
			}

		}
	}
	
	STATIC_INLINE void ReadData(void* const __restrict DestStruct, uint8_t const* const __restrict pReadPointer, uint32_t const SizeOfDestStruct)
	{
		memcpy_s(DestStruct, SizeOfDestStruct, pReadPointer, SizeOfDestStruct);
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

				mio::mmap_source mmap = mio::make_mmap_source(path, false, error);
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

								memcpy_s(&load_thumbnail->block[0], offscreen_image_size, pReadPointer, offscreen_image_size);
								
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

			mio::mmap_source mmap = mio::make_mmap_source(path, false, error);
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

								InitializeRandomNumberGenerators(headerChunk.secure_seed); // use the seed the city was saved with for program (deterministic random state) now

								MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(50));

								cVoxelWorld::GridSnapshotLoad((Iso::Voxel const* const __restrict)outDecompressed);
								pReadPointer += headerChunk.grid_compressed_size;

								MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(70));

								{
									vector<model_state_instance_static> data_models_static;
									{ // do all static //

										size_t const count(*((size_t const* const)pReadPointer));
										pReadPointer += sizeof(size_t);

										data_models_static.reserve(count); data_models_static.resize(count);

										size_t const bytes(sizeof(model_state_instance_static) * count);
										memcpy_s(data_models_static.data(), bytes, pReadPointer, bytes); // file access using memory-mapped io. secure version of memcpy_s
										pReadPointer += bytes;
									}

									vector<model_state_instance_dynamic> data_models_dynamic;
									{ // do all dynamic //

										size_t const count(*((size_t const* const)pReadPointer));
										pReadPointer += sizeof(size_t);

										data_models_dynamic.reserve(count); data_models_dynamic.resize(count);

										size_t const bytes(sizeof(model_state_instance_dynamic) * count);
										memcpy_s(data_models_dynamic.data(), bytes, pReadPointer, bytes); // file access using memory-mapped io. secure version of memcpy_s
										pReadPointer += bytes;
									}

									vector<model_root_index> data_rootIndex;
									{ // do all voxelRootIndices //

										size_t const count(*((size_t const* const)pReadPointer));
										pReadPointer += sizeof(size_t);

										data_rootIndex.reserve(count); data_rootIndex.resize(count);

										size_t const bytes(sizeof(model_root_index) * count);
										memcpy_s(data_rootIndex.data(), bytes, pReadPointer, bytes); // file access using memory-mapped io. secure version of memcpy_s
										pReadPointer += bytes;
									}

									MinCity::VoxelWorld->upload_model_state(data_rootIndex, data_models_static, data_models_dynamic);
								}
								
								{ // load gameobject specific data //

									// read total / all gameobjects data size
									int64_t bytes_all((int64_t )*((size_t const* const)pReadPointer));
									pReadPointer += sizeof(size_t);

									uint32_t zero_count(0);

									while (bytes_all && zero_count < file_delim_zero_count) {

										if (*pReadPointer) { // non-zero value?

											zero_count = 0; // reset consecutive zero count

											// read owner hash
											uint32_t const hash(*((uint32_t const* const)pReadPointer));
											pReadPointer += sizeof(uint32_t);
											bytes_all -= sizeof(uint32_t);

											if (hash) {
												// read gameobject data size
												uint32_t const bytes(*((uint32_t const* const)pReadPointer));
												pReadPointer += sizeof(uint32_t);
												bytes_all -= sizeof(uint32_t);

												if (bytes) {
													
													// only need base //
													bool bDynamic(true); // default to dynamic
													Volumetric::voxelModelInstanceBase* instance(nullptr);

													instance = MinCity::VoxelWorld->lookupVoxelModelInstance<true>(hash); // search dynamic
													if (!instance) {
														instance = MinCity::VoxelWorld->lookupVoxelModelInstance<false>(hash); // search static
														bDynamic = false;
													}
													
													// we have the world model instance associated with this hash. (loaded before in upload_model_state(), into the actual voxelworld)
													if (instance) {

														if (bDynamic) {
															tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>* const pGameObject = instance->getOwnerGameObject< tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic> >();
															// read gameobject data
															pGameObject->importData(pReadPointer, bytes);
														}
														else {
															tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>* const pGameObject = instance->getOwnerGameObject< tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static> >();
															// read gameobject data
															pGameObject->importData(pReadPointer, bytes);
														}
														
													}
													pReadPointer += bytes; // advance
													bytes_all -= bytes;
												}
												else {
													++pReadPointer; // continue to next byte if errornous
													--bytes_all;
												}
											}
											else {
												++pReadPointer; // continue to next byte if errornous
												--bytes_all;
											}
										}
										else {
											++pReadPointer; // advance
											--bytes_all;
											++zero_count;
										}
									}
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

		_onLoadedRequired = true; // trigger onloaded() inside Update of VoxelWorld
	}

} // end ns

