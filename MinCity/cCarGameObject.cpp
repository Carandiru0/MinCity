#include "pch.h"
#include "eDirection.h"
#include "cCarGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "cVoxelWorld.h"
#include "MinCity.h"
#include "cTrafficSignGameObject.h"
#include "eTrafficLightState.h"

BETTER_ENUM(ePrimaryColors, uint32_t const,

	DEFAULT = cCarGameObject::MASK_COLOR_PRIMARY,
	COLOR_BLUE = 0x9d2929,	//bgra
	COLOR_RED = 0x270bbe,
	COLOR_PURPLE = 0xcc155a,
	COLOR_LIME = 0x35ffa4,
	COLOR_HOT_PINK = 0x8a3ce0,
	COLOR_BLACK = 0x0c0c0c

);
BETTER_ENUM(eSecondaryColors, uint32_t const,

	DEFAULT = cCarGameObject::MASK_COLOR_SECONDARY,
	COLOR_GRAY = 0xa0a0a0, // bgra
	COLOR_BLACK = 0x0c0c0c,
	COLOR_GRAY_WHITE = 0xe6e6e6,
	COLOR_WHITE = 0xffffff

);

static constexpr int32_t const CENTER_NODE_OFFSET(Iso::SEGMENT_SIDE_WIDTH + 1);

static void OnRelease(void const* const __restrict _this) // private to this file
{
	if (_this) {
		cCarGameObject::remove(static_cast<cCarGameObject const* const>(_this));
	}
}

cCarGameObject::cCarGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
	: tUpdateableGameObject(instance_)
{
	instance_->setOwnerGameObject<cCarGameObject>(this, &OnRelease);
	instance_->setVoxelEventFunction(&cCarGameObject::OnVoxel);
	_this.tIdleStart = zero_time_point;

	// save car length
	rect2D_t const localArea(instance_->getModel()._LocalArea);
	_this.half_length = float(SFM::max(localArea.width(), localArea.height())) * 0.5f;

	// car color selection
	_this.primary_color = ePrimaryColors::_from_index_unchecked(PsuedoRandomNumber32(0, ePrimaryColors::_size() - 1));

	if (PsuedoRandom5050()) {
		_this.secondary_color = _this.primary_color >> 1; // darkened
	}
	else {
		_this.secondary_color = eSecondaryColors::_from_index_unchecked(PsuedoRandomNumber32(0, eSecondaryColors::_size() - 1));
	}
}

cCarGameObject::cCarGameObject(cCarGameObject&& src) noexcept
	: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
{
	// important 
	src.free_ownership();

	// important
	if (Instance && *Instance) {
		(*Instance)->setOwnerGameObject<cCarGameObject>(this, &OnRelease);
		(*Instance)->setVoxelEventFunction(&cCarGameObject::OnVoxel);
	}
	// important
	if (src.Instance && *src.Instance) {
		(*src.Instance)->setOwnerGameObject<cCarGameObject>(nullptr, nullptr);
		(*src.Instance)->setVoxelEventFunction(nullptr);
	}

	_this = std::move(src._this);
	_arcs[0] = std::move(src._arcs[0]); _arcs[1] = std::move(src._arcs[1]);
}
cCarGameObject& cCarGameObject::operator=(cCarGameObject&& src) noexcept
{
	tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
	// important 
	src.free_ownership();

	// important
	if (Instance && *Instance) {
		(*Instance)->setOwnerGameObject<cCarGameObject>(this, &OnRelease);
		(*Instance)->setVoxelEventFunction(&cCarGameObject::OnVoxel);
	}
	// important
	if (src.Instance && *src.Instance) {
		(*src.Instance)->setOwnerGameObject<cCarGameObject>(nullptr, nullptr);
		(*src.Instance)->setVoxelEventFunction(nullptr);
	}

	_this = std::move(src._this);
	_arcs[0] = std::move(src._arcs[0]); _arcs[1] = std::move(src._arcs[1]);

	return(*this);
}

