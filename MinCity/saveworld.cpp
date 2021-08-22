#include "pch.h"
#include "cVoxelWorld.h"
#include "MinCity.h"
#include "data.h"
#include "CityInfo.h"
#include "cCity.h"
#include <filesystem>
#include <stdio.h> // C File I/O is 10x faster than C++ file stream I/O
#include <density.h>	// https://github.com/centaurean/density - Density, fastest compression/decompression library out there with simple interface. must reproduce license file. attribution.
#include <Imaging/Imaging/Imaging.h>
#include "cNonUpdateableGameObject.h"

namespace fs = std::filesystem;

// SAVING
// this file contains the class methods of cVoxelWorld for saving, seperated due to cVoxelWorld.cpp size/length complexity
namespace world
{
	template<typename T1, typename T2>
	static void BufferGameObject(uint32_t const hash, T2 const& instance, vector<uint8_t>& data_gameobjects)
	{
		vector<uint8_t> data_gameobject(0); // for this gameobject only, no reserve due to possibility there is no specific gameobject data that needs to be saved for this gameobject

		// don't need actual type, just the base class of which all game objects are derived from
		// this is to invoke the virtual method which is then specific to the actual game object type (leverage polymorphism).
		tNonUpdateableGameObject<T1> const* const pGameObject = instance->getOwnerGameObject< tNonUpdateableGameObject<T1> >();
		size_t const bytes = pGameObject->exportData(data_gameobject);
		if (bytes) { // *only if specific gameobject data is required

			// add to global game object data
			data_gameobjects.reserve(data_gameobjects.size() + 8 + bytes);  // reserve the amount of bytes to be appended in total (single reallocation optimization)

			{ // first the hash (owner) of the gameobject data is appended
				union {
					uint32_t hash;
					uint8_t  split[4];

				} unpack{ hash };

				for (uint32_t i = 0 ; i < 4 ; ++i) 
					data_gameobjects.push_back(unpack.split[i]);
			}
			{ // second the size of the gameobject data is appended
				union {
					uint32_t bytes;
					uint8_t  split[4];

				} unpack{ (uint32_t)bytes }; // only supporting up to 2^32 bytes, no need for size_t

				for (uint32_t i = 0; i < 4; ++i)
					data_gameobjects.push_back(unpack.split[i]);
			}
			data_gameobjects.insert(data_gameobjects.end(), data_gameobject.begin(), data_gameobject.end());		// third the gameobject data is appended
		}
	}

	template<typename T>
	static void BufferStaticModelInstance(uint32_t const hash, world::model_state const& world_model_state, T const& iter, vector<model_state_instance_static>& data_models, vector<model_root_index>& data_rootIndex, vector<uint8_t>& data_gameobjects) {

		// verify good
		// exists
		if (iter->second) {

			uint32_t const gameobject_type = iter->second->getOwnerGameObjectType();

			auto const& iterFind = world_model_state.hshVoxelModelRootIndex.find(hash);
			if (world_model_state.hshVoxelModelRootIndex.cend() != iterFind) {

				data_models.emplace_back(hash, gameobject_type, iter->second->getModel().identity());
				data_rootIndex.emplace_back(hash, iterFind->second); // add to buffer of saved root indices

				// *set additional varying data
				data_models.back().elevation = iter->second->getElevation();

				// set gameobject specific data
				if (types::game_object_t::NoOwner != gameobject_type) {
					BufferGameObject<Volumetric::voxelModelInstance_Static>(hash, iter->second, data_gameobjects);
				}
			}
		}
	}

	static bool const filter_dynamic(uint32_t const gameobject_type)
	{
		switch (gameobject_type)
		{
		case types::game_object_t::NonUpdateable:
		case types::game_object_t::Updateable:
		case types::game_object_t::TrafficSignGameObject:
		case types::game_object_t::TrafficControlGameObject:
		case types::game_object_t::SignageGameObject:
			return(true);
		case types::game_object_t::NoOwner: // *all filtered types even NoOwner types (eg.) lampposts)
			return(true);
		default:
			break;
		}

		return(false);
	}

