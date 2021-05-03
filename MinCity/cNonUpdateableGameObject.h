#pragma once
#include <stdint.h>
#include "tTime.h"
#include "MinCity.h"
#include "cVoxelWorld.h"
#include <Utility/type_colony.h>

namespace world
{
	template<typename TVoxelModelInstance>
	class tNonUpdateableGameObject : private no_copy
	{
	public:
		TVoxelModelInstance* const getModelInstance() const { return(*Instance); }
#ifndef NDEBUG
		// every child of this class should override to_string with approprate string
		constexpr virtual std::string_view const to_string() const { return("tNonUpdateableGameObject"); }
#endif

	public:
		bool const operator==(tNonUpdateableGameObject const& src) const
		{
			return(((uint64_t) &(*this)) == ((uint64_t)&src));
		}
		tNonUpdateableGameObject(tNonUpdateableGameObject&& src) noexcept
		{														   
			Instance = std::move(src.Instance);
			src.Instance = nullptr;
		}
		tNonUpdateableGameObject& operator=(tNonUpdateableGameObject&& src) noexcept
		{
			Instance = std::move(src.Instance);
			src.Instance = nullptr;

			return(*this);
		}
	protected:
		TVoxelModelInstance* const* Instance;
	public:
		tNonUpdateableGameObject(TVoxelModelInstance* const& instance_)
			: Instance(&instance_)
		{}
		// no virtual dtor, not allowed
	};

	// base implementation for reference //
	/*
	//leaf - do not inherit - inherit/derive from "tNonUpdateableGameObject" instead
	class cNonUpdateableGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cNonUpdateableGameObject>
	{
	public:
		cNonUpdateableGameObject(cNonUpdateableGameObject&& src) noexcept
			: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
		{
			src.free_ownership();
		}
		cNonUpdateableGameObject& operator=(cNonUpdateableGameObject&& src) noexcept
		{
			tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
			src.free_ownership();
			return(*this);
		}

	public:

		cNonUpdateableGameObject(Volumetric::voxelModelInstance_Static* const& instance_)
			: tNonUpdateableGameObject(instance_)
		{
		}
	};

	STATIC_INLINE_PURE void swap(cNonUpdateableGameObject& __restrict left, cNonUpdateableGameObject& __restrict right) noexcept
	{
		cNonUpdateableGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}
	*/
} // end ns world


