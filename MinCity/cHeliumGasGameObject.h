#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "voxelAnim.h"

namespace world
{

	class cHeliumGasGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cHeliumGasGameObject>
	{
		static constexpr XMVECTORF32 const
			GRADIENT_COLOR_VECTOR = { 0.396078f, 0.121568f, 1.0f, 0.0f };  // rgb

	public:
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::ExplosionGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		void setElevation(float const elevation);

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	public:
		cHeliumGasGameObject(cHeliumGasGameObject&& src) noexcept;
		cHeliumGasGameObject& operator=(cHeliumGasGameObject&& src) noexcept;
	private:
		Volumetric::voxelAnim<true>	_animation;
	public:
		cHeliumGasGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_);
	};

	STATIC_INLINE_PURE void swap(cHeliumGasGameObject& __restrict left, cHeliumGasGameObject& __restrict right) noexcept
	{
		cHeliumGasGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


