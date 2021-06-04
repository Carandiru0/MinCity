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
#define FILE_STATIC_EMPTY "empty__" 
#define FILE_BUILDING_RESIDENTIAL "res__"
#define FILE_BUILDING_COMMERCIAL "com__"
#define FILE_BUILDING_INDUSTRIAL "ind__"
#define FILE_STATIC_MISC "static_misc__" // last

// #### Same Order #### //// DYNAMIC
#define FILE_DYNAMIC_EMPTY "empty__" 
#define FILE_DYNAMIC_CARS "car__"
#define FILE_DYNAMIC_MISC "dynamic_misc__" // last

#define FILE_SR_71_BLACKBIRD "sr71"
#define FILE_HARRIER_JET "harrier"

extern tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::DYNAMIC> > _dynamicModels;
extern tbb::concurrent_vector< Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> > _staticModels;

namespace Volumetric
{
	// groups //

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
	BETTER_ENUM(eVoxelModels_Indices, uint32_t const,

		// ** static misc ** 
		EMPTY = 0,


		// ** static named **
		ROCK_STAGE = 0,


		// ** dynamic cars **
		POLICE_CAR = 0, // from group CARS - should be the very first car file as index is used in a special way


		// ** dynamic misc **
		DEPTH_CUBE = 0,
		HOLOGRAM_GIRL = 1,
		HOLOGRAM_GIRL2 = 2,
		VOODOO_SKULL = 3,
		LAMP_POST = 4,
		ROAD_SIGN = 5,
		TRAFFIC_SIGN = 6,
		COPTER_PROP = 7,
		COPTER_BODY = 8,
		GIF_LIGHT = 9,


		// ** dynamic named **
		GUITAR = 0,
		SINGER = 1,
		MUSICIAN = 2,
		LIGHT = 3,
		CROWD = 4 // **** NOTE THERE ARE 7 CROWD VOXEL MODELS INCLUDING THIS ONE AFTERWARDS


	);

	bool const LoadAllVoxelModels();

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