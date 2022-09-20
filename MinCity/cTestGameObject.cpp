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

	cTestGameObject::cTestGameObject(cTestGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTestGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cTestGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTestGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}
	}
	cTestGameObject& cTestGameObject::operator=(cTestGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTestGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cTestGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTestGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		return(*this);
	}

	cTestGameObject::cTestGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _glass_color(MASK_GLASS_COLOR), _bulb_color(MASK_BULB_COLOR),
		_accumulator(0.0f), _direction(1.0f)
	{
		instance_->setOwnerGameObject<cTestGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cTestGameObject::OnVoxel);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cTestGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cTestGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cTestGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

#ifdef GIF_MODE

		if (MASK_GLASS_COLOR == voxel.Color) {
			voxel.Color = _glass_color;
		}
		else if (MASK_BULB_COLOR == voxel.Color) {
			voxel.Color = _bulb_color;
		}

#else


#endif
		return(voxel);
	}

	void cTestGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
#ifdef GIF_MODE

		_accumulator += tDelta.count() * _direction * 0.333f;

		if (_direction >= 0.0f) {
			if (_accumulator >= 1.0f) {

				_direction = -_direction;
			}
		}
		else {
			if (_accumulator <= 0.0f) {

				_direction = -_direction;
			}
		}

		uvec4_v const black_body(MinCity::VoxelWorld->blackbody(_accumulator));
		uvec4_t color;
		
		SFM::unpack_rgba(BULB_COLOR, color);

		uvec4_v bulb_color(color);
		bulb_color = SFM::modulate(bulb_color, black_body);

		bulb_color.rgba(color);
		_bulb_color = SFM::pack_rgba(color);

		SFM::unpack_rgba(GLASS_COLOR, color);
		color.a = SFM::float_to_u8(_accumulator * 0.5f);

		uvec4_v glass_color(color);
		glass_color = SFM::blend(glass_color, bulb_color);

		glass_color.rgba(color);
		_glass_color = SFM::pack_rgba(color);

#define SPEED (Iso::VOX_SIZE*2.0f)

		/// 
		// parent rotation of all lights 
		_parent_rotation += tDelta.count() * SPEED;


		XMVECTOR xmLoc((*Instance)->getLocation());
		XMVECTOR const xmLookAt(XMVectorZero());

		XMVECTOR xmDir = XMVectorSubtract(xmLoc, xmLookAt);
		xmDir = XMVector2Normalize(xmDir);
		{
			v2_rotation_t vOrient;
			vOrient = xmDir;

			(*Instance)->setLocationYaw(xmLoc, /*_parent_rotation +*/ vOrient);
		}

		xmLoc = XMVectorSetY(xmLoc, (*Instance)->getElevation());
		xmDir = XMVectorSubtract(xmLoc, xmLookAt);
		xmDir = XMVector2Normalize(xmDir);
		{
			v2_rotation_t vOrient;
			vOrient = -xmDir;

			//(*Instance)->setPitch(vOrient);
		}
#else
		{
			v2_rotation_t vOrient((*Instance)->getYaw());

			vOrient += time_to_float(tDelta) * 0.5f;

			(*Instance)->setYaw(vOrient);
		}
	    /* {
			v2_rotation_t vOrient((*Instance)->getPitch());

			vOrient += time_to_float(tDelta) * 0.5f;

			(*Instance)->setPitch(vOrient);
		}
		{
			v2_rotation_t vOrient((*Instance)->getRoll());

			vOrient += time_to_float(tDelta) * 0.5f;

			(*Instance)->setRoll(vOrient);
		}*/

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
#endif
	}


} // end ns world

