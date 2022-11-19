#pragma once
#include <Math/point2D_t.h>
#include <Math/v2_rotation_t.h>
#include "IsoVoxel.h"

// forward decl's
namespace Volumetric
{
	class voxelModelInstance_Static;
	class voxelModelInstance_Dynamic;
}

namespace world
{
	static constexpr uint32_t const RESIDENTIAL = 0,
									COMMERCIAL = 1,
									INDUSTRIAL = 2;

	// default ||  R  ||  C  ||  I  ||
	static constexpr uint32_t const ZONING_COLOR[4] = { 0x000000, 0xFA22F8, 0xBD2334, 0xFEFFAC }; // bgr`

#define zERO 0.0f
	read_only inline XMVECTORF32 const WORLD_CENTER{ 0.0f, 0.0f, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_TOP{ 0.0f, -Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_BOTTOM{ 0.0f, Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_LEFT{ -Iso::WORLD_GRID_FHALFSIZE, 0.0f, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_RIGHT{ Iso::WORLD_GRID_FHALFSIZE, 0.0f, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_TL{ -Iso::WORLD_GRID_FHALFSIZE, -Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_TR{ Iso::WORLD_GRID_FHALFSIZE, -Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_BL{ -Iso::WORLD_GRID_FHALFSIZE, Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
	read_only inline XMVECTORF32 const WORLD_BR{ Iso::WORLD_GRID_FHALFSIZE, Iso::WORLD_GRID_FHALFSIZE, zERO, zERO };
#undef zERO

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	point2D_t const getNeighbourLocalVoxelIndex(point2D_t voxelIndex, point2D_t const relativeOffset);
	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const getNeighbour(point2D_t voxelIndex, point2D_t const relativeOffset);
	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const getNeighbourLocal(point2D_t const voxelIndex, point2D_t const relativeOffset);
	
	// World Space (-x,-z) to (X, Z) Coordinates Only - (Camera Origin) - *swizzled*
	XMVECTOR const __vectorcall getOrigin();
	XMVECTOR const __vectorcall getFractionalOffset(); // beware double adding the fractional offset to a transformation

	v2_rotation_t const& getYaw();

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	point2D_t const __vectorcall getLocalVoxelIndexAt(point2D_t voxelIndex);

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const __vectorcall getVoxelAt(point2D_t voxelIndex);
	Iso::Voxel const __vectorcall getVoxelAt(FXMVECTOR const Location);

	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const __vectorcall getVoxelAtLocal(point2D_t const voxelIndex);

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	uint32_t const __vectorcall getVoxelHeightAt(point2D_t voxelIndex);

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	void __vectorcall setVoxelAt(point2D_t voxelIndex, Iso::Voxel const&& __restrict newData);
	void __vectorcall setVoxelAt(FXMVECTOR const Location, Iso::Voxel const&& __restrict newData);
	void __vectorcall setVoxelsAt(rect2D_t voxelArea, Iso::Voxel const&& __restrict voxelReference);

	// Grid Space (0,0) to (X, Y) Coordinates Only
	void __vectorcall setVoxelAtLocal(point2D_t const voxelIndex, Iso::Voxel const&& __restrict newData);

	template<bool const Dynamic>
	STATIC_INLINE void __vectorcall setVoxelHashAt(point2D_t const voxelIndex, uint32_t const hash);
	void __vectorcall setVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash); // for static only
	void __vectorcall setVoxelsHashAt(rect2D_t const voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR);			// for dynamic only
	
	template<bool const Dynamic>
	STATIC_INLINE void __vectorcall resetVoxelHashAt(point2D_t const voxelIndex, uint32_t const hash);
	void __vectorcall resetVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash); // for static only
	void __vectorcall resetVoxelsHashAt(rect2D_t const voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR); // for dynamic only

	int32_t const __vectorcall testVoxelsAt(rect2D_t const voxelArea, v2_rotation_t const& __restrict vR); // ""  dynamic ""

	uint32_t const getVoxelsAt_AverageHeight(rect2D_t voxelArea);
	uint32_t const __vectorcall getVoxelsAt_MaximumHeight(rect2D_t const voxelArea, v2_rotation_t const& __restrict vR); // ""  dynamic ""

	void setVoxelHeightAt(point2D_t const voxelIndex, uint32_t const heightstep);
	void setVoxelsHeightAt(rect2D_t voxelArea, uint32_t const heightstep);
	rect2D_t const voxelArea_grow(rect2D_t const voxelArea, point2D_t const grow);
	void smoothRect(rect2D_t voxelArea);
	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	void __vectorcall recomputeGroundAdjacency(rect2D_t voxelArea);
	void __vectorcall recomputeGroundAdjacency(point2D_t const voxelIndex);

	bool const __vectorcall isVoxelVisible(FXMVECTOR const xmLocation, float const voxelRadius); // y (height) coordinate is required, otherwise it's 0.0f    Volumetric::volumetricVisibility::getVoxelRadius() or Volumetric::volumetricVisibility::getMiniVoxelRadius()
	bool const __vectorcall isVoxelVisible(point2D_t const voxelIndex); // y (height) coordinate *not* required, automattically set to ground height. only Volumetric::volumetricVisibility::getVoxelRadius()

	// zoning
	namespace zoning
	{
		void zoneArea(rect2D_t voxelArea, uint32_t const zone_type);
		void dezoneArea(rect2D_t voxelArea);
	}

	// Random & Search //
	point2D_t const __vectorcall getRandomVoxelIndexInArea(rect2D_t const area);
	point2D_t const __vectorcall getRandomVisibleVoxelIndexInArea(rect2D_t const area);
	point2D_t const __vectorcall getRandomVisibleVoxelIndex();
	rect2D_t const __vectorcall  getRandomNonVisibleAreaNear(); // only a step in width/height of visible rect, surrounding non visible area
	point2D_t const __vectorcall getRandomNonVisibleVoxelIndexNear();

	// intended for private usage by cVoxelWorld
	using mapVoxelModelInstancesStatic = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Static*>;
	using mapVoxelModelInstancesDynamic = tbb::concurrent_unordered_map<uint32_t const, Volumetric::voxelModelInstance_Dynamic*>;

	namespace access
	{
		void create_game_object(uint32_t const hash, uint32_t const gameobject_type, mapVoxelModelInstancesStatic&& __restrict static_instances, mapVoxelModelInstancesDynamic&& __restrict dynamic_instances);
		void update_game_objects(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
		void release_game_objects();
	} // ends ns




	// inline functions //
	template<bool const Dynamic>
	STATIC_INLINE void __vectorcall setVoxelHashAt(point2D_t voxelIndex, uint32_t const hash)
	{
		Iso::Voxel oVoxel(getVoxelAt(voxelIndex));

		uint8_t const index(Iso::getNextAvailableHashIndex<Dynamic>(oVoxel));

		if (0 != index) {
			Iso::setHash(oVoxel, index, hash);

			setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		}
	}

	template<bool const Dynamic>
	STATIC_INLINE void __vectorcall resetVoxelHashAt(point2D_t voxelIndex, uint32_t const hash)
	{
		Iso::Voxel oVoxel(getVoxelAt(voxelIndex));

		if constexpr (Dynamic) {
			for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {

				if (Iso::getHash(oVoxel, i) == hash) {
					Iso::resetHash(oVoxel, i);
					setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}
			}
		}
		else {
			Iso::resetHash(oVoxel, Iso::STATIC_HASH);
			setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		}
	}
} // end ns world