#pragma once
#include <Math/point2D_t.h>
#include <Math/v2_rotation_t.h>
#include "IsoVoxel.h"

namespace world
{
	static constexpr uint32_t const NUM_DISTINCT_GROUND_HEIGHTS = Iso::NUM_HEIGHT_STEPS - 1; // ***not including / forget about zero***
	static constexpr uint32_t const GROUND_HEIGHT_NOISE_STEP = (UINT8_MAX - 100) / NUM_DISTINCT_GROUND_HEIGHTS;			// is a good value for more "flat" landmass		

	static constexpr uint32_t const TERRAIN_TEXTURE_SZ = Iso::WORLD_GRID_SIZE;		// ** must be a multiple of world grid, 
																					// ** power of 2 and not exceeding 16384

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
	Iso::Voxel const* const __restrict getNeighbour(point2D_t voxelIndex, point2D_t const relativeOffset);
	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict getNeighbourLocal(point2D_t const voxelIndex, point2D_t const relativeOffset);
	
	// World Space (-x,-z) to (X, Z) Coordinates Only - (Camera Origin) - *swizzled*
	XMVECTOR const __vectorcall getOrigin();
	XMVECTOR const __vectorcall getOriginNoFractionalOffset(); // used only in rendering - use getOrigin() instead
	XMVECTOR const __vectorcall getFractionalOffset(); // beware double adding the fractional offset to a transformation that will later be multiplied by the view matrix. the view matrix already contains the fractional offset translation!

	v2_rotation_t const& getAzimuth();

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict __vectorcall getVoxelAt(point2D_t voxelIndex);
	Iso::Voxel const* const __restrict __vectorcall getVoxelAt(FXMVECTOR const Location);

	// Grid Space (0,0) to (X, Y) Coordinates Only
	Iso::Voxel const* const __restrict __vectorcall getVoxelAtLocal(point2D_t const voxelIndex);

	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	bool const __vectorcall setVoxelAt(point2D_t voxelIndex, Iso::Voxel const&& __restrict newData);
	bool const __vectorcall setVoxelAt(FXMVECTOR const Location, Iso::Voxel const&& __restrict newData);
	void __vectorcall setVoxelsAt(rect2D_t voxelArea, Iso::Voxel const&& __restrict voxelReference);

	// Grid Space (0,0) to (X, Y) Coordinates Only
	bool const __vectorcall setVoxelAtLocal(point2D_t const voxelIndex, Iso::Voxel const&& __restrict newData);

