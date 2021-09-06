#include "pch.h"
#include "cAbstractToolMethods.h"
#include "IsoVoxel.h"
#include "world.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

void __vectorcall cAbstractToolMethods::draw_grid(rect2D_t const highlight_area, int32_t const division_size, int32_t const grid_radius)
{
	constexpr float const step(Iso::MINI_VOX_STEP * 2.0f);

	constexpr uint32_t const
		color_normal(0x007f7f7f), // abgr (rgba backwards)
		color_highlighted(0x0089E944);

	point2D_t const voxelIndexOrigin(MinCity::VoxelWorld.getVisibleGridCenter());// (highlight_area.center());
	rect2D_t const max_area(p2D_subs(voxelIndexOrigin, grid_radius), p2D_adds(voxelIndexOrigin, grid_radius));

	// make relative to world origin (gridspace to worldspace transform)
	XMVECTOR const xmWorldOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOrigin()));

	point2D_t voxelIndex;
	for (voxelIndex.y = max_area.top; voxelIndex.y <= max_area.bottom; voxelIndex.y += division_size) {

		for (voxelIndex.x = max_area.left; voxelIndex.x <= max_area.right; voxelIndex.x += division_size) {

			Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));
			if (pVoxel) {
				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isGroundOnly(*pVoxel)) { // only draw grid on ground

					uint32_t const color((r2D_contains(highlight_area, voxelIndex) ? color_highlighted : color_normal));

					XMVECTOR xmOrigin = p2D_to_v2(voxelIndex);
					xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
					xmOrigin = XMVectorSetY(xmOrigin, -Iso::getRealHeight(oVoxel) - step);
					xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

					world::addVoxel(xmOrigin, voxelIndex, color, Iso::mini::emissive);
					/*
					world::addVoxel(XMVectorAdd(xmOrigin, XMVectorSet(-step, 0.0f, -step, 0.0f)), voxelIndex, color, true);
					world::addVoxel(XMVectorAdd(xmOrigin, XMVectorSet(-step, 0.0f,  step, 0.0f)), voxelIndex, color, true);
					world::addVoxel(XMVectorAdd(xmOrigin, XMVectorSet( step, 0.0f, -step, 0.0f)), voxelIndex, color, true);
					world::addVoxel(XMVectorAdd(xmOrigin, XMVectorSet( step, 0.0f,  step, 0.0f)), voxelIndex, color, true);
					*/

				}
			}
		}
	}

}