	template<typename T>
	static void BufferDynamicModelInstance(uint32_t const hash, world::model_state const& world_model_state, T const& iter, vector<model_state_instance_dynamic>& data_models, vector<model_root_index>& data_rootIndex, vector<uint8_t>& data_gameobjects) {

		// verify good
		// exists
		if (iter->second) {

			uint32_t const gameobject_type = iter->second->getOwnerGameObjectType();
			// all filtered types *even NoOwner types (eg.) lampposts)
			if (!filter_dynamic(gameobject_type)) // only filtered
				return;

			auto const& iterFind = world_model_state.hshVoxelModelRootIndex.find(hash);
			if (world_model_state.hshVoxelModelRootIndex.cend() != iterFind) {

				data_models.emplace_back(hash, gameobject_type, iter->second->getModel().identity());
				data_rootIndex.emplace_back(hash, iterFind->second); // add to buffer of saved root indices

				// *set additional varying data
				data_models.back().elevation = iter->second->getElevation();
				XMStoreFloat2(&data_models.back().location, iter->second->getLocation());
				XMStoreFloat3(&data_models.back().azimuth, iter->second->getAzimuth().data());
				XMStoreFloat3(&data_models.back().pitch, iter->second->getPitch().data());

				// set gameobject specific data
				if (types::game_object_t::NoOwner != gameobject_type) {
					BufferGameObject<Volumetric::voxelModelInstance_Dynamic>(hash, iter->second, data_gameobjects);
				}
			}
		}
	}

	void cVoxelWorld::SaveWorld()
	{
		std::string_view const szCityName(MinCity::getCityName());

		fs::path savePath(MinCity::getUserFolder());
		savePath += SAVE_DIR;

		// make sure SAVE_DIR directory exists //
		if (!std::filesystem::exists(savePath)) {
			std::filesystem::create_directory(savePath);
		}

		savePath += szCityName;
		savePath += CITY_EXT;

		FILE* __restrict stream(nullptr);
		if ((0 == _wfopen_s(&stream, savePath.c_str(), L"wbS")) && stream) {

			MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(10));

			// structured binding
			auto const [snapshot, voxel_count] = GridSnapshot();

			MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(20));

			size_t const gridSz(sizeof(Iso::Voxel) * voxel_count);

			// Determine safe buffer sizes
			size_t const compress_safe_size = density_compress_safe_size(gridSz);

			uint8_t* __restrict outCompressed((uint8_t * __restrict)scalable_malloc(compress_safe_size));

			density_processing_result const result = density_compress((uint8_t* const __restrict)&snapshot[0], gridSz, outCompressed, compress_safe_size, DENSITY_ALGORITHM_CHAMELEON);

			if (!result.state) {

				MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(50));

				//write file header
				voxelWorldDesc const header{ { 'C', '1', 'T', 'Y' }, GetSecureSeed(), uint32_t(szCityName.length()), result.bytesWritten, voxel_count };

				_fwrite_nolock(&header, sizeof(voxelWorldDesc), 1, stream);

				// write city name
				_fwrite_nolock(szCityName.data(), sizeof(szCityName.data()[0]), szCityName.length(), stream);

				// write city info
				_fwrite_nolock(&MinCity::City->getInfo(), sizeof(CityInfo), 1, stream);

				// reserve space needed for offscreen image capture
				constexpr uint32_t const offscreen_image_size(offscreen_thumbnail_width * offscreen_thumbnail_height * sizeof(uint32_t));

				auto const offscreen_image_start(_ftell_nolock(stream));
				for (uint32_t i = 0; i < offscreen_image_size; ++i) {
					_putc_nolock(0, stream); // clearing reserved space
				}

				// write the grid
				_fwrite_nolock(&outCompressed[0], sizeof(outCompressed[0]), header.grid_compressed_size, stream);

				MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(70));
				// parse all grid voxels looking for voxelmodel instances
				// match the hash id for found root voxels to voxelmodel
				// write the relation between hash id and the corresponding voxel model identity

				auto const world_model_state(MinCity::VoxelWorld.download_model_state());

				vector<uint8_t> data_gameobjects;
				{
					vector<model_root_index> data_rootIndex;

					{ // do all static //
						vector<model_state_instance_static> data_models_static;
						for (mapVoxelModelInstancesStatic::const_iterator iter = world_model_state.hshVoxelModelInstances_Static.cbegin(); iter != world_model_state.hshVoxelModelInstances_Static.cend(); ++iter) {

							BufferStaticModelInstance(iter->first, world_model_state, iter, data_models_static, data_rootIndex, data_gameobjects);
						}

						// write the static model instances and associated game objects
						size_t const count(data_models_static.size());
						_fwrite_nolock(&count, sizeof(size_t), 1, stream);
						_fwrite_nolock(data_models_static.data(), sizeof(model_state_instance_static), count, stream);
					}

					{ // do required dynamic //
						vector<model_state_instance_dynamic> data_models_dynamic;
						for (mapVoxelModelInstancesDynamic::const_iterator iter = world_model_state.hshVoxelModelInstances_Dynamic.cbegin(); iter != world_model_state.hshVoxelModelInstances_Dynamic.cend(); ++iter) {

							BufferDynamicModelInstance(iter->first, world_model_state, iter, data_models_dynamic, data_rootIndex, data_gameobjects);
						}

						// write the dynamic model instances and associated game objects
						size_t const count(data_models_dynamic.size());
						_fwrite_nolock(&count, sizeof(size_t), 1, stream);
						_fwrite_nolock(data_models_dynamic.data(), sizeof(model_state_instance_dynamic), count, stream);
					}

					// finally write finished model root indices
					size_t const count(data_rootIndex.size());
					_fwrite_nolock(&count, sizeof(size_t), 1, stream);
					_fwrite_nolock(data_rootIndex.data(), sizeof(model_root_index), count, stream);
				}

				// *last* write gameobject specific data
				size_t const count(data_gameobjects.size());
				_fwrite_nolock(&count, sizeof(size_t), 1, stream);
				_fwrite_nolock(data_gameobjects.data(), count, 1, stream);

				// write file delimiter (null, 64bytes in length) //
				for (uint32_t i = 0; i < file_delim_zero_count; ++i) {
					_putc_nolock(0, stream);
				}

				/*
				Iso::Voxel const* pVoxels(&snapshot[0]);
				for (size_t voxel = 0; voxel < voxel_count; ++voxel) {

					Iso::Voxel const* const pVoxel = pVoxels;
					++pVoxels;

					if (pVoxel) {
						Iso::Voxel const oVoxel(*pVoxel);

						if (Iso::isOwnerAny(oVoxel)) {

							// skipping ground
							for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {

							}
							//Iso::serialize();
						}
					}
				}
				*/

				// This is the last file save operation always //

				// move file pointer back to reserved offscreen image area
				_fseek_nolock(stream, offscreen_image_start, SEEK_SET);

				point2D_t const frameBufferSz(MinCity::getFramebufferSize());
				Imaging offscreen_image = ImagingNew(eIMAGINGMODE::MODE_BGRX, frameBufferSz.x, frameBufferSz.y);

				// wait until the offscreen capture is copied from gpu
				std::atomic_flag& OffscreenCapturedFlag(MinCity::Vulkan.getOffscreenCopyStatus());
				while( OffscreenCapturedFlag.test_and_set() ) {	// OffscreenCapturedFlag is clear upon copy completion
					_mm_pause();
				}
				// safe to query the data from offscreen buffer
				MinCity::Vulkan.queryOffscreenBuffer((uint32_t * const __restrict)offscreen_image->block);

				// resample to thumbnail size
				Imaging scaled_offscreen_image = ImagingResample(offscreen_image, offscreen_thumbnail_width, offscreen_thumbnail_height, IMAGING_TRANSFORM_BICUBIC);

				// write offscreen image data in reserved area
				_fwrite_nolock(&scaled_offscreen_image->block[0], sizeof(scaled_offscreen_image->block[0]), offscreen_image_size, stream);

				_fclose_nolock(stream);

				ImagingDelete(scaled_offscreen_image);
				ImagingDelete(offscreen_image);

				// done!
				MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(100));
			}

			if (outCompressed) {
				scalable_free(outCompressed);
				outCompressed = nullptr;
			}
			// release mem for the grid copy
			if (snapshot) {
				scalable_free(const_cast<Iso::Voxel*>(snapshot));
			}
		}
	}


} // end ns
