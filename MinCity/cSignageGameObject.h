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

	class cSignageGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cSignageGameObject>
	{
	public:
		// ALL derivatives of this class must call base function first in overriden methods, and check its return value
		// typedef Volumetric::voxB::voxelState const(* const voxel_event_function)(void* const _this, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState);
		static Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

	public:
		cSignageGameObject(cSignageGameObject&& src) noexcept;
		cSignageGameObject& operator=(cSignageGameObject&& src) noexcept;
	private:
		ImageAnimation*											_videoscreen;
	public:
		cSignageGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cSignageGameObject();
	};

	STATIC_INLINE_PURE void swap(cSignageGameObject& __restrict left, cSignageGameObject& __restrict right) noexcept
	{
		cSignageGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