// If currently visible event:
Volumetric::voxB::voxelState const cCarGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
{
	return(reinterpret_cast<cCarGameObject const* const>(_this)->OnVoxel(voxel, rOriginalVoxelState, vxl_index));
}
// ***** watchout - thread safety is a concern here this method is executed in parallel ******
Volumetric::voxB::voxelState const cCarGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
{
	Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

	Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);

	switch(voxel.Color)
	{
	case MASK_COLOR_HEADLIGHT:
		voxelState.Emissive = !(_this.left_signal_on || _this.right_signal_on);
		break;
	case MASK_COLOR_TAILLIGHT:
		voxel.Color = _this.brakes_on ? 0x0000ff : 0x00007f; // bgra (bright red) when braking
		break;
	case MASK_COLOR_LEFTSIGNAL:
		voxelState.Emissive = _this.left_signal_on;
		break;
	case MASK_COLOR_RIGHTSIGNAL:
		voxelState.Emissive = _this.right_signal_on;
		break;
	case MASK_COLOR_PRIMARY:
		voxel.Color = _this.primary_color;
		break;
	case MASK_COLOR_SECONDARY:
		voxel.Color = _this.secondary_color;
		break;
	}

	return(voxelState);
}

namespace world
{
	bool const hasStoppedCar(Iso::Voxel const& oVoxel, uint32_t const excludeHash)
	{
		for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {

			uint32_t const hash = Iso::getHash(oVoxel, i);
			if (0 != hash && excludeHash != hash) {

				auto const FoundModelInstance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(hash);

				if (FoundModelInstance) {

					if (Volumetric::eVoxelModels_Dynamic::CARS == FoundModelInstance->getModel().identity()._modelGroup) {

						// car exists, is it moving ?
						cCarGameObject const* const pGameObject = FoundModelInstance->getOwnerGameObject<cCarGameObject>();
						if (pGameObject) {

							return(pGameObject->isStopped());  // this function only returns true when a car exists and it is not moving
						}
					}
				}
			}
		}

		return(false);
	}
	bool const hasCar(Iso::Voxel const& oVoxel, uint32_t const excludeHash)
	{
		for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {

			uint32_t const hash = Iso::getHash(oVoxel, i);
			if (0 != hash && excludeHash != hash) {

				auto const FoundModelInstance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(hash);

				if (FoundModelInstance) {

					if (Volumetric::eVoxelModels_Dynamic::CARS == FoundModelInstance->getModel().identity()._modelGroup) {
						return(true); // car already occupies area
					}
				}
			}
		}

		return(false);
	}
	bool const __vectorcall hasCar(point2D_t const voxelIndex, uint32_t const excludeHash)
	{
		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {

			Iso::Voxel const oVoxel(*pVoxel);

			return(hasCar(oVoxel, excludeHash));
		}
	
		return(false);
	}
}

