#include "pch.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "eVoxelModels.h"
#include "voxBinary.h"
#include "voxelModel.h"

#include <vector>
#include <filesystem>
#include <Utility/stringconv.h>
#include <tbb/tbb.h>

namespace fs = std::filesystem;

inline tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC> > _dynamicModels;
inline tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> >  _staticModels;

namespace { // local to this file only}

	static inline tbb::concurrent_queue< Volumetric::newVoxelModel > _new_models;

} // end ns

namespace Volumetric
{
	// Parallezing furth the loading of individual VOX models (task per model) only oversaturates the cpu cores
	// the scheduler is maxed when loading a model and culling voxels and generating adjacency
	// static models and dynamic models are loaded in separate tasks already
	// checked, and removing the task per model results in noticably faster loading of all voxel models

	enum eModelFileType
	{
		GROUP_VOX = 0,
		SINGLE_VOX = 1,
		SEQUENCE_VDB = 2
	};
	
	// this function is re-entrant for a group, appending correctly if called in such a way (eg. named files)
	template<bool const DYNAMIC, uint32_t const FILE_TYPE = GROUP_VOX>
	static void LoadModelGroup(std::string_view const folder_group, ModelGroup& __restrict groupInfo)
	{
		std::wstring folder_path(VOX_DIR);

		if constexpr (SEQUENCE_VDB == FILE_TYPE) // sequence operation
		{
			folder_path += stringconv::s2ws(FOLDER_NAMED); // named files must be in the named vox dir. all sequences are named.
			folder_path += L'/';
			folder_path += stringconv::s2ws(folder_group);	// folder_group contains folder name	
			folder_path += L'/';
		}
		else if constexpr (SINGLE_VOX == FILE_TYPE) { // file operation
			folder_path += stringconv::s2ws(FOLDER_NAMED); // named files must be in the named vox dir.
			folder_path += L'/';
			folder_path += stringconv::s2ws(folder_group);	// folder_group contains file name, no extension.
			folder_path += VOX_FILE_EXT;
		}
		else { // folder operation (default)
			folder_path += stringconv::s2ws(folder_group); // folder_group contains folder name
			folder_path += L'/';
		}

		uint32_t modelCount(groupInfo.size); // start with current count

		// record offset once
		if (0 == groupInfo.offset) {
			if constexpr (DYNAMIC) {
				groupInfo.offset = (uint32_t)_dynamicModels.size();
			}
			else {
				groupInfo.offset = (uint32_t)_staticModels.size();
			}
		}

		using voxModel = Volumetric::voxB::voxelModel<DYNAMIC>;
		using voxIdent = Volumetric::voxB::voxelModelIdent<DYNAMIC>;

		if constexpr (SEQUENCE_VDB == FILE_TYPE)
		{
			voxModel* __restrict pVox;

			if constexpr (DYNAMIC) {
				pVox = &(*_dynamicModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}
			else {
				pVox = &(*_staticModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}

			int const exists = voxB::LoadVDB(folder_path, pVox);
			if (exists) {

				// full sequence loaded into one model
				++modelCount;
			}
		}
		else if constexpr (SINGLE_VOX == FILE_TYPE) { // file operation

			voxModel* __restrict pVox;

			if constexpr (DYNAMIC) {
				pVox = &(*_dynamicModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}
			else {
				pVox = &(*_staticModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}

			int const exists = voxB::LoadVOX(folder_path, pVox);
			if (exists) {

				if (exists < 0) { // new vox model detected 
																		// safe up-cast to base type
					_new_models.emplace(newVoxelModel{ fs::path(folder_path).filename().string(), reinterpret_cast<voxB::voxelModelBase*>(pVox), DYNAMIC });  // reference to new dynamic or static model
				}

				++modelCount;
			}
		}
		else { // folder operation (default)
			
			for (auto const& entry : fs::directory_iterator(folder_path)) {

				if (entry.exists() && !entry.is_directory()) {
					if (stringconv::case_insensitive_compare(VOX_FILE_EXT, entry.path().extension().wstring())) // only vox files 
					{
						voxModel* __restrict pVox;

						if constexpr (DYNAMIC) {
							pVox = &(*_dynamicModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
						}
						else {
							pVox = &(*_staticModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
						}

						int const exists = voxB::LoadVOX(entry.path(), pVox);
						if (exists) {

							if (exists < 0) { // new vox model detected 
																					// safe up-cast to base type
								_new_models.emplace(newVoxelModel{ entry.path().filename().string(), reinterpret_cast<voxB::voxelModelBase*>(pVox), DYNAMIC });  // reference to new dynamic or static model
							}

							++modelCount;
						}
					}
				}
			}
		}
		
		// update the count
		groupInfo.size += modelCount;
	}

	template<bool const DYNAMIC>
	static void LoadModelNamed(std::string_view const file_name_no_extension) {

		if constexpr (DYNAMIC) {

			LoadModelGroup<DYNAMIC, SINGLE_VOX>(file_name_no_extension, isolated_group::DynamicNamed);

		}
		else { // STATIC

			LoadModelGroup<DYNAMIC, SINGLE_VOX>(file_name_no_extension, isolated_group::StaticNamed);
		}
	}

	template<bool const DYNAMIC>
	static void LoadModelSequenceNamed(std::string_view const file_name_no_extension) {

		if constexpr (DYNAMIC) {

			LoadModelGroup<DYNAMIC, SEQUENCE_VDB>(file_name_no_extension, isolated_group::DynamicNamed);

		}
		else { // STATIC

			LoadModelGroup<DYNAMIC, SEQUENCE_VDB>(file_name_no_extension, isolated_group::StaticNamed);
		}
	}
	
	static bool LoadAllStaticVoxelModels() // #### Same Order #### //// STATIC
	{
		static constexpr bool const STATIC = false;

		bool Success(true);

		_staticModels.reserve(999);

		LoadModelGroup<STATIC, SINGLE_VOX>(FILE_STATIC_EMPTY, isolated_group::StaticEmpty);
		LoadModelGroup<STATIC>(FOLDER_BUILDING_RESIDENTIAL, isolated_group::Residential);
		LoadModelGroup<STATIC>(FOLDER_BUILDING_COMMERCIAL, isolated_group::Commercial);
		LoadModelGroup<STATIC>(FOLDER_BUILDING_INDUSTRIAL, isolated_group::Industrial);

#ifdef GIF_MODE
		LoadModelNamed<STATIC>("rock_stage/rock_stage");
#endif

		// last!
		LoadModelGroup<STATIC>(FOLDER_STATIC_MISC, isolated_group::StaticMisc); // last

		return(Success);
	}
	static bool LoadAllDynamicVoxelModels() // #### Same Order #### //// DYNAMIC
	{
		static constexpr bool const DYNAMIC = true;

		bool Success(true);

		_dynamicModels.reserve(999);

		LoadModelGroup<DYNAMIC, SINGLE_VOX>(FILE_DYNAMIC_EMPTY, isolated_group::DynamicEmpty);
		LoadModelGroup<DYNAMIC>(FOLDER_DYNAMIC_CARS, isolated_group::DynamicCars);

		LoadModelSequenceNamed<DYNAMIC>("ground_explosion");
#ifdef GIF_MODE
		LoadModelNamed<DYNAMIC>("rock_stage/guitar");
		LoadModelNamed<DYNAMIC>("rock_stage/singer");
		LoadModelNamed<DYNAMIC>("rock_stage/musician");
		LoadModelNamed<DYNAMIC>("rock_stage/light");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__0");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__1");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__2");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__3");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__4");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__5");
		LoadModelNamed<DYNAMIC>("rock_stage/crowd__6");
#endif

		// last!
		LoadModelGroup<DYNAMIC>(FOLDER_DYNAMIC_MISC, isolated_group::DynamicMisc); // last

		return(Success);
	}

	
	bool const LoadAllVoxelModels()
	{
		bool bSuccess[2]{ false, false };

		FMT_LOG(VOX_LOG, "loading voxel models.....");

		bSuccess[0] = LoadAllStaticVoxelModels();
		bSuccess[1] = LoadAllDynamicVoxelModels();

		if (bSuccess[0]) {
			FMT_LOG_OK(VOX_LOG, "static models loaded");
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "static models not all loaded.");
		}
		if (bSuccess[1]) {
			FMT_LOG_OK(VOX_LOG, "dynamic models loaded");
		}
		else {
			FMT_LOG_FAIL(VOX_LOG, "dynamic models not all loaded.");
		}

		return(bSuccess[0] & bSuccess[1]);
	}

	bool const isNewModelQueueEmpty() { return(_new_models.empty()); }

	tbb::concurrent_queue< Volumetric::newVoxelModel >& getNewModelQueue() { return(_new_models); }

	void CleanUpAllVoxelModels()
	{
		// controlled demolition 
		_dynamicModels.clear(); _dynamicModels.shrink_to_fit();
		_staticModels.clear(); _staticModels.shrink_to_fit();
	}
}