	template<bool const Dynamic>
	STATIC_INLINE bool const __vectorcall setVoxelHashAt(point2D_t const voxelIndex, uint32_t const hash);
	void __vectorcall setVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash); // for static only
	void __vectorcall setVoxelsHashAt(rect2D_t const voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR); // for dynamic only

	template<bool const Dynamic>
	STATIC_INLINE bool const __vectorcall resetVoxelHashAt(point2D_t const voxelIndex, uint32_t const hash);
	void __vectorcall resetVoxelsHashAt(rect2D_t voxelArea, uint32_t const hash); // for static only
	void __vectorcall resetVoxelsHashAt(rect2D_t const voxelArea, uint32_t const hash, v2_rotation_t const& __restrict vR); // for dynamic only

	uint32_t const getVoxelsAt_AverageHeight(rect2D_t voxelArea);
	void setVoxelHeightAt(point2D_t const voxelIndex, uint32_t const heightstep);
	void setVoxelsHeightAt(rect2D_t voxelArea, uint32_t const heightstep);
	rect2D_t const voxelArea_grow(rect2D_t const voxelArea, point2D_t const grow);
	void smoothRect(rect2D_t voxelArea);
	// Grid Space (-x,-y) to (X, Y) Coordinates Only
	void __vectorcall recomputeGroundAdjacency(rect2D_t voxelArea);
	void __vectorcall recomputeGroundAdjacency(point2D_t const voxelIndex);

	bool const __vectorcall isVoxelVisible(FXMVECTOR const xmLocation, float const voxelRadius = Iso::VOX_RADIUS); // y (height) coordinate is required, otherwise it's 0.0f    [Iso::VOX_RADIUS] or Iso::MINI_VOX_RADIUS
	bool const __vectorcall isVoxelVisible(point2D_t const voxelIndex); // y (height) coordinate *not* required, automattically set to ground height.  only [Iso::VOX_RADIUS]

	// mini voxels:
	
	// *voxel painting ONLY* (minivoxels)  ***** addVoxel/addLight can only be called with UserInterface->Paint(), or methods invoked inside UserInterface->Paint() *****
	// voxel / light will not persist more than one frame! using these methods
	// *voxel painting ONLY* (minivoxels)  *****
	bool const __vectorcall addVoxel(FXMVECTOR const location, point2D_t const voxelIndex, uint32_t const color, uint32_t const flags = 0);	// color is abgr (rgba backwards)
	bool const __vectorcall addLight(point2D_t const voxelIndex, uint32_t const color, float const height = 0.0f);


	// zoning
	namespace zoning
	{
		void zoneArea(rect2D_t voxelArea, uint32_t const zone_type);
		void dezoneArea(rect2D_t voxelArea);
	}

	// Random & Search //
	point2D_t const __vectorcall getRandomVoxelIndexInArea(rect2D_t const area);
	point2D_t const __vectorcall getRandomVisibleVoxelIndex();
	rect2D_t const __vectorcall  getRandomNonVisibleAreaNear(); // only a step in width/height of visible rect, surrounding non visible area
	point2D_t const __vectorcall getRandomNonVisibleVoxelIndexNear();

	// Roads //
	namespace roads
	{
		bool const __vectorcall searchForClosestRoadNode(rect2D_t const area, point2D_t const voxelIndexStart, point2D_t&& voxelIndexFound);
		bool const __vectorcall searchForClosestRoadEdge(rect2D_t const area, point2D_t const voxelIndexStart, point2D_t&& voxelIndexFound);

		bool const directions_are_perpindicular(uint32_t const encoded_direction_a, uint32_t const encoded_direction_b);
		bool const __vectorcall search_point_for_road(point2D_t const origin);
		bool const __vectorcall search_neighbour_for_road(point2D_t& __restrict found_road_point, point2D_t const origin, point2D_t const offset);
		bool const __vectorcall search_neighbour_for_road(point2D_t const origin, point2D_t const offset);
		point2D_t const __vectorcall search_road_intersect(point2D_t const origin, point2D_t const axis,
														   int32_t const begin, int32_t const end); // begin / end are offsets from origin

	} // end ns world::roads



	// inline functions //
	template<bool const Dynamic>
	STATIC_INLINE bool const __vectorcall setVoxelHashAt(point2D_t voxelIndex, uint32_t const hash)
	{
		Iso::Voxel const* const __restrict pVoxel(getVoxelAt(voxelIndex));

		if (pVoxel) {
			Iso::Voxel oVoxel(*pVoxel);

			uint8_t const index(Iso::getNextAvailableHashIndex<Dynamic>(oVoxel));

			if (0 != index) {
				Iso::setHash(oVoxel, index, hash);

				return(setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel)));
			}
		}

		return(false);
	}

	template<bool const Dynamic>
	STATIC_INLINE bool const __vectorcall resetVoxelHashAt(point2D_t voxelIndex, uint32_t const hash)
	{
		Iso::Voxel const* const __restrict pVoxel(getVoxelAt(voxelIndex));

		if (pVoxel) {
			Iso::Voxel oVoxel(*pVoxel);

			if constexpr (Dynamic) {
				for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {

					if (Iso::getHash(oVoxel, i) == hash) {
						Iso::resetHash(oVoxel, i);
						return(setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel)));
					}
				}
			}
			else {
				Iso::resetHash(oVoxel, Iso::STATIC_HASH);
				return(setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel)));
			}
		}

		return(false);
	}
} // end ns world