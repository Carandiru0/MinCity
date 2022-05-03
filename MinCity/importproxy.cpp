#include "pch.h"
#include "globals.h"
#include "cImportGameObject.h"
#include "importproxy.h"
#include <filesystem>
#include <Utility/stringconv.h>
#include "voxBinary.h"
#include "IsoVoxel.h"

namespace fs = std::filesystem;

namespace Volumetric
{
	void ImportProxy::reset(world::cImportGameObject* const game_object_) {

		game_object = game_object_;
		model = nullptr;

		colors.clear(); previous = colors.end();

		active_color.color = 0; // this clears the material bits aswell
		active_color.count = 0;
		inv_start_color_count = 0.0f;
	}
	void ImportProxy::apply_material(colormap::iterator const& iter_current_color)
	{
		if (colors.end() != iter_current_color) {

			uvec4_v mini(Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ, Volumetric::MODEL_MAX_DIMENSION_XYZ),
				    maxi(0, 0, 0);

			uint32_t numEmissive(0), numTransparent(0); // must be updating the whole count

			ImportMaterial const material(iter_current_color->material);
			
			uint32_t const numVoxels(model->_numVoxels);
			Volumetric::voxB::voxelDescPacked* pVoxels(model->_Voxels);

			for (uint32_t i = 0; i < numVoxels; ++i) {

				if (material.Color == pVoxels->Color) { // safe 24bit comparison, not affected by material bits soi this is the only way to compare ther color correctly.
					pVoxels->RGBM = material.RGBM;
					pVoxels->Hidden = false; // always false here on output

					__m128i const xmPosition(pVoxels->getPosition());
					mini.v = SFM::min(mini.v, xmPosition);
					maxi.v = SFM::max(maxi.v, xmPosition);
				}

				numEmissive += (uint32_t)pVoxels->Emissive;
				numTransparent += (uint32_t)pVoxels->Transparent;

				++pVoxels;
			}

			// update the emissive and transparent counts of the model
			model->_numVoxelsEmissive = numEmissive;
			model->_numVoxelsTransparent = numTransparent;

			if (material.Video) {
				uvec4_t cube;
				uvec4_v(_mm_sub_epi32(maxi.v, mini.v)).xyzw(cube);
				
				uvec4_t minimum, maximum;
				mini.xyzw(minimum);
				maxi.xyzw(maximum);

				if (model->_Features.videoscreen) { // always reset
					delete model->_Features.videoscreen;
					model->_Features.videoscreen = nullptr;
				}
				// only screens that are xz or zx orientation are acceptable, supports curved screen - if containing bbox dimensions symmetrical major axis defaults to X axis 
				// find axis with lowest "thickness", flag major axis for screen
				if (cube.x < cube.z) {
					rect2D_t const screen_rect = rect2D_t(minimum.z, minimum.y, maximum.z + 1, maximum.y + 1);
					model->_Features.videoscreen = new Volumetric::voxB::voxelScreen(screen_rect, Volumetric::voxB::voxelScreen::MAJOR_AXIS_Z);
				}
				else {
					rect2D_t const screen_rect = rect2D_t(minimum.x, minimum.y, maximum.x + 1, maximum.y + 1);
					model->_Features.videoscreen = new Volumetric::voxB::voxelScreen(screen_rect, Volumetric::voxB::voxelScreen::MAJOR_AXIS_X);
				}
				
				// now the game object itself must be notified of the change and enable the videoscreen.
				game_object->OnVideoScreen(true);
			}
			else { // model previously had a video screen, but now the material doesn't
				if (model->_Features.videoscreen) { // always reset
					
					// now the game object itself must be notified of the change and disable the videoscreen.
					game_object->OnVideoScreen(false);

					// *bugfix - do not remove from model, so v1x gets saved with the video screen
					// delete model->_Features.videoscreen;
					// model->_Features.videoscreen = nullptr;
				}
			}
			
			active_color = *iter_current_color;
		}
	}
	void ImportProxy::load(world::cImportGameObject* const game_object_, voxB::voxelModelBase const* const model_)
	{
		reset(game_object_);
		model = const_cast<voxB::voxelModelBase* const>(model_); // remember model

		colors.reserve(256);
		colors.clear();

		uvec4_v xmMin(UINT32_MAX, UINT32_MAX, UINT32_MAX, 0),
			    xmMax(0);

		uint32_t const numVoxels(model->_numVoxels);
		Volumetric::voxB::voxelDescPacked const* pVoxels(model->_Voxels); // stream above is still storing, use the cached read of the models' voxels ++
		for (uint32_t i = 0; i < numVoxels; ++i) {

			Volumetric::voxB::voxelDescPacked const voxel(*pVoxels);

			uint32_t const color = voxel.Color;

			auto iter = std::lower_bound(colors.begin(), colors.end(), color);

			auto iter_found(colors.end());
			if (color == iter->color) {
				iter_found = iter;
			}
			else if (color == (iter - 1)->color) {
				iter_found = iter - 1;
			}

			if (colors.cend() == iter_found) { // only if unique
				colors.insert(iter, ImportColor(color)); // will be sorted from lowest color to highest color order from lowest vector index to highest vector index
			}
			else {
				++iter_found->count;
			}

			++pVoxels;
		}
		// all unique colors found

		// convient initialization before voxel model instance is added to scene
		previous = colors.end(); // important to reset
		inv_start_color_count = 1.0f / (float)colors.size();
		apply_material(colors.begin()); // select first color on load
	}
	void ImportProxy::save(std::string_view const name) const
	{
		if (model) {
			
			std::wstring szCachedPathFilename(VOX_CACHE_DIR);
			szCachedPathFilename += fs::path(stringconv::toLower(name)).stem();
			szCachedPathFilename += V1X_FILE_EXT;

			// save / cache that model
			if (SaveV1XCachedFile(szCachedPathFilename, model)) {
				FMT_LOG_OK(VOX_LOG, " < {:s} > cached", stringconv::ws2s(szCachedPathFilename));
			}
			else {
				FMT_LOG_FAIL(VOX_LOG, "unable to cache to .V1X file: {:s}", stringconv::ws2s(szCachedPathFilename));
			}
		}
	}

} // end ns