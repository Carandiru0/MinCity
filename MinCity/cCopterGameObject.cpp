#include "pch.h"
#include "cCopterGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{

	cCopterGameObject::cCopterGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_body)
		: 
		_body(&cCopterBodyGameObject::emplace_back(instance_body)),
		_prop(&cCopterPropGameObject::emplace_back((Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict)instance_body->getChild()))
	{
		_body->SetOwnerCopter(this);
		_prop->SetOwnerCopter(this);
	}

	//cCopterGameObject::iterator cCopterGameObject::Validate(cCopterGameObject::iterator const& it) {

		//if (!_body->isValid() || !_prop->isValid()) {
		//	return(cCopterGameObject::remove(it));
		//}
		// todo this type of game object is not automatically releasable todo //
	//	return(++cCopterGameObject::iterator(it));	// valid, must return subsequent iterator
	//} 

	void cCopterGameObject::releasePart(cCopterBodyGameObject* const& part)
	{
		if (((int64_t)_body) == ((int64_t)part)) {
			_body = nullptr;
		}

		if (nullptr == _body && nullptr == _prop) {
			cCopterGameObject::remove(this);
		}
	}
	void cCopterGameObject::releasePart(cCopterPropGameObject* const& part)
	{
		if (((int64_t)_prop) == ((int64_t)part)) {
			_prop = nullptr;
		}

		if (nullptr == _body && nullptr == _prop) {
			cCopterGameObject::remove(this);
		}
	}

	void cCopterGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		// update child part that inherit the copters main transform
		if (_body->OnUpdate(tNow, tDelta)) {

			// communicate body states to prop
			_prop->setLightsOn(_body->isLightsOn());
			// update
			_prop->OnUpdate(tNow, tDelta, _body->getModelInstance()->getLocation(), _body->getModelInstance()->getElevation(), _body->getModelInstance()->getYaw());
		}
	}
} // end ns