bool const __vectorcall cCarGameObject::xing(state const& __restrict currentState, state& __restrict targetState, point2D_t const voxelRoadCenterNode, uint32_t const roadNodeType)
{
	// form an L shape infront of the car from the xing's center node. That will be the traffic light (in a corner) this car cares about on the grid
	
	// move forward
	point2D_t voxelTrafficLight = p2D_muls(currentState.voxelDirection, CENTER_NODE_OFFSET);
	
	// move to the right
	if (0 == voxelTrafficLight.x) {
		if (voxelTrafficLight.y < 0) {
			voxelTrafficLight.x -= CENTER_NODE_OFFSET;
		}
		else {
			voxelTrafficLight.x += CENTER_NODE_OFFSET;
		}
	}
	else if (0 == voxelTrafficLight.y) {
		if (voxelTrafficLight.x < 0) {
			voxelTrafficLight.y += CENTER_NODE_OFFSET;
		}
		else {
			voxelTrafficLight.y -= CENTER_NODE_OFFSET;
		}
	}
	
	voxelTrafficLight = p2D_add(voxelRoadCenterNode, voxelTrafficLight);

	Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelTrafficLight);

	if (pVoxel) {

		Iso::Voxel const oVoxel(*pVoxel);

		world::cTrafficSignGameObject const* pGameObject(nullptr);

		// searching voxel for traffic light instance
		for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {

			uint32_t const hash = Iso::getHash(oVoxel, i);
			if (0 != hash && Iso::isOwner(oVoxel, i)) {

				auto const FoundModelInstance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(hash);

				if (FoundModelInstance) {

					if (Volumetric::eVoxelModels_Dynamic::MISC == FoundModelInstance->getModel().identity()._modelGroup &&
						Volumetric::eVoxelModels_Indices::TRAFFIC_SIGN == FoundModelInstance->getModel().identity()._index) {

						pGameObject = FoundModelInstance->getOwnerGameObject<world::cTrafficSignGameObject>();

						break; // found
					}
				}
			}
		}

		if (pGameObject) {

			bool bTurningLeft(false),
				 bTurningRight(false);
				 
			
			switch (pGameObject->getState())
			{
			case eTrafficLightState::GREEN_TURNING_ENABLED:
				bTurningLeft = PsuedoRandom5050();
			case eTrafficLightState::GREEN_TURNING_DISABLED:
				bTurningRight = PsuedoRandom5050();
				break;
			case eTrafficLightState::YELLOW_CLEAR:
			case eTrafficLightState::RED_STOP:
			default:
				return(false); // stop - no longer moving
			}
			
			// 3way intersection? lane that always turns left? or the lane that can't turn right or the lane that can't turn left?
			switch (roadNodeType)
			{
			case Iso::ROAD_NODE_TYPE::XING_RTL:
				if (eDirection::S == currentState.direction) { // must turn in this lane
					if (PsuedoRandom5050()) {
						bTurningLeft = true;
					}
					else {
						bTurningRight = true;
					}
				}
				else if (eDirection::E == currentState.direction) { // must not turn right in this lane
					bTurningRight = false;
					bTurningLeft = false; // disabled turning left from this lane aswell
				}
				else if (eDirection::W == currentState.direction) { // must not turn left in this lane
					bTurningLeft = false;
				}
				break;
			case Iso::ROAD_NODE_TYPE::XING_TLB:
				if (eDirection::E == currentState.direction) { // must turn in this lane
					if (PsuedoRandom5050()) {
						bTurningLeft = true;
					}
					else {
						bTurningRight = true;
					}
				}
				else if (eDirection::N == currentState.direction) { // must not turn right in this lane
					bTurningRight = false;
					bTurningLeft = false; // disabled turning left from this lane aswell
				}
				else if (eDirection::S == currentState.direction) { // must not turn left in this lane
					bTurningLeft = false;
				}
				break;
			case Iso::ROAD_NODE_TYPE::XING_LBR:
				if (eDirection::N == currentState.direction) { // must turn in this lane
					if (PsuedoRandom5050()) {
						bTurningLeft = true;
					}
					else {
						bTurningRight = true;
					}
				}
				else if (eDirection::W == currentState.direction) { // must not turn right in this lane
					bTurningRight = false;
					bTurningLeft = false; // disabled turning left from this lane aswell
				}
				else if (eDirection::E == currentState.direction) { // must not turn left in this lane
					bTurningLeft = false;
				}
				break;
			case Iso::ROAD_NODE_TYPE::XING_BRT:
				if (eDirection::W == currentState.direction) { // must turn in this lane
					if (PsuedoRandom5050()) {
						bTurningLeft = true;
					}
					else {
						bTurningRight = true;
					}
				}
				else if (eDirection::S == currentState.direction) { // must not turn right in this lane
					bTurningRight = false;
					bTurningLeft = false; // disabled turning left from this lane aswell
				}
				else if (eDirection::N == currentState.direction) { // must not turn left in this lane
					bTurningLeft = false;
				}
				break;
			default: // no changes to turning left / turning right state
				break;
			}

			
			point2D_t voxelRoadTarget(voxelRoadCenterNode);
			uint32_t targetDirection(currentState.direction);

			if (bTurningLeft) {
				
				switch (currentState.direction) {
				case eDirection::N:
					targetDirection = eDirection::W;
					voxelRoadTarget.x -= CENTER_NODE_OFFSET;
					break;
				case eDirection::S:
					targetDirection = eDirection::E;
					voxelRoadTarget.x += CENTER_NODE_OFFSET;
					break;
				case eDirection::E:
					targetDirection = eDirection::N;
					voxelRoadTarget.y += CENTER_NODE_OFFSET;
					break;
				case eDirection::W:
					targetDirection = eDirection::S;
					voxelRoadTarget.y -= CENTER_NODE_OFFSET;
					break;
				}
				targetState = state(eStateType::XING, targetDirection, voxelRoadTarget, true);
			}
			else if (bTurningRight) {

				switch (currentState.direction) {
				case eDirection::N:
					targetDirection = eDirection::E;
					voxelRoadTarget.x += CENTER_NODE_OFFSET;
					break;
				case eDirection::S:
					targetDirection = eDirection::W;
					voxelRoadTarget.x -= CENTER_NODE_OFFSET;
					break;
				case eDirection::E:
					targetDirection = eDirection::S;
					voxelRoadTarget.y -= CENTER_NODE_OFFSET;
					break;
				case eDirection::W:
					targetDirection = eDirection::N;
					voxelRoadTarget.y += CENTER_NODE_OFFSET;
					break;
				}
				targetState = state(eStateType::XING, targetDirection, voxelRoadTarget, true);
			}
			else { // straight
				voxelRoadTarget = p2D_add(voxelRoadTarget, p2D_muls(currentState.voxelDirection, CENTER_NODE_OFFSET + 1));
				targetState = state(eStateType::XING, currentState.direction, voxelRoadTarget, false);
			}

			{ // check target space for car, don't enter intersection if space is not clear for target
				int32_t const car_half_length(SFM::round_to_i32(_this.half_length));
				point2D_t const targetOffset(p2D_add(targetState.voxelIndex, p2D_muls(targetState.voxelDirection, car_half_length)));
				// target offset is the origin of the car offset by half its length in the target direction
				if (!checkSpace(targetOffset, targetState.voxelDirection, car_half_length, evaluate_car_space)) {
					return(false);
				}
			}

			// common
			if (bTurningLeft || bTurningRight) {

				// setup for new target
				ZeroMemory(_arcs, sizeof(_arcs));

				// calculate biarcs
				biarc_computeArcs(_arcs[0], _arcs[1],
					p2D_to_v2(currentState.voxelIndex), p2D_to_v2(currentState.voxelDirection),
					p2D_to_v2(targetState.voxelIndex), p2D_to_v2(targetState.voxelDirection));

				targetState.distance = (_arcs[0].length + _arcs[1].length) * 2.0f; // bugfix: must be 2x else car moves too fast thru xing
				return(true);
			}

			// straight thru intersection
			targetState.distance = XMVectorGetX(XMVector2Length(p2D_to_v2(p2D_sub(voxelRoadTarget, _this.currentState.voxelIndex)))) * 2.0f; // bugfix: must be 2x else car moves too fast thru xing

			return(true);
		}
	}

	return(false); // stop - no longer moving
}

