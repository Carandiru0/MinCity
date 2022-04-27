#pragma once
#include "cProceduralGameObject.h"
#include <Utility/type_colony.h>

#include <Utility/bit_volume.h>

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

	class cLevelSetGameObject : public tProceduralGameObject<Volumetric::voxelModelInstance_Dynamic, Volumetric::voxelModel_Dynamic>, public type_colony<cLevelSetGameObject>
	{
		using local_volume = bit_volume<Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ>;

	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::NonSaveable);
		}
		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;
		
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	private:
		__inline __declspec(noalias) uint32_t const __vectorcall encode_adjacency(uvec4_v const xmIndex) const;

	private:
		alignas(CACHE_LINE_BYTES) local_volume* __restrict _bits{ nullptr };
		
	public:
		cLevelSetGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_, Volumetric::voxelModel_Dynamic* const& model_);
		cLevelSetGameObject(cLevelSetGameObject&& src) noexcept;
		cLevelSetGameObject& operator=(cLevelSetGameObject&& src) noexcept;
		~cLevelSetGameObject();
	};


	__inline __declspec(noalias) uint32_t const __vectorcall cLevelSetGameObject::encode_adjacency(uvec4_v const xmIndex) const
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
		if (iIndex.x + 1 < Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ) {
			adjacent |= _bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z) << Volumetric::adjacency::right;
		}
		if (iIndex.z - 1 >= 0) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1) << Volumetric::adjacency::front;
		}
		if (iIndex.z + 1 < Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1) << Volumetric::adjacency::back;
		}
		if (iIndex.y + 1 < Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ) {
			adjacent |= _bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z) << Volumetric::adjacency::above;
		}
		return(adjacent);
	}

	STATIC_INLINE_PURE void swap(cLevelSetGameObject& __restrict left, cLevelSetGameObject& __restrict right) noexcept
	{
		cLevelSetGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

} // end ns