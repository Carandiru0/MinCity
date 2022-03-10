#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "ImageAnimation.h"
#include "cLightGameObject.h"

// forward decl
namespace Volumetric
{
	struct ImportProxy;
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	}
}

// these classes are intentionally simple to support a minimal amount of difference between the two types that had to be seperate. (maintainability)
namespace world
{
	class cImportGameObject
	{
	protected:
		static constexpr uint32_t const _numLights = 3;

	public:
		virtual void signal(tTime const& __restrict tNow);

		void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	protected:
		XMFLOAT3A		  _source[_numLights];
		XMFLOAT3A		  _target[_numLights];
		cLightGameObject* _lights[_numLights];
		tTime			  _tStamp;
	public:
		static Volumetric::ImportProxy&		getProxy() { return(_proxy); }
	protected:
		static Volumetric::ImportProxy		_proxy;
	public:
		cImportGameObject();
	};

	class cImportGameObject_Dynamic : public tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cImportGameObject_Dynamic>, public cImportGameObject
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::NonSaveable);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		virtual void signal(tTime const& __restrict tNow) override final;
	public:
		cImportGameObject_Dynamic(cImportGameObject_Dynamic&& src) noexcept;
		cImportGameObject_Dynamic& operator=(cImportGameObject_Dynamic&& src) noexcept;
	private:
		ImageAnimation*						_videoscreen;
	public:
		cImportGameObject_Dynamic(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cImportGameObject_Dynamic();
	};

	STATIC_INLINE_PURE void swap(cImportGameObject_Dynamic& __restrict left, cImportGameObject_Dynamic& __restrict right) noexcept
	{
		cImportGameObject_Dynamic tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

	class cImportGameObject_Static : public tUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cImportGameObject_Static>, public cImportGameObject
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::NonSaveable);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		virtual void signal(tTime const& __restrict tNow) override final;
	public:
		cImportGameObject_Static(cImportGameObject_Static&& src) noexcept;
		cImportGameObject_Static& operator=(cImportGameObject_Static&& src) noexcept;
	private:
		ImageAnimation*						_videoscreen;
	public:
		cImportGameObject_Static(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
		~cImportGameObject_Static();
	};

	STATIC_INLINE_PURE void swap(cImportGameObject_Static& __restrict left, cImportGameObject_Static& __restrict right) noexcept
	{
		cImportGameObject_Static tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

	
 } // end ns


