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

	class cLightGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cLightGameObject>
	{
	public:
		// overrides //
		constexpr virtual types::game_object_t const to_type() const override final {
			return(types::game_object_t::LightGameObject);
		}
		// ALL derivatives of this class must call base function first in overriden methods, and check its return value
		// typedef Volumetric::voxB::voxelState const(* const voxel_event_function)(void* const _this, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState);
		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

	public:
		bool const	isOn() const { return(_State); }

		void		setOn(bool const on) { _State = on; }
		void		setColor(uint32_t const color) { _Color = color; }

	public:
		cLightGameObject(cLightGameObject&& src) noexcept;
		cLightGameObject& operator=(cLightGameObject&& src) noexcept;
	private:
		uint32_t	_Color;
		bool		_State; // on/off
	public:
		cLightGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_);
		~cLightGameObject();
	};

	STATIC_INLINE_PURE void swap(cLightGameObject& __restrict left, cLightGameObject& __restrict right) noexcept
	{
		cLightGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


