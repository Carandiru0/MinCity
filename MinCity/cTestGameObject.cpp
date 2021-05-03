#include "pch.h"
#include "cTestGameObject.h"
#include "voxelModelInstance.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cTestGameObject::remove(static_cast<cTestGameObject const* const>(_this));
		}
	}

	cTestGameObject::cTestGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_)
	{
		instance_->setOwnerGameObject<cTestGameObject>(this, &OnRelease);
	}

	void cTestGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		// some spinning on depth cube
			
		v2_rotation_t vOrient( (*Instance)->getAzimuth() );

		vOrient += tDelta.count() * 0.15f;
			
		(*Instance)->setAzimuth(vOrient);

		/* some displacement on depth cube 
		static v2_rotation_t vAngle;

		XMVECTOR xmDisplacement = XMVectorReplicate(1.1f);

		vAngle += tDelta.count() * 0.9f;
		xmDisplacement = v2_rotate(xmDisplacement, vAngle);

		XMVECTOR xmLocation(Instance->getLocation());

		xmLocation = XMVectorAdd(xmLocation, xmDisplacement);

		//xmDisplacement = XMVectorSetY(xmDisplacement, 0.0f);
		Instance->setLocation(xmLocation);
		*/
	}


} // end ns world

