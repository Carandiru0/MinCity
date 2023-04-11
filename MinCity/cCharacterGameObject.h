#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "voxelAnim.h"

namespace world
{

	class cCharacterGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cCharacterGameObject>
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::CharacterGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void setElevation(float const elevation);

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cCharacterGameObject(cCharacterGameObject&& src) noexcept;
		cCharacterGameObject& operator=(cCharacterGameObject&& src) noexcept;
	private:
		Volumetric::voxelAnim<true>	_animation;
	public:
		cCharacterGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_);
	};

	STATIC_INLINE_PURE void swap(cCharacterGameObject& __restrict left, cCharacterGameObject& __restrict right) noexcept
	{
		cCharacterGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


