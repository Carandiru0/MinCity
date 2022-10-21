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

	class cVideoScreenGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cVideoScreenGameObject>
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::VideoScreenGameObject);
		}

		void setSequence(uint32_t const index);

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

	public:
		cVideoScreenGameObject(cVideoScreenGameObject&& src) noexcept;
		cVideoScreenGameObject& operator=(cVideoScreenGameObject&& src) noexcept;
	private:
		ImageAnimation*											_videoscreen;

	public:
		cVideoScreenGameObject(Volumetric::voxelModelInstance_Static* const& instance_);
		~cVideoScreenGameObject();
	};

	STATIC_INLINE_PURE void swap(cVideoScreenGameObject& __restrict left, cVideoScreenGameObject& __restrict right) noexcept
	{
		cVideoScreenGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


