#pragma once

#include "globals.h"
#include "betterenums.h"
#include "voxelModel.h"
#include "tTime.h"

#if !defined(NDEBUG) // assert_print only available in debug builds only
#include <Utility/assert_print.h>
#endif


#define STACKED true
#define FILE_WILD_VIDEO L"_video"
#define FILE_WILD_EMISSIVE L"_emissive"
#define FILE_WILD_TRANSPARENT L"_trans"

// #### Same Order #### //// STATIC
#define FILE_STATIC_EMPTY "empty" 
#define FOLDER_BUILDING_RESIDENTIAL "residential"
#define FOLDER_BUILDING_COMMERCIAL "commercial"
#define FOLDER_BUILDING_INDUSTRIAL "industrial"
#define FOLDER_STATIC_MISC "static_misc" // last

// #### Same Order #### //// DYNAMIC
#define FILE_DYNAMIC_EMPTY FILE_STATIC_EMPTY 
#define FOLDER_DYNAMIC_CARS "cars"
#define FOLDER_DYNAMIC_MISC "dynamic_misc" // last

#define FOLDER_NAMED "named"

extern tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC> > _dynamicModels;
extern tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> > _staticModels;

namespace Volumetric
{
	typedef struct newVoxelModel
	{
		std::string				   name;
		voxB::voxelModelBase*	   model = nullptr;
		bool					   dynamic;

		newVoxelModel() = default;
		newVoxelModel(std::string_view const name_, voxB::voxelModelBase* const model_, bool const dynamic_)
			: name(name_), model(model_), dynamic(dynamic_)
		{}

	} newVoxelModel;

	// groups // *if groups are added, getModelGroupFromModelGroupID() must be updated

	// #### Same Order #### //// STATIC
	BETTER_ENUM(eVoxelModels_Static, int32_t const,
		
		// *** must be positive values
		EMPTY = 0,
		BUILDING_RESIDENTAL,
		BUILDING_COMMERCIAL,
		BUILDING_INDUSTRIAL,

		NAMED,
		MISC // last
	);

	// #### Same Order #### //// DYNAMIC
	BETTER_ENUM(eVoxelModels_Dynamic, int32_t const,
		
		// *** must be negative values
		EMPTY = -1,
		CARS = -2,

		NAMED = -3,
		MISC = -4 // last
	);

	// #### No particular Order #### //// DYNAMIC or STATIC  -- saved indices for models index that are "special", index must match as loaded for the *group* it belongs to.
	struct eVoxelModel
	{
		struct STATIC
		{
			struct MISC // ** static misc **
			{
				static inline uint32_t

					EMPTY = 0;
			};
			struct NAMED // ** static named **
			{
				static inline uint32_t

					RESERVED = 0;
			};
		};

		struct DYNAMIC
		{
			struct CARS // ** dynamic cars **
			{
				static inline uint32_t

					POLICE = 0;  // from group CARS - should be the very first car file as index is used in a special way
			};
			struct MISC // ** dynamic misc **
			{
				static inline uint32_t

					EMPTY = 0,
					LAMP_POST = 4,
					ROAD_SIGN = 5,
					TRAFFIC_SIGN = 6,
					COPTER_PROP = 7,
					COPTER_BODY = 8,
					LIGHT_X1 = 12,
					LIGHT_X3 = 13,
					LIGHT_X5 = 14,
					LIGHT_X7 = 15,
					LIGHT_X9 = 16,
					LIGHT_X64 = 17;
			};
			struct NAMED // ** dynamic named **
			{
				static inline uint32_t

					RESERVED = 0;
			};
		};
	};

	bool const LoadAllVoxelModels();

	bool const isNewModelQueueEmpty();

	tbb::concurrent_queue< Volumetric::newVoxelModel >& getNewModelQueue();

	void CleanUpAllVoxelModels();

	


	// internal usage structure only //
	typedef struct sModelGroup
	{
		int32_t const modelID;
		uint32_t offset;
		uint32_t size;

		sModelGroup(int32_t const modelID_)
			: modelID(modelID_), offset(0), size(0)
		{}

	} ModelGroup;

	// default templates - do not call see specializations below
	template<int32_t const eVoxelModelGroupID>
	STATIC_INLINE auto const* const __restrict getVoxelModel(uint32_t const index) {
#ifndef NDEBUG
		assert_print(0, "getVoxelModel called with unknown model group ID");
#endif
		return(nullptr);
	}
	template<int32_t const eVoxelModelGroupID>
	STATIC_INLINE uint32_t const getVoxelModelCount() {
#ifndef NDEBUG
		assert_print(0, "getVoxelModelCount called with unknown model group ID");
#endif
		return(0);
	}

	// specializations //
	namespace isolated_group {
		inline ModelGroup
			/* static groups */
			StaticEmpty(eVoxelModels_Static::EMPTY),
			Residential(eVoxelModels_Static::BUILDING_RESIDENTAL),
			Commercial(eVoxelModels_Static::BUILDING_COMMERCIAL),
			Industrial(eVoxelModels_Static::BUILDING_INDUSTRIAL),
			StaticNamed(eVoxelModels_Static::NAMED),
			StaticMisc(eVoxelModels_Static::MISC), // last

