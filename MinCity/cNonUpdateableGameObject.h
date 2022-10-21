#pragma once
#include <stdint.h>
#include "tTime.h"
#include "MinCity.h"
#include "cVoxelWorld.h"
#include <Utility/type_colony.h>
#include <Utility/alias.h>

namespace world
{
	template<typename TVoxelModelInstance>
	class tNonUpdateableGameObject : private no_copy
	{
	public:
		bool const Validate() const {

			// This is required before using Instance in any case.
			[[unlikely]] if (!Instance.ok())
				return(false);
			else if (Instance->destroyPending())
				return(false);

			return(true);
		}
		TVoxelModelInstance* const getModelInstance() const { return(*Instance); }

		// *mandatory overrides - every child of this class should override
		constexpr virtual types::game_object_t const to_type() const {
			return(types::game_object_t::NonUpdateable);
		}
#ifndef NDEBUG
		std::string_view const to_string() const { return(to_type()._to_string()); }
#endif
		// *optional overrides
		virtual size_t const exportData(vector<uint8_t>& out) const { return(0); };
		virtual void importData(uint8_t const* const in, size_t const size) {};

	public:
		bool const operator==(tNonUpdateableGameObject const& src) const
		{
			return(((uint64_t) &(*this)) == ((uint64_t)&src));
		}
		tNonUpdateableGameObject(tNonUpdateableGameObject&& src) noexcept
			: Instance(std::move(src.Instance))
		{														   
			src.Instance = nullptr;
		}
		tNonUpdateableGameObject& operator=(tNonUpdateableGameObject&& src) noexcept
		{
			Instance = std::move(src.Instance);
			src.Instance = nullptr;

			return(*this);
		}
	protected:
		alias_t<TVoxelModelInstance> Instance;
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


