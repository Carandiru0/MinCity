#pragma once
#include <Math/point2D_t.h>
#include <Math/superfastmath.h>


namespace Volumetric
{
	namespace voxB
	{

		typedef struct voxelScreen
		{
			static constexpr bool const
				MAJOR_AXIS_X = false,
				MAJOR_AXIS_Z = true;

			rect2D_t screen_rect;
			bool major_axis;

			voxelScreen(rect2D_t const& screen_rect_, bool const major_axis_)
				: screen_rect(screen_rect_.v), major_axis(major_axis_)
			{}

			voxelScreen(voxelScreen const& src) noexcept
				: screen_rect(src.screen_rect.v), major_axis(src.major_axis)
			{

			}

			voxelScreen& operator=(voxelScreen const& src) noexcept
			{
				screen_rect.v = src.screen_rect.v;
				major_axis = src.major_axis;

				return(*this);
			}

		} voxelScreen;


	} // end ns
} // end ns
