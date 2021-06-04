#pragma once

// ** generic simple game object where update can be customized by a static function, no derived class neccessary ** //
// ***************************************************************************************************************** //

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{
	// ** Location is swizzled, so that elevation is in y component ** //
	typedef std::pair<XMVECTOR const, v2_rotation_t const> const(*remote_update_function)(XMVECTOR, v2_rotation_t, tTime const& __restrict, fp_seconds const& __restrict, uint32_t const);

	class cRemoteUpdateGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cRemoteUpdateGameObject>
	{
	public:
#ifndef NDEBUG
		// every child of this class should override to_string with approprate string
		virtual std::string_view const to_string() const override { return("cRemoteUpdateGameObject"); }
#endif
		//static Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		//Volumetric::voxB::voxelState const OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

		void setUpdateFunction(remote_update_function const eventHandler) { _eOnUpdate = eventHandler; }

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cRemoteUpdateGameObject(cRemoteUpdateGameObject&& src) noexcept;
		cRemoteUpdateGameObject& operator=(cRemoteUpdateGameObject&& src) noexcept;
	private:
		remote_update_function		_eOnUpdate;
	public:
		cRemoteUpdateGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cRemoteUpdateGameObject& __restrict left, cRemoteUpdateGameObject& __restrict right) noexcept
	{
		cRemoteUpdateGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


