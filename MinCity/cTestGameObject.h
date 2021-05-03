#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

namespace world
{

	class cTestGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cTestGameObject>
	{
	public:
#ifndef NDEBUG
		// every child of this class should override to_string with approprate string
		virtual std::string_view const to_string() const override { return("cTestGameObject"); }
#endif
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cTestGameObject(cTestGameObject&& src) noexcept
			: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
		{
			src.free_ownership();
		}
		cTestGameObject& operator=(cTestGameObject&& src) noexcept
		{
			tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

			src.free_ownership();
			return(*this);
		}
	private:

	public:
		cTestGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cTestGameObject& __restrict left, cTestGameObject& __restrict right) noexcept
	{
		cTestGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


