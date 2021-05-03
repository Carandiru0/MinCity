#pragma once

#include "cNonUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "ImageAnimation.h"

// forward decl
namespace Volumetric
{
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	}
}

namespace world
{

	class cBuildingGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cBuildingGameObject>
	{
	public:
#ifndef NDEBUG
		// every child of this class should override to_string with approprate string
		virtual std::string_view const to_string() const override { return("cBuildingGameObject"); }
#endif
		static Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

	public:
		cBuildingGameObject(cBuildingGameObject&& src) noexcept;
		cBuildingGameObject& operator=(cBuildingGameObject&& src) noexcept;
	private:
		milliseconds											_tLightChangeInterval;
		ImageAnimation*											_videoscreen;

		struct sMutableState {

			typedef struct sWindow
			{
				uint32_t						vxl_index;
				bool							emissive;

				sWindow() = default;
				sWindow(uint32_t const vxl_index_, bool const emissive_)
					: vxl_index(vxl_index_), emissive(emissive_)
				{}
			} Window;

			static constexpr uint32_t const CACHE_SZ = 16;

			tbb::atomic<int64_t>		_tCurrentInterval;
			tbb::atomic<uint32_t>		_changedWindowIndex;
			Window						_changedWindows[CACHE_SZ];
		}*								_MutableState;
	public:
		cBuildingGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
		~cBuildingGameObject();
	};

	STATIC_INLINE_PURE void swap(cBuildingGameObject& __restrict left, cBuildingGameObject& __restrict right) noexcept
	{
		cBuildingGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


