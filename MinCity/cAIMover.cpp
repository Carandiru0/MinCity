#include "pch.h"
#include "globals.h"
#include "cAIMover.h"
#include <Random/superrandom.hpp>

cAIMover::cAIMover() // default 1 / voxel per second & 1 / voxel radians per second **do not change these default values**
	: _rectLocalArea{}, _rectFocusedArea{}, _tLastSteeringDuration(zero_duration), _fSpeed(1.0f), _fAngSpeed(1.0f), _fInvAngSpeed(1.0f), _hashOwner(0)
{

}

sRoute const __vectorcall cAIMover::getNewRouteDestination(FXMVECTOR const xmLocation, FXMVECTOR const xmR, rect2D_t const& rectArea, float const fSpeed) const
{
	point2D_t randomTarget;

	// generate 2D point that fits inside desired area
	randomTarget.x = PsuedoRandomNumber32(rectArea.left, rectArea.right);
	randomTarget.y = PsuedoRandomNumber32(rectArea.top, rectArea.bottom);

	XMVECTOR const xmNewLocation(p2D_to_v2(randomTarget));
	// v = d/t
	// tv = d
	// t = d / v
	// this is a deliberate approx of distance that will actually be travelled since its an easement of quadratic interpolation that is used
	// to traverse this distance (of a straight line)
	XMVECTOR xmDir = XMVectorSubtract(xmLocation, xmNewLocation);
	XMVECTOR const xmDistance = XMVector2Length(xmDir);
	xmDir = XMVector2Normalize(xmDir);
	
	// return new random route
	return( sRoute(xmLocation, xmNewLocation, 
				   xmR, xmDir,
				   fp_seconds(XMVectorGetX(XMVectorDivide(xmDistance, _mm_set1_ps(fSpeed)))), now()) );
}

bool const cAIMover::buildRoutes(tTime const& __restrict tNow, fp_seconds const& __restrict tDeltaFrame, sRoute const& __restrict newRoute, sRoute& __restrict currentRoute)
{
	fp_seconds const tDelta(tNow - newRoute.tStart);

	if (tDelta < newRoute.tDuration) {

		float tDeltaNormalized = tDelta.count() * newRoute.fInvDuration;

		XMVECTOR const xmStart_Position(XMLoadFloat2A(&newRoute.position.vStart));
		XMVECTOR const xmTarget_Position(XMLoadFloat2A(&newRoute.position.vTarget));
		XMVECTOR const xmStart_Direction(XMLoadFloat2A(&newRoute.direction.vStart));
		XMVECTOR const xmTarget_Direction(XMLoadFloat2A(&newRoute.direction.vTarget));

		XMVECTOR xmNow_Position(SFM::lerp(xmStart_Position, xmTarget_Position, tDeltaNormalized));
		XMStoreFloat2A(&currentRoute.position.vStart, xmNow_Position);

		XMVECTOR xmNow_Direction;
		if (tDeltaNormalized < _fInvAngSpeed) {
			xmNow_Direction = SFM::slerp2D(xmStart_Direction, xmTarget_Direction, SFM::saturate(tDeltaNormalized * _fAngSpeed));
		}
		else {
			xmNow_Direction = xmTarget_Direction;
		}
		XMStoreFloat2A(&currentRoute.direction.vStart, xmNow_Direction);

		currentRoute.tStart = tNow;

		int32_t condition(eRouteCondition::CLEAR);
		
		fp_seconds tDuration(tDelta);
		
		do {

			tDuration += tDeltaFrame;

			// calculate the next location 16 ms(tDeltaFrame) from now
			tDeltaNormalized = tDuration.count() * newRoute.fInvDuration;

			if (tDeltaNormalized < 1.0f) {

				XMVECTOR const xmNext_Position = SFM::lerp(xmStart_Position, xmTarget_Position, tDeltaNormalized);
				
				condition = conditionOfRoute(xmNext_Position);
				
				if (eRouteCondition::CLEAR == condition) {
					xmNow_Position = xmNext_Position;

					if (tDeltaNormalized < _fInvAngSpeed) {
						xmNow_Direction = SFM::slerp2D(xmStart_Direction, xmTarget_Direction, SFM::saturate(tDeltaNormalized * _fAngSpeed));
					}
					else {
						xmNow_Direction = xmTarget_Direction;
					}
				}
			}
			else { // end of route reached
				xmNow_Position = xmTarget_Position;
				break;
			}

		} while (eRouteCondition::CLEAR == condition);

		if (eRouteCondition::CANCEL == condition) {
			currentRoute.tStart = zero_time_point; // invalidate route
			return(false);
		}

		XMStoreFloat2A(&currentRoute.position.vTarget, xmNow_Position);
		XMStoreFloat2A(&currentRoute.direction.vTarget, xmNow_Direction);

		currentRoute.tDuration = tDuration - tDelta; // actual time to traverse using quadratic interpolation
		currentRoute.fInvDuration = 1.0f / (currentRoute.tDuration.count());

		return(true);
	}

	return(false);
}





