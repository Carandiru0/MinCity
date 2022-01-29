#pragma once
#include "cProceduralGameObject.h"
#include <Utility/type_colony.h>

#define BIT_VOLUME_ATOMIC
#include <Utility/bit_volume.h>
#undef BIT_VOLUME_ATOMIC

// forward decl
namespace Volumetric
{
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	}

	using voxelModel_Dynamic = voxB::voxelModel<true>;
}

namespace world
{

	class cAutomataGameObject : public tProceduralGameObject<Volumetric::voxelModelInstance_Dynamic, Volumetric::voxelModel_Dynamic>, public type_colony<cAutomataGameObject>
	{
		typedef struct
		{
			uvec4_v								 index;
			Volumetric::voxB::voxelDescPacked	 voxel;
			Volumetric::voxB::voxelState       	 state;

		} Automata;

		using VecAutomata = tbb::enumerable_thread_specific<vector_aligned<Automata, 16>, tbb::cache_aligned_allocator<vector_aligned<Automata, 16>>, tbb::ets_key_per_instance >;
		using volume = bit_volume<Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y, Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z>;

	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::AutomataGameObject);
		}

		static Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index);
		Volumetric::voxB::voxelState const __vectorcall OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;

		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		void setRule(uint32_t const born, uint32_t const die) { _born = born; _die = die; }

	private:
		__inline uint32_t const __vectorcall encode_adjacency(uvec4_v const xmIndex) const;

	private:
		static inline constinit alignas(CACHE_LINE_BYTES) volume* __restrict _bits{ nullptr };	// global scope to all cAutomataGameObject instances.
	
		Volumetric::voxB::voxelState* __restrict _state{ nullptr };

		fp_seconds _accumulator;
		uint32_t _born, _die;
		bool	 _firstUpdate;

	public:
		cAutomataGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_, Volumetric::voxelModel_Dynamic* const& model_);
		cAutomataGameObject(cAutomataGameObject&& src) noexcept;
		cAutomataGameObject& operator=(cAutomataGameObject&& src) noexcept;
		~cAutomataGameObject();
	};


	__inline uint32_t const __vectorcall cAutomataGameObject::encode_adjacency(uvec4_v const xmIndex) const
	{
		ivec4_t iIndex;
		ivec4_v(xmIndex).xyzw(iIndex);

		//BETTER_ENUM(adjacency, uint32_t const,  // matching the same values to voxelModel.h values
		//	left = voxB::BIT_ADJ_LEFT,
		//	right = voxB::BIT_ADJ_RIGHT,
		//	front = voxB::BIT_ADJ_FRONT,
		//	back = voxB::BIT_ADJ_BACK,
		//	above = voxB::BIT_ADJ_ABOVE

		uint32_t adjacent(0);

		if (iIndex.x - 1 >= 0) {
			adjacent |= _bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z) << Volumetric::adjacency::left;
		}
		if (iIndex.x + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X) {
			adjacent |= _bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z) << Volumetric::adjacency::right;
		}
		if (iIndex.z - 1 >= 0) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1) << Volumetric::adjacency::front;
		}
		if (iIndex.z + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1) << Volumetric::adjacency::back;
		}
		if (iIndex.y + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z) << Volumetric::adjacency::above;
		}
		return(adjacent);
	}


	STATIC_INLINE_PURE void swap(cAutomataGameObject& __restrict left, cAutomataGameObject& __restrict right) noexcept
	{
		cAutomataGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

} // end ns