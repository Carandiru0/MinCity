#pragma once
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include "tTime.h"
#include <Math/point2D_t.h>
#include <Math/v2_rotation_t.h>

struct sRoute /*: private no_copy*/
{
	typedef struct
	{
		XMFLOAT2A				vStart, vTarget;
	} range;

	range					position, direction;
	fp_seconds				tDuration;
	float					fInvDuration;
	tTime					tStart;

	sRoute()
		: position{}, direction{}, tDuration{ zero_duration }, fInvDuration(0.0f), tStart{ zero_time_point }
	{
		XMStoreFloat2A(&direction.vStart, v2_rotation_t().v2());	// bugfix: direction vectors must be initialized correctly for default rotation (cos=1.0f,sin=0.0f)
		XMStoreFloat2A(&direction.vTarget, v2_rotation_t().v2());
	}

	sRoute(FXMVECTOR const xmStart_Position, FXMVECTOR const xmTarget_Position,
		   FXMVECTOR const xmStart_Direction, FXMVECTOR const xmTarget_Direction,
		   fp_seconds const& tDuration_, tTime const& tStart_)
		: tDuration(tDuration_), fInvDuration(1.0f / tDuration_.count()), tStart(tStart_)
	{
		XMStoreFloat2A(&position.vStart, xmStart_Position);
		XMStoreFloat2A(&position.vTarget, xmTarget_Position);
		XMStoreFloat2A(&direction.vStart, xmStart_Direction);
		XMStoreFloat2A(&direction.vTarget, xmTarget_Direction);
	}
};

BETTER_ENUM(eRouteCondition, int32_t const,
	CANCEL = -1,
	CLEAR = 0,
	BLOCKED = 1
);

class cAIMover
{
public:
	// accessors //
	rect2D_t const& __restrict		getFocusedArea() const { return(_rectFocusedArea); }
	float const						getSpeed() const { return(_fSpeed); }
	float const						getAngularSpeed() const { return(_fAngSpeed); }
	uint32_t const					getOwner() const { return(_hashOwner); }

	// mutators //
	void setFocusedArea(rect2D_t const& rectArea) { _rectFocusedArea = rectArea; }
	void setSpeed(float const fSpeed) { _fSpeed = fSpeed; }
	void setAngularSpeed(float const fSpeed) { _fAngSpeed = fSpeed; _fInvAngSpeed = 1.0f / fSpeed; }

	void setOwner(uint32_t const hash, rect2D_t const rectLocalArea) { _hashOwner = hash; _rectLocalArea = rectLocalArea; }

	// methods //
	inline auto const __vectorcall OnUpdate(FXMVECTOR xmLocation, FXMVECTOR xmR, tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
										    bool* const __restrict pbNewRoute = nullptr);

protected:
	sRoute const __vectorcall getNewRouteDestination(FXMVECTOR const xmCurLocation, FXMVECTOR const xmR, rect2D_t const& rectArea, float const fSpeed) const;

	virtual int32_t const conditionOfRoute(FXMVECTOR const xmLocation) = 0;
	virtual void updateRoute(float const fTDeltaNormalized) = 0;

	bool const buildRoutes(tTime const& __restrict tNow, fp_seconds const& __restrict tDeltaFrame, sRoute const& __restrict newRoute, sRoute& __restrict currentRoute);
	 

protected:
	sRoute		_currentRoute,
				_nextRoute;
	
	rect2D_t	_rectLocalArea,
				_rectFocusedArea;

	fp_seconds	_tLastSteeringDuration;

	float		_fSpeed,
				_fAngSpeed,
				_fInvAngSpeed;

	uint32_t	_hashOwner;
public:
	cAIMover();
	virtual ~cAIMover() = default;
};


inline auto const __vectorcall cAIMover::OnUpdate(FXMVECTOR xmLocation, FXMVECTOR xmR, tTime const& __restrict tNow, fp_seconds const& __restrict tDeltaFrame,
												  bool* const __restrict pbNewRoute)
{
	typedef struct
	{
		__m128 const xmLocation;
		__m128 const xmR;

	} const returning;

	if (zero_time_point == _currentRoute.tStart) {

		_currentRoute = std::move<sRoute const&& __restrict>(getNewRouteDestination(xmLocation, xmR, _rectFocusedArea, _fSpeed));

		if (pbNewRoute) {
			*pbNewRoute = true;
		}
	}

	if (zero_time_point == _nextRoute.tStart) {
		if (!buildRoutes(tNow, tDeltaFrame, _currentRoute, _nextRoute)) {
			// invalidate current route, it's finished or cancelled
			_currentRoute.tStart = zero_time_point;
		}
	}
	else {

		fp_seconds const tDelta(tNow - _nextRoute.tStart);

		if (tDelta < _nextRoute.tDuration) {

			// get current position of route
			float const tDeltaNormalized = tDelta.count() * _nextRoute.fInvDuration;

			updateRoute(tDeltaNormalized);

			XMVECTOR const xmStart_Position(XMLoadFloat2A(&_nextRoute.position.vStart));

			XMVECTOR const xmStart_Direction(XMLoadFloat2A(&_nextRoute.direction.vStart));
			XMVECTOR const xmTarget_Direction(XMLoadFloat2A(&_nextRoute.direction.vTarget));

			if (tDeltaNormalized < _fInvAngSpeed) {

				XMVECTOR const xmDirection(SFM::slerp2D(xmStart_Direction, xmTarget_Direction, SFM::saturate(tDeltaNormalized * _fAngSpeed)));
				XMVECTOR const xmPosition = SFM::__fma(xmDirection, _mm_set1_ps(tDeltaFrame.count() * -_fSpeed), xmStart_Position);
				
				XMStoreFloat2A(&_nextRoute.position.vStart, xmPosition);
				_tLastSteeringDuration = tDelta;

				return(returning{ xmPosition, xmDirection });
			}
			else {
				float const tDeltaNormalized = (tDelta - _tLastSteeringDuration).count() / (_nextRoute.tDuration - _tLastSteeringDuration).count();

				return(returning{ SFM::lerp(xmStart_Position, XMLoadFloat2A(&_nextRoute.position.vTarget), tDeltaNormalized),
								  xmTarget_Direction });
			}
		}
		else { // finished route
			_nextRoute.tStart = zero_time_point;

			return(returning{ XMLoadFloat2A(&_nextRoute.position.vTarget), XMLoadFloat2A(&_nextRoute.direction.vTarget) });
		}
	}

	// default is to not change location / out = in
	return(returning{ xmLocation, xmR });
}