bool const __vectorcall cCarGameObject::corner(state const& __restrict currentState, state& __restrict targetState, point2D_t const voxelRoadCenterNode, uint32_t const roadNodeType)
{
	point2D_t voxelRoadTarget(voxelRoadCenterNode);
	uint32_t targetDirection(0);

	switch (roadNodeType)
	{
	case Iso::ROAD_NODE_TYPE::CORNER_TL:
		if (eDirection::N == currentState.direction) {
			targetDirection = eDirection::E;
			voxelRoadTarget.x += CENTER_NODE_OFFSET;
		}
		else {
			targetDirection = eDirection::S;
			voxelRoadTarget.y -= CENTER_NODE_OFFSET;
		}
		break;
	case Iso::ROAD_NODE_TYPE::CORNER_BL:
		if (eDirection::S == currentState.direction) {
			targetDirection = eDirection::E;
			voxelRoadTarget.x += CENTER_NODE_OFFSET;
		}
		else {
			targetDirection = eDirection::N;
			voxelRoadTarget.y += CENTER_NODE_OFFSET;
		}
		break;
	case Iso::ROAD_NODE_TYPE::CORNER_BR:
		if (eDirection::S == currentState.direction) {
			targetDirection = eDirection::W;
			voxelRoadTarget.x -= CENTER_NODE_OFFSET;
		}
		else {
			targetDirection = eDirection::N;
			voxelRoadTarget.y += CENTER_NODE_OFFSET;
		}
		break;
	case Iso::ROAD_NODE_TYPE::CORNER_TR:
		if (eDirection::N == currentState.direction) {
			targetDirection = eDirection::W;
			voxelRoadTarget.x -= CENTER_NODE_OFFSET;
		}
		else {
			targetDirection = eDirection::S;
			voxelRoadTarget.y -= CENTER_NODE_OFFSET;
		}
		break;
	};

	// setup for new target
	ZeroMemory(_arcs, sizeof(_arcs));

	targetState = state(eStateType::CORNER, targetDirection, voxelRoadTarget, true);

	// calculate biarcs
	biarc_computeArcs(_arcs[0], _arcs[1],
						p2D_to_v2(currentState.voxelIndex), p2D_to_v2(currentState.voxelDirection),
						p2D_to_v2(targetState.voxelIndex), p2D_to_v2(targetState.voxelDirection));

	targetState.distance = (_arcs[0].length + _arcs[1].length) * 2.0f;  // bugfix: must be 2x else car moves thru corner too fast

	return(true);
}


