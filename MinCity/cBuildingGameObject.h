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

typedef tbb::enumerable_thread_specific< uint32_t, tbb::cache_aligned_allocator<uint32_t>, tbb::ets_key_per_instance > thread_local_counter; // per thread instance

namespace world
{
	class cBuildingGameObject : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cBuildingGameObject>
	{
	private:
		void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::BuildingGameObject);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		static void UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	public:
		cBuildingGameObject(cBuildingGameObject&& src) noexcept;
		cBuildingGameObject& operator=(cBuildingGameObject&& src) noexcept;
	private:
		Volumetric::voxB::model_volume*							_destroyed;  // ~2MB per building, ouch
		milliseconds											_tLightChangeInterval;
		ImageAnimation*											_videoscreen;

		struct sMutableState {

			typedef struct sWindow
			{
				uint32_t						vxl_index;
				bool							emissive;

				sWindow() = default;
				sWindow(uint32_t const vxl_index_, bool const emissive_)
					: vxl_index(vxl_index_), emissive(emissive_)
				{}
			} Window;

			static constexpr uint32_t const CACHE_SZ = 16;

			std::atomic_flag			_queued_updatable{};
			thread_local_counter		_destroyed_count = 0;
			tbb::atomic<int64_t>		_tCurrentInterval = 0;
			tbb::atomic<uint32_t>		_changedWindowIndex = 0;
			Window						_changedWindows[CACHE_SZ];
			
		}* _MutableState;

		static tbb::concurrent_queue<cBuildingGameObject*>		_updateable;
		
	public:
		cBuildingGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
		~cBuildingGameObject();
	};

	STATIC_INLINE_PURE void swap(cBuildingGameObject& __restrict left, cBuildingGameObject& __restrict right) noexcept
	{
		cBuildingGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}


 } // end ns


