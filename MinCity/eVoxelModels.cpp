#include "pch.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "eVoxelModels.h"
#include "voxBinary.h"
#include "voxelModel.h"

#include <vector>
#include <Utility/stringconv.h>
#include <tbb/tbb.h>

inline tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC> > _dynamicModels;
inline tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> > _staticModels;

namespace Volumetric
{
	// Parallezing furth the loading of individual VOX models (task per model) only oversaturates the cpu cores
	// the scheduler is maxed when loading a model and culling voxels and generating adjacency
	// static models and dynamic models are loaded in separate tasks already
	// checked, and removing the task per model results in noticably faster loading of all voxel models

	// this function is re-entrant for a group, appending correctly if called in such a way (eg. named files)
	template<bool const DYNAMIC, bool const SINGLE_FILE = false>
	static void LoadModelGroup(std::string_view const file_group, ModelGroup& __restrict groupInfo)
	{
		std::wstring wszFile;
		std::wstring const wsz_file_group(stringconv::s2ws(file_group));

		bool bExists(false);
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
		do {
			if constexpr (SINGLE_FILE) {
				wszFile = wsz_file_group;
			}
			else {
				wszFile = fmt::format(FMT_STRING(L"{}{:03d}"), wsz_file_group, modelCount);
			}

			voxModel* __restrict pVox;
			
			if constexpr(DYNAMIC) {
				pVox = &(*_dynamicModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}
			else {
				pVox = &(*_staticModels.emplace_back(voxModel(voxIdent{ groupInfo.modelID, modelCount })));
			}

			if ( (bExists = voxB::LoadVOX(wszFile, pVox)) ) {

				voxB::AddEmissiveVOX(wszFile, pVox); // optional
				voxB::AddTransparentVOX(wszFile, pVox); // optional
				voxB::AddVideoscreenVOX(wszFile, pVox); // optional
				++modelCount;

				if constexpr (SINGLE_FILE) {
					break; // done jump out of loop
				}
			}

		} while (bExists);

		// update the count
		groupInfo.size += modelCount;
	}

	template<bool const DYNAMIC>
	static void LoadModelNamed(std::string_view const file_name_no_extension) {

		if constexpr (DYNAMIC) {

			LoadModelGroup<DYNAMIC, true>(file_name_no_extension, isolated_group::DynamicNamed);

		}
		else { // STATIC

			LoadModelGroup<DYNAMIC, true>(file_name_no_extension, isolated_group::StaticNamed);
		}
	}

	static bool LoadAllStaticVoxelModels() // #### Same Order #### //// STATIC
	{
		static constexpr bool const STATIC = false;

		bool Success(true);

		_staticModels.reserve(999);

		LoadModelGroup<STATIC>(FILE_STATIC_EMPTY, isolated_group::StaticEmpty);
		LoadModelGroup<STATIC>(FILE_BUILDING_RESIDENTIAL, isolated_group::Residential);
		LoadModelGroup<STATIC>(FILE_BUILDING_COMMERCIAL, isolated_group::Commercial);
		LoadModelGroup<STATIC>(FILE_BUILDING_INDUSTRIAL, isolated_group::Industrial);

#ifdef GIF_MODE
		LoadModelNamed<STATIC>("rock_stage/rock_stage");
#endif

		// last!
		LoadModelGroup<STATIC>(FILE_STATIC_MISC, isolated_group::StaticMisc); // last

		return(Success);
	}
	static bool LoadAllDynamicVoxelModels() // #### Same Order #### //// DYNAMIC
	{
		static constexpr bool const DYNAMIC = true;

		bool Success(true);

		_dynamicModels.reserve(999);

		LoadModelGroup<DYNAMIC>(FILE_DYNAMIC_EMPTY, isolated_group::DynamicEmpty);
		LoadModelGroup<DYNAMIC>(FILE_DYNAMIC_CARS, isolated_group::DynamicCars);

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
		LoadModelGroup<DYNAMIC>(FILE_DYNAMIC_MISC, isolated_group::DynamicMisc); // last

		// *** apply special functionality to specific models here *** //
		Volumetric::voxB::ApplyAllTransparent((Volumetric::voxB::voxelModelBase*)Volumetric::getVoxelModel<eVoxelModels_Dynamic::MISC>(eVoxelModels_Indices::HOLOGRAM_GIRL));
		Volumetric::voxB::ApplyAllTransparent((Volumetric::voxB::voxelModelBase*)Volumetric::getVoxelModel<eVoxelModels_Dynamic::MISC>(eVoxelModels_Indices::HOLOGRAM_GIRL2));

		/*
		{
			Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC>& __restrict rVox = _dynamicModels.emplace_back(Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC>());

			if (voxB::LoadVOX(L"" FILE_DEPTHCUBE, &rVox)) {

				voxB::AddEmissiveVOX(L"" FILE_DEPTHCUBE, &rVox); // optional
			}
			else {
				Success = false;
			}
		}*/
		/*
		if (voxB::LoadVOX(L"" FILE_SR_71_BLACKBIRD, &_dynamicModels[eVoxelModels_Dynamic::SR_71_BLACKBIRD])) {
			++Success;
		}

		if (voxB::LoadVOX(L"" FILE_HARRIER_JET, &_dynamicModels[eVoxelModels_Dynamic::HARRIER_JET])) {
			++Success;
		}
		*/


		return(Success);
	}

	
	bool const LoadAllVoxelModels()
	{
		bool bSuccess[2]{ false, false };

		FMT_LOG(VOX_LOG, "loading voxel models.....");

		tbb::task_group tG;

		tG.run([&bSuccess] {
			bSuccess[0] = LoadAllStaticVoxelModels();
		});
		tG.run([&bSuccess] {
			bSuccess[1] = LoadAllDynamicVoxelModels();
		});

		tG.wait();

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

	/*void UpdateAllVoxelModels(tTime const& tNow)
	{
		UpdateAllVoxelAnimations(tNow);
	}*/
	void CleanUpAllVoxelModels()
	{
		}
}