bool const cCarGameObject::forward(state const& __restrict currentState, state& __restrict targetState, int32_t const distance)
{
	int32_t const car_half_length(SFM::round_to_i32(_this.half_length));
	point2D_t const voxelAhead = p2D_add(currentState.voxelIndex, p2D_muls(currentState.voxelDirection, car_half_length + distance));
	point2D_t voxelRoad = p2D_sub(voxelAhead, currentState.voxelOffsetFromRoadCenter);

	Iso::Voxel const* const pVoxelRoad = world::getVoxelAt(voxelRoad);
	if (pVoxelRoad) {

		Iso::Voxel oVoxelRoad(*pVoxelRoad);
			
		// Don't allow movement onto constructing / pending road
		if (Iso::isPending(oVoxelRoad)) {
			return(false);
		}
		
		// cars?
		{
			Iso::Voxel const* const pVoxelAhead = world::getVoxelAt(voxelAhead);
			if (pVoxelAhead) {

				Iso::Voxel const oVoxelAhead(*pVoxelAhead);

				if (world::hasCar(oVoxelAhead, (*Instance)->getHash())) {
					return(false);  // blocked by car
				}
			}
		}

		bool bFound(false);

		// road end? xing? or corner?
		{
			if (!(Iso::isExtended(oVoxelRoad) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxelRoad))) {

				for (int32_t i = CENTER_NODE_OFFSET; i >= 1; --i) {

					// *center node* //
					if (world::roads::search_neighbour_for_road(voxelRoad, voxelRoad, p2D_muls(currentState.voxelDirection, i))) {

						bFound = true;
						break;
					}
				}

				if (!bFound) {
					(*Instance)->destroy(); // todo: temporary
					return(false);  // blocked by no road exists
				}
			}
			else if (Iso::isRoadNode(oVoxelRoad)) {

				for (int32_t i = 1; i <= (CENTER_NODE_OFFSET << 1); ++i) {

					point2D_t voxelIndex;
					// xing ?
					if (world::roads::search_neighbour_for_road(voxelIndex, voxelRoad, p2D_muls(currentState.voxelDirection, i))) {

						Iso::Voxel const oVoxel = *world::getVoxelAt(voxelIndex);
						if (!Iso::isRoadNode(oVoxel)) {

							if (Iso::ROAD_TILE::XING == Iso::getRoadTile(oVoxel)) { // not a node = straight / xing

								// *center node* //
								voxelRoad = p2D_sub(voxelIndex, p2D_muls(currentState.voxelDirection, CENTER_NODE_OFFSET));

								bFound = true;
								break;
							}
						}
							
					}
					else { // corner !

						voxelIndex = p2D_add(voxelRoad, p2D_muls(currentState.voxelDirection, i));
						// *center node* //
						voxelRoad = p2D_sub(voxelIndex, p2D_muls(currentState.voxelDirection, CENTER_NODE_OFFSET));

						bFound = true;
						break;
					}
				}
			}
		}

		uint32_t roadNodeType(Iso::ROAD_NODE_TYPE::INVALID);
		if (bFound) {

			// acquire center node
			oVoxelRoad = *world::getVoxelAt(voxelRoad);

			if (Iso::isRoadNode(oVoxelRoad)) {

				roadNodeType = Iso::getRoadNodeType(oVoxelRoad);

				if (Iso::ROAD_NODE_TYPE::INVALID != roadNodeType) {

					// xing?
					if (roadNodeType <= Iso::ROAD_NODE_TYPE::XING && Iso::isRoadNodeCenter(oVoxelRoad)) {

						return(xing(currentState, targetState, voxelRoad, roadNodeType));
					}
					// corner?
					else if (roadNodeType <= Iso::ROAD_NODE_TYPE::CORNER) {

						return(corner(currentState, targetState, voxelRoad, roadNodeType));
					}
					/*else { // state non existant
						return(false);
					}*/
				}
			}
			/*else { // state non existant
				return(false);
			}*/
		}

		// straight thru - forward //

		targetState = state(eStateType::STRAIGHT, currentState.direction, p2D_add(currentState.voxelIndex, currentState.voxelDirection));
		targetState.distance = 1;

		return(true);
	}
		
	return(false);
}

