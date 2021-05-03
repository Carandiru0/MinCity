#pragma once
#include "cAIMover.h"

class cAISkyMover : public cAIMover
{
public:
	float const getClearance() const { return(_fClearance); }
	float const getElevation() const { return(_fCurrent); }
	float const getMaxElevation() const { return(_fMaxElevation); }

	void setClearance(float const fClearance) {	_fClearance = fClearance; }
	void setMaxElevation(float const fElevation) { _fMaxElevation = fElevation; }

protected:
	virtual int32_t const conditionOfRoute(FXMVECTOR const xmLocation) final;
	virtual void updateRoute(float const fTDeltaNormalized) final;

private:
	int32_t const conditionOfRouteVoxel(point2D_t const voxelIndex);

private:
	float	
		_fMaxElevation,
		_fClearance,
		_fCurrent,
		_fStart,
		_fTarget;
public:
	cAISkyMover();
	~cAISkyMover() = default;
};