#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{
	class cAttachableGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cAttachableGameObject>
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::TestGameObject);
		}

	public:
		v2_rotation_t const& __vectorcall getRoll() const { return(_vRoll); }
		v2_rotation_t const& __vectorcall getYaw() const { return(_vYaw); }
		v2_rotation_t const& __vectorcall getPitch() const { return(_vPitch); }

		void __vectorcall setRoll(v2_rotation_t const vRoll) { _vRoll = vRoll; }						// row doesn't affect synchronization
		void __vectorcall setYaw(v2_rotation_t const vYaw) { _vYaw = vYaw; }										// yaw does affect synchronization
		void __vectorcall setPitch(v2_rotation_t const vPit) { _vPitch = vPit; }						// pitch doesn't affect synchronization

		void __vectorcall setRollYawPitch(v2_rotation_t const& vRoll, v2_rotation_t const& vYaw, v2_rotation_t const& vPit) { _vPitch = vPit; _vRoll = vRoll; _vYaw = vYaw; }

		void __vectorcall setOffset(FXMVECTOR xmOffset) { XMStoreFloat3A(&_offset, xmOffset); }

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		void setParent(tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic> const* const parent, FXMVECTOR const xmOffset) { _parent = parent; XMStoreFloat3A(&_offset, xmOffset); }

	public:
		cAttachableGameObject(cAttachableGameObject&& src) noexcept;
		cAttachableGameObject& operator=(cAttachableGameObject&& src) noexcept;

	private:
		tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic> const*		_parent;
		XMFLOAT3A																	_offset;
		v2_rotation_t																_vPitch, _vYaw, _vRoll;

	public:
		cAttachableGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cAttachableGameObject& __restrict left, cAttachableGameObject& __restrict right) noexcept
	{
		cAttachableGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