void __vectorcall cCarGameObject::gravity(FXMVECTOR const xmLocation, FXMVECTOR const xmAzimuth)
{
	// actual front & back (not offset to road center)
	XMVECTOR xmFront, xmBack;

	xmFront = XMVectorAdd(xmLocation, XMVectorScale(xmAzimuth, _this.half_length));
	xmBack = XMVectorSubtract(xmLocation, XMVectorScale(xmAzimuth, _this.half_length));

	float fRoadHeightFront, fRoadHeightBack, fRoadHeightCenter;

	// Default Height is the origin road height
	{
		// origin, offset to road center
		point2D_t const voxelRoad(p2D_sub(v2_to_p2D(xmLocation), _this.currentState.voxelOffsetFromRoadCenter));

		Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelRoad));
		if (pVoxel) {
			Iso::Voxel const oVoxel(*pVoxel);

			if (Iso::isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)) {
				fRoadHeightFront = fRoadHeightBack = fRoadHeightCenter = Iso::getRealRoadHeight(oVoxel);
			}
			else { // shouldn't happen but just in case if no road default to ground height
				fRoadHeightFront = fRoadHeightBack = fRoadHeightCenter = Iso::getRealHeight(oVoxel);
			}
		}
	}
	{
		// front/back, offset to road center
		point2D_t const voxelFront(p2D_sub(v2_to_p2D(xmFront), _this.currentState.voxelOffsetFromRoadCenter)),
						voxelBack(p2D_sub(v2_to_p2D(xmBack), _this.currentState.voxelOffsetFromRoadCenter));

		{ // front
			Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelFront);
			if (pVoxel) {
				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)) {
					fRoadHeightFront = Iso::getRealRoadHeight(oVoxel);
				}
			}
		}

		{ // back
			Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelBack);
			if (pVoxel) {
				Iso::Voxel const oVoxel(*pVoxel);

				if (Iso::isExtended(oVoxel) && Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)) {
					fRoadHeightBack = Iso::getRealRoadHeight(oVoxel);
				}
			}
		}
	}

	// determine difference between current origin road height and front/back road heights to elevate entire model above road
	{
		//float const fElevationFront = SFM::abs(fRoadHeightCenter - fRoadHeightFront);
		//float const fElevationBack = SFM::abs(fRoadHeightCenter - fRoadHeightBack);

		(*Instance)->setElevation(/*(fElevationFront + fElevationBack) * 0.5f +*/ fRoadHeightCenter + Iso::VOX_SIZE);
	}

	// determine "pitch" vector for direction of road elevation from Back To Front
	xmFront = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmFront);
	xmFront = XMVectorSetY(xmFront, fRoadHeightFront);

	xmBack = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmBack);
	xmBack = XMVectorSetY(xmBack, fRoadHeightBack);

	// now have two (3D) endpoints, get normalized direction (3D)
	XMVECTOR const xmDirection(XMVector3Normalize(XMVectorSubtract(xmBack, xmFront)));

	// only want pitch portion of 3D direction
	XMVECTOR xmPitch;
		
	switch (_this.currentState.direction)
	{
	case eDirection::N:
	case eDirection::S:
		xmPitch = XMVectorSwizzle<XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W, XM_SWIZZLE_W>(xmDirection);
		break;
	case eDirection::E:
	case eDirection::W:
		xmPitch = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Y, XM_SWIZZLE_W, XM_SWIZZLE_W>(xmDirection);
		break;
	}
		
	// renormalize for 2D direction now in x,y components
	xmPitch = XMVector2Normalize(xmPitch);

	// convert direction (vector) to rotation
	v2_rotation_t vPitch;
	vPitch = xmPitch;
		
	if (eDirection::S == _this.currentState.direction || eDirection::W == _this.currentState.direction) {
		vPitch = vPitch + v2_rotation_constants::v180;
		vPitch = -vPitch;
	}

	// apply pitch to model instance
	(*Instance)->setPitch(vPitch);
}

