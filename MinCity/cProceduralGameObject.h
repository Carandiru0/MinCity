#pragma once
#include <stdint.h>
#include "tTime.h"
#include "cUpdateableGameObject.h"

namespace world
{
	template<typename TVoxelModelInstance, typename TVoxelModel>
	class tProceduralGameObject : public tUpdateableGameObject<TVoxelModelInstance>
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::Procedural);
		}
	public:
		TVoxelModel* const& getModel() const { return(Model); }

		bool const operator==(tProceduralGameObject const& src) const
		{
			return(tUpdateableGameObject<TVoxelModelInstance>::operator==(src));
		}
		tProceduralGameObject(tProceduralGameObject&& src) noexcept
			: tUpdateableGameObject<TVoxelModelInstance>(std::forward<tProceduralGameObject&&>(src))
		{
			Model = std::move(src.Model);
			src.Model = nullptr;
		}
		tProceduralGameObject& operator=(tProceduralGameObject&& src) noexcept
		{
			tUpdateableGameObject<TVoxelModelInstance>::operator=(std::forward<tProceduralGameObject&&>(src));

			SAFE_DELETE(Model);

			Model = std::move(src.Model);
			src.Model = nullptr;

			return(*this);
		}
	protected:
		TVoxelModel*	Model;
	public:
		tProceduralGameObject(TVoxelModelInstance* const& instance_, TVoxelModel* const& model_)
			: tUpdateableGameObject<TVoxelModelInstance>(instance_), Model(model_)
		{}
		// *** virtual destructor not allowed
		// *** delete Model in leaf implementation (required)
	};

	// base implementation for reference //
	//leaf - do not inherit - inherit/derive from "tProceduralGameObject" instead
	/*
	class cProceduralGameObject : public tProceduralGameObject<Volumetric::voxelModelInstance_Dynamic, Volumetric::voxelModel_Dynamic>, public type_colony<cProceduralGameObject>
	{
	public:
		cProceduralGameObject(cProceduralGameObject&& src) noexcept
			: tProceduralGameObject(std::forward<tProceduralGameObject&&>(src))
		{
			src.free_ownership();
		}
		cProceduralGameObject& operator=(cProceduralGameObject&& src) noexcept
		{
			tProceduralGameObject::operator=(std::forward<tPropceduralGameObject&&>(src));
			src.free_ownership();
			return(*this);
		}

	public:

		cProceduralGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_, Volumetric::voxelModel_Dynamic* const& model_)
			: tProceduralGameObject(instance_, model_)
		{
		}
	};

	STATIC_INLINE_PURE void swap(cProceduralGameObject& __restrict left, cProceduralGameObject& __restrict right) noexcept
	{
		cProceduralGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}
	*/
} // end ns world