			/* dynamic groups */
			DynamicEmpty(eVoxelModels_Dynamic::EMPTY),
			DynamicCars(eVoxelModels_Dynamic::CARS),
			DynamicNamed(eVoxelModels_Dynamic::NAMED),
			DynamicMisc(eVoxelModels_Dynamic::MISC); // last
	} // end ns

	// helper //
	STATIC_INLINE ModelGroup const* const getModelGroupFromModelGroupID(int32_t const modelGroupID) {

		if (modelGroupID < 0) { // dynamic

			switch (modelGroupID) {

			case eVoxelModels_Dynamic::EMPTY:
				return(&isolated_group::DynamicEmpty);
			case eVoxelModels_Dynamic::CARS:
				return(&isolated_group::DynamicCars);
			case eVoxelModels_Dynamic::NAMED:
				return(&isolated_group::DynamicNamed);
			case eVoxelModels_Dynamic::MISC: // last
				return(&isolated_group::DynamicMisc);
			}
		}
		else { // static 

			switch (modelGroupID) {

			case eVoxelModels_Static::EMPTY:
				return(&isolated_group::StaticEmpty);
			case eVoxelModels_Static::BUILDING_RESIDENTAL:
				return(&isolated_group::Residential);
			case eVoxelModels_Static::BUILDING_COMMERCIAL:
				return(&isolated_group::Commercial);
			case eVoxelModels_Static::BUILDING_INDUSTRIAL:
				return(&isolated_group::Industrial);
			case eVoxelModels_Static::NAMED:
				return(&isolated_group::StaticNamed);
			case eVoxelModels_Static::MISC: // last
				return(&isolated_group::StaticMisc);
			}
		}

		return(nullptr); // *return must be checked by caller
	}

	// slower, general purpose //
	template<bool const Dynamic>
	STATIC_INLINE auto const* const __restrict getVoxelModel(int32_t const modelGroupID, uint32_t const index) {

		ModelGroup const* const pModelGroup(getModelGroupFromModelGroupID(modelGroupID));

		if (pModelGroup) {

			if constexpr (Dynamic) { // dynamic
				return(&_dynamicModels[pModelGroup->offset + index]);
			}
			else { // static
				return(&_staticModels[pModelGroup->offset + index]);
			}
		}
		
		// not valid/found ?
		if constexpr (Dynamic) { // dynamic
			return((Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC> const* const)nullptr); // *return must be checked by caller
		}
		else {
			return((Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const* const)nullptr);
		}
	}

	// static specializations //
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::EMPTY>(uint32_t const index) {
		return(&_staticModels[isolated_group::StaticEmpty.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::EMPTY>() {
		return(isolated_group::StaticEmpty.size);
	}
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::BUILDING_RESIDENTAL>(uint32_t const index) {
		return(&_staticModels[isolated_group::Residential.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::BUILDING_RESIDENTAL>() {
		return(isolated_group::Residential.size);
	}
	
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::BUILDING_COMMERCIAL>(uint32_t const index) {
		return(&_staticModels[isolated_group::Commercial.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::BUILDING_COMMERCIAL>() {
		return(isolated_group::Commercial.size);
	}
	
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::BUILDING_INDUSTRIAL>(uint32_t const index) {
		return(&_staticModels[isolated_group::Industrial.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::BUILDING_INDUSTRIAL>() {
		return(isolated_group::Industrial.size);
	}

	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::NAMED>(uint32_t const index) {
		return(&_staticModels[isolated_group::StaticNamed.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::NAMED>() {
		return(isolated_group::StaticNamed.size);
	}

	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Static::MISC>(uint32_t const index) {
		return(&_staticModels[isolated_group::StaticMisc.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Static::MISC>() {
		return(isolated_group::StaticMisc.size);
	}

	// dynamic specializations //
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Dynamic::EMPTY>(uint32_t const index) {
		return(&_dynamicModels[isolated_group::DynamicEmpty.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Dynamic::EMPTY>() {
		return(isolated_group::DynamicEmpty.size);
	}
	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Dynamic::CARS>(uint32_t const index) {
		return(&_dynamicModels[isolated_group::DynamicCars.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Dynamic::CARS>() {
		return(isolated_group::DynamicCars.size);
	}

	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Dynamic::NAMED>(uint32_t const index) {
		return(&_dynamicModels[isolated_group::DynamicNamed.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Dynamic::NAMED>() {
		return(isolated_group::DynamicNamed.size);
	}

	template<>
	STATIC_INLINE auto const* const __restrict getVoxelModel<eVoxelModels_Dynamic::MISC>(uint32_t const index) {
		return(&_dynamicModels[isolated_group::DynamicMisc.offset + index]);
	}
	template<>
	STATIC_INLINE uint32_t const getVoxelModelCount<eVoxelModels_Dynamic::MISC>() {
		return(isolated_group::DynamicMisc.size);
	}

} // end ns