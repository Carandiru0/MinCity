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

namespace fs = std::filesystem;

// SAVING
// this file contains the class methods of cVoxelWorld for saving, seperated due to cVoxelWorld.cpp size/length complexity
namespace world
{

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
				voxelWorldDesc const header{ { 'C', '1', 'T', 'Y' }, uint32_t(szCityName.length()), result.bytesWritten, voxel_count };
				
				_fwrite_nolock(&header, sizeof(voxelWorldDesc), 1, stream);

				// write city name
				_fwrite_nolock(szCityName.data(), sizeof(szCityName.data()[0]), szCityName.length(), stream);

				// write city info
				_fwrite_nolock(&MinCity::City->getInfo(), sizeof(CityInfo), 1, stream);

				// reserve space needed for offscreen image capture
				constexpr uint32_t const offscreen_image_size(offscreen_thumbnail_width * offscreen_thumbnail_height * sizeof(uint32_t));

				auto const offscreen_image_start( _ftell_nolock(stream) );
				for (uint32_t i = 0 ; i < offscreen_image_size ; ++i) {
					_putc_nolock(0, stream); // clearing reserved space
				}

				// write the grid
				_fwrite_nolock(&outCompressed[0], sizeof(outCompressed[0]), header.grid_compressed_size, stream);

				MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(70));
				// parse all grid voxels looking for voxelmodel instances
				// match the hash id for found root voxels to voxelmodel
				// write the relation between hash id and the corresponding voxel model identity
				for (size_t voxel = 0; voxel < voxel_count; ++voxel) {

					// todo
				}


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