bool const __vectorcall cCarGameObject::ccd(FXMVECTOR const xmLocation, FXMVECTOR const xmAzimuth) const
{
	// actual front + 1 (not offset to road center) // ***subtract here works for ccd don't know how - but it works***
	XMVECTOR const xmAhead(XMVectorSubtract(xmLocation, XMVectorScale(xmAzimuth, _this.half_length + 1.0f)));

	// only doing check directly infront of car
	point2D_t const voxelIndexAhead(v2_to_p2D_rounded(xmAhead)); // rounding to catch a collision that is a little more ahead than if this were not rounded as usual.
	
    // return current state of voxel car<>car collision
	return(!world::hasCar(voxelIndexAhead, (*Instance)->getHash()));
} // returns true on no collision, false on collision

#ifndef NDEBUG
#ifdef DEBUG_TRAFFIC
static uint32_t g_cars_errored_out(0);
#endif
#endif

void __vectorcall cCarGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	if (!_this.moving) {

		_this.moving = forward(_this.currentState, _this.targetState, 1);
	}

	if (_this.moving) {

		// any changes to members of *this should be done only *after* ccd (below)
		XMVECTOR const xmTarget(p2D_to_v2(_this.targetState.voxelIndex));
		XMVECTOR xmNow, xmNowR(_this.currentState.vAzimuth.v2());

		// v = d/t
		// t = d/v
		// d = vt
		bool end_of_state(false);
		float const dt = _speed * tDelta.count();		// distance to add this frame
		float accumulator(_this.accumulator);

		if ((accumulator += dt) >= _this.targetState.distance) {	// when distance travelled exceeds total distance
			accumulator = 0.0f;
			end_of_state = true;
			
			xmNow = xmTarget;
			xmNowR = _this.targetState.vAzimuth.v2();
		}
		else {

			XMVECTOR const xmStart(p2D_to_v2(_this.currentState.voxelIndex));

			float tNorm(accumulator / _this.targetState.distance);

			// turning lerp (position & direction)
			if (_this.targetState.bTurning) {

				// position lerp
				xmNow = biarc_interpolate(_arcs[0], _arcs[1], tNorm);

				// rotation nlerp (direction only) // nlerp = normalize(lerp) * d  // see: https://www.desmos.com/calculator/rgfc7qpnru
				xmNowR = XMVector2Normalize(SFM::lerp(_this.currentState.vAzimuth.v2(), _this.targetState.vAzimuth.v2(), tNorm));
			}
			else {
				// position lerp
				xmNow = SFM::lerp(xmStart, xmTarget, tNorm);
			}
		}
		
		if (ccd(xmNow, xmNowR)) {

			// common //
			_this.accumulator = accumulator;

			if (end_of_state) {
				// make current = target
				_this.currentState = std::move(_this.targetState);

				// reset
				if (_this.currentState.bTurning) {

					ZeroMemory(_arcs, sizeof(_arcs));
				}

				_this.moving = false;
			}

			gravity(xmNow, xmNowR);

			if (_this.targetState.bTurning) {
				v2_rotation_t vNowR;
				vNowR = xmNowR;

				(*Instance)->setLocationAzimuth(xmNow, vNowR);
			}
			else {

				(*Instance)->setLocationAzimuth(xmNow, _this.currentState.vAzimuth);
			}

			_this.brakes_on = false;
			_this.tIdleStart = zero_time_point; // reset idle counter on successful movement //
		}
		else {
			// if ccd fails, there is no change to *this, retry next update
			if (!_this.brakes_on) {
				_this.brakes_on = true;
				if (zero_time_point == _this.tIdleStart) {
					_this.tIdleStart = tNow; // idle counter still works
				}
			}
			else { 
				// too long idle (ccd failure state)
				if (tNow - _this.tIdleStart > CAR_IDLE_MAX) {
#ifndef NDEBUG
#ifdef DEBUG_TRAFFIC
					if (!(*Instance)->destroyPending()) {
						++g_cars_errored_out;
					}
#endif
#endif	
					(*Instance)->destroy(); // car gets destroyed		
				}
			}
			// nothing else changes, want _this.moving to equal true so that this state that isn't finished
			// persists until it is done (when ccd clears). vs recalculating target state in middle of corner or xing when ccd clears.
		}
		return; // just return, good for either ccd pass/fail
	}


	// fallthru to default state //
	_this.brakes_on = true;
	_this.moving = false; // this triggers forward() for next frame

	if (zero_time_point == _this.tIdleStart) {
		_this.tIdleStart = tNow;
	}
	else {
		// too long idle
		if (tNow - _this.tIdleStart > (CAR_IDLE_MAX * 2)) { // double the time to error out *only* here
#ifndef NDEBUG
#ifdef DEBUG_TRAFFIC
			if (!(*Instance)->destroyPending()) {
				++g_cars_errored_out;
			}
#endif
#endif	
			(*Instance)->destroy(); // car gets destroyed
		}
	}
}

