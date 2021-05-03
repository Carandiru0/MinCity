#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>

#include "cCopterPropGameObject.h"
#include "cCopterBodyGameObject.h"

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

	class cCopterGameObject : public type_colony<cCopterGameObject>
	{
	public:
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		void releasePart(cCopterBodyGameObject* const& part);
		void releasePart(cCopterPropGameObject* const& part);
	public:
		cCopterGameObject(cCopterGameObject&& src) noexcept
			: 
			_body(std::move(src._body)), _prop(std::move(src._prop))
		{
			src.free_ownership();

			src._body = nullptr;
			src._prop = nullptr;
		}
		cCopterGameObject& operator=(cCopterGameObject&& src) noexcept
		{			
			src.free_ownership();

			_body = std::move(src._body); src._body = nullptr;
			_prop = std::move(src._prop); src._prop = nullptr;

			return(*this);
		}
	private:
		cCopterBodyGameObject*	_body;
		cCopterPropGameObject*	_prop;
	public:
		cCopterGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_body);
	};

	STATIC_INLINE_PURE void swap(cCopterGameObject& __restrict left, cCopterGameObject& __restrict right) noexcept
	{
		cCopterGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


