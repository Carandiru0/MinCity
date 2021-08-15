#pragma once
#include <stdint.h>
#include "tTime.h"
#include "cNonUpdateableGameObject.h"

namespace world
{
	template<typename TVoxelModelInstance>
	class tUpdateableGameObject : public tNonUpdateableGameObject<TVoxelModelInstance>
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::Updateable);
		}
	public:
		bool const operator==(tUpdateableGameObject const& src) const
		{
			return(tNonUpdateableGameObject<TVoxelModelInstance>::operator==(src));
		}
		tUpdateableGameObject(tUpdateableGameObject&& src) noexcept
			: tNonUpdateableGameObject<TVoxelModelInstance>(std::forward<tUpdateableGameObject&&>(src))
		{
		}
		tUpdateableGameObject& operator=(tUpdateableGameObject&& src) noexcept
		{
			tNonUpdateableGameObject<TVoxelModelInstance>::operator=(std::forward<tUpdateableGameObject&&>(src));

			return(*this);
		}
	protected:

	public:
		tUpdateableGameObject(TVoxelModelInstance* const& instance_)
			: tNonUpdateableGameObject<TVoxelModelInstance>(instance_)
		{}
	};

	// base implementation for reference //
	//leaf - do not inherit - inherit/derive from "tUpdateableGameObject" instead
	/*
	class cUpdateableGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cUpdateableGameObject>
	{
	public:
		cUpdateableGameObject(cUpdateableGameObject&& src) noexcept
			: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
		{
			src.free_ownership();
		}
		cUpdateableGameObject& operator=(cUpdateableGameObject&& src) noexcept
		{
			tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
			src.free_ownership();
			return(*this);
		}

	public:

		cUpdateableGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
			: tUpdateableGameObject(instance_)
		{
		}
	};

	STATIC_INLINE_PURE void swap(cUpdateableGameObject& __restrict left, cUpdateableGameObject& __restrict right) noexcept
	{
		cUpdateableGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}
	*/
} // end ns world