void cCarGameObject::state::deduce_state()
{
	switch (direction)
	{
	case eDirection::N:
		voxelOffsetFromRoadCenter.x = (Iso::ROAD_SEGMENT_WIDTH >> 2) + 1;
		voxelDirection = point2D_t(0, 1);
		vAzimuth = -v2_rotation_constants::v90;
		break;
	case eDirection::S:
		voxelOffsetFromRoadCenter.x = -int32_t((Iso::ROAD_SEGMENT_WIDTH >> 2) + 1);
		voxelDirection = point2D_t(0, -1);
		vAzimuth = v2_rotation_constants::v90;
		break;
	case eDirection::E:
		voxelOffsetFromRoadCenter.y = -int32_t((Iso::ROAD_SEGMENT_WIDTH >> 2) + 1);
		voxelDirection = point2D_t(1, 0);
		vAzimuth = v2_rotation_constants::v180;
		break;
	case eDirection::W:
		voxelOffsetFromRoadCenter.y = (Iso::ROAD_SEGMENT_WIDTH >> 2) + 1;
		voxelDirection = point2D_t(-1, 0);
		break;
	}
}

cCarGameObject::state::state(uint32_t const type_, uint32_t const direction_, point2D_t const voxelIndex_) // accepts voxelIndex (ahead of car)
	: type(type_), direction(direction_), voxelIndex(voxelIndex_), distance(1.0f), bTurning(false)
{
	deduce_state();
}
cCarGameObject::state::state(uint32_t const type_, uint32_t const direction_, point2D_t const voxelRoadIndex_, bool const bTurning_) // accepts voxelRoadIndex (center of road)
	: type(type_), direction(direction_), distance(1.0f), bTurning(bTurning_)
{
	deduce_state();
	voxelIndex = p2D_add(voxelRoadIndex_, voxelOffsetFromRoadCenter);
}

void cCarGameObject::setInitialState(state&& initialState)
{
	(*Instance)->setAzimuth(initialState.vAzimuth);

	_this.currentState = std::forward<state&&>(initialState);
	_this.targetState = std::move(_this.currentState);

	_this.moving = false;
	_this.accumulator = 0.0f;

	_speed = MIN_SPEED;
}

// STATIC METHODS : //

void cCarGameObject::Initialize(tTime const& __restrict tNow)
{
	reserve(MAX_CARS);
}
void cCarGameObject::UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
#ifndef GIF_MODE
	if (size() < MAX_CARS) {

		static tTime tLastCreation(zero_time_point);

		if (tNow - tLastCreation > milliseconds(CAR_CREATE_INTERVAL)) {
			CreateCar();
			tLastCreation = tNow;
		}
	}

	auto it = begin();
	while (end() != it) {
	
		it->OnUpdate(tNow, tDelta);
		++it;
	}

#ifndef NDEBUG
#ifdef DEBUG_TRAFFIC
	FMT_NUKLEAR_DEBUG(false, "cars active: {:n}  cars errored out: {:n}", size(), g_cars_errored_out);
#endif
#endif	
#endif
}

cCarGameObject::~cCarGameObject()
{
}


