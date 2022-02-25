#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "eDirection.h"
#include <Math/biarc_t.h>

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
	bool const hasStoppedCar(Iso::Voxel const& oVoxel, uint32_t const excludeHash = 0);
	bool const hasCar(Iso::Voxel const& oVoxel, uint32_t const excludeHash = 0);
	bool const __vectorcall hasCar(point2D_t const voxelIndex, uint32_t const excludeHash = 0);
}

BETTER_ENUM(eStateType, uint32_t const,

	// *** must be negative values
	INVALID = 0,
	STRAIGHT,
	XING,
	CORNER
);

#ifndef GAMEOBJECT_T
#define GAMEOBJECT_T cCarGameObject
#endif

namespace world
{
	class cCarGameObject : public world::tUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<class GAMEOBJECT_T>
	{
		static constexpr milliseconds const
			CAR_CREATE_INTERVAL = milliseconds(333),
			CAR_IDLE_MAX = seconds(30); // close to 2 full intersection sequences

		static constexpr uint32_t const
			TILES_PER_CAR = 6;

		static constexpr float const
			MIN_SPEED = 50.0f;

		static constexpr tTime const
			CANCEL_IDLE = tTime{ std::chrono::nanoseconds(1) };

	public:
		static constexpr uint32_t const
			MASK_COLOR_HEADLIGHT = 0xf0f0f0,		//bgra
			MASK_COLOR_TAILLIGHT = 0x2711a4,		//bgra
			MASK_COLOR_LEFTSIGNAL = 0x0e8bff,
			MASK_COLOR_RIGHTSIGNAL = 0x0c79ff,
			MASK_COLOR_PRIMARY = 0xffffff,
			MASK_COLOR_SECONDARY = 0x505050;
	protected:
		typedef struct state {

			uint32_t		type, direction;
			point2D_t		voxelIndex,
							voxelOffsetFromRoadCenter,
							voxelDirection;
			float			distance;
			v2_rotation_t   vAzimuth;
			bool			bTurning;

			state() = default;
			state(state&& src) = default;
			state& operator=(state&& src) = default;

			void deduce_state();
			explicit state(uint32_t const type_, uint32_t const direction_, point2D_t const voxelIndex_); // accepts voxelIndex (ahead of car)
			explicit state(uint32_t const type_, uint32_t const direction_, point2D_t const voxelRoadIndex_, bool const bTurning_); // accepts voxelRoadIndex (center of road)

		} state;

	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::CarGameObject);
		}

		bool const isStopped() const { return(_this.brakes_on); } // bug fix, can't use _this.moving for ccd leaves it as true when the ccd test fails - however brakes on is reliable and consistent

		void __vectorcall OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

		static void UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	protected:
		void setInitialState(state&& initialState);
		template<typename T = cCarGameObject>
		STATIC_INLINE void CreateCar(int32_t carModelIndex = -1);
	private:
		bool const __vectorcall xing(state const& __restrict currentState, state& __restrict targetState, point2D_t const voxelRoadCenterNode, uint32_t const roadNodeType);
		bool const __vectorcall corner(state const& __restrict currentState, state& __restrict targetState, point2D_t const voxelRoadCenterNode, uint32_t const roadNodeType);
		bool const forward(state const& __restrict currentState, state& __restrict targetState, int32_t const distance);
		void __vectorcall gravity(FXMVECTOR const xmLocation, FXMVECTOR const xmAzimuth);	// makes car "hug" road
		bool const __vectorcall ccd(FXMVECTOR const xmLocation, FXMVECTOR const xmAzimuth) const; // continous collision detection (only against other dynamics (cars))
		void idle(tTime const idleStart) { _this.tIdleStart = idleStart; } // for cancelling idle timeout or reseting to zero idle start

		// special 
		STATIC_INLINE_PURE bool const evaluate_spawn_space(Iso::Voxel const& __restrict oVoxel);
		STATIC_INLINE_PURE bool const evaluate_car_space(Iso::Voxel const& __restrict oVoxel);

		template<typename condition_function>
		STATIC_INLINE_PURE bool const __vectorcall checkSpace(point2D_t const voxelIndex, point2D_t const voxelDirection, int32_t const car_half_length, condition_function const func);

	public:
		cCarGameObject(cCarGameObject&& src) noexcept;
		cCarGameObject& operator=(cCarGameObject&& src) noexcept;
	private:
		struct {

			bool			moving,
							left_signal_on,
							right_signal_on,
							brakes_on;

			tTime			tIdleStart;

			uint32_t        primary_color,
							secondary_color;

			state			currentState,
							targetState;

			float			half_length,
							accumulator;

		} _this = {};

		biarc_t				_arcs[2];

	protected:
		float			_speed;
	public:
		cCarGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cCarGameObject();
	};

	STATIC_INLINE_PURE void swap(cCarGameObject& __restrict left, cCarGameObject& __restrict right) noexcept
	{
		cCarGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

	bool const cCarGameObject::evaluate_spawn_space(Iso::Voxel const& __restrict oVoxel)
	{
		if (Iso::isPending(oVoxel)) {
			return(false); // silently fail if the voxel is marked temporary / pending / constructing
		}
		// silently fail if road is a node (xing or corner)
		if (Iso::isRoadNode(oVoxel)) {
			return(false);
		}
		// silently fail if voxel is occupied by a car already
		if (world::hasCar(oVoxel))
			return(false);

		return(true);
	}
	bool const cCarGameObject::evaluate_car_space(Iso::Voxel const& __restrict oVoxel)
	{
		// silently fail if voxel is occupied by a *stopped* car already
		return(!world::hasStoppedCar(oVoxel));
	}

	template<typename condition_function>
	STATIC_INLINE_PURE bool const __vectorcall cCarGameObject::checkSpace(point2D_t const voxelIndex, point2D_t const voxelDirection, int32_t const car_half_length, condition_function const func)
	{
		// voxels to check (forms a straight line)
		point2D_t const voxelAhead(p2D_add(voxelIndex, p2D_muls(voxelDirection, car_half_length))),
			voxelBehind(p2D_sub(voxelIndex, p2D_muls(voxelDirection, car_half_length)));

		// depending on direction, y or x will only iterate once because they are equal (straight line with a width of 1)
		// the opposite of the y or x index will iterate for the car length +- 1

		// traverse voxels in order correctly for the loop
		point2D_t const voxelBegin(p2D_min(voxelAhead, voxelBehind)),
			voxelEnd(p2D_max(voxelAhead, voxelBehind));

		point2D_t voxelSpace;
		for (voxelSpace.y = voxelBegin.y; voxelSpace.y <= voxelEnd.y; ++voxelSpace.y) {

			for (voxelSpace.x = voxelBegin.x; voxelSpace.x <= voxelEnd.x; ++voxelSpace.x) {

				Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelSpace));
				if (pVoxel) {

					Iso::Voxel const oVoxel(*pVoxel);

					if (!func(oVoxel)) {
						return(false);
					}
				}
				else {
					return(false);
				}
			}
		}

		return(true);
	}

	template<typename T>
	STATIC_INLINE void cCarGameObject::CreateCar(int32_t carModelIndex)
	{
		if (0 == MinCity::VoxelWorld->getRoadVisibleCount())
			return;

		rect2D_t const area(MinCity::VoxelWorld->getVisibleGridBounds());

		point2D_t const randomVoxelIndex = world::getRandomVoxelIndexInArea(area);
		point2D_t randomRoadEdgeVoxelIndex;

		if (world::roads::searchForClosestRoadEdge(area, randomVoxelIndex, std::forward<point2D_t&&>(randomRoadEdgeVoxelIndex))) {

			Iso::Voxel const* const pVoxel(world::getVoxelAt(randomRoadEdgeVoxelIndex));
			if (pVoxel) {

				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isPending(oVoxel)) {
					return; // silently fail if the voxel is marked temporary / pending / constructing
				}
				// silently fail if voxel is occupied by a car already
				if (world::hasCar(oVoxel))
					return;

				uint32_t carDirection(0);

				switch (Iso::getRoadDirection(oVoxel))
				{
				case Iso::ROAD_DIRECTION::N:
				case Iso::ROAD_DIRECTION::S:
					carDirection = PsuedoRandom5050() ? eDirection::N : eDirection::S;
					break;

				case Iso::ROAD_DIRECTION::E:
				case Iso::ROAD_DIRECTION::W:
					carDirection = PsuedoRandom5050() ? eDirection::E : eDirection::W;
					break;
				}

				state initialState(eStateType::STRAIGHT, carDirection, randomRoadEdgeVoxelIndex);
				initialState.voxelIndex = p2D_add(initialState.voxelIndex, initialState.voxelOffsetFromRoadCenter);  // offseting from road center

				// Required to check the longest axis, of the car to be created, for existing cars //

				// randomly pick car model that will be used for instance, if one is not already specified
				if (carModelIndex < 0) {
					carModelIndex = PsuedoRandomNumber(1, Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Dynamic::CARS>() - 1); // **** +1 avoids the Police car being placed as a regular car - it's always the first "index" in cars files.
				}
				auto const* const carModel(Volumetric::getVoxelModel<Volumetric::eVoxelModels_Dynamic::CARS>(carModelIndex));
				// determine the length of the car
				rect2D_t const localArea(carModel->_LocalArea);
				float const half_length(float(SFM::max(localArea.width(), localArea.height())) * 0.5f);
				int32_t const car_half_length(SFM::round_to_i32(half_length) + 1); // rounding so length of car is never short, +1 so its one voxel ahead or behind (below)

				if (checkSpace(initialState.voxelIndex, initialState.voxelDirection, car_half_length, evaluate_spawn_space)) {

					// finally attempt creating instance of model previousky selected at random
					using flags = Volumetric::eVoxelModelInstanceFlags;

					T* const pGameObj = MinCity::VoxelWorld->placeUpdateableInstanceAt<T, Volumetric::eVoxelModels_Dynamic::CARS>(
						initialState.voxelIndex, carModelIndex,
						flags::INSTANT_CREATION);  // instant creation must be used, as any delay would invalidate the current collision test above

					if (pGameObj) {
						(*(pGameObj->Instance))->setTransparency(Volumetric::eVoxelTransparency::ALPHA_100); // change transparency on cars, not so see-thru
						pGameObj->setInitialState(std::forward<state&&>(initialState));
					}
				}
			}

		} // silently fails if no road edge exists
	}

} // end ns world


