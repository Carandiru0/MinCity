#include "pch.h"
#include "cExplosionGameObject.h"
#include "voxelModelInstance.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cExplosionGameObject::remove(static_cast<cExplosionGameObject const* const>(_this));
		}
	}

	cExplosionGameObject::cExplosionGameObject(cExplosionGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src)), _animation(std::move(src._animation)),
		_brightness(std::move(src._brightness))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cExplosionGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}
	}
	cExplosionGameObject& cExplosionGameObject::operator=(cExplosionGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cExplosionGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_animation = std::move(src._animation);
		_brightness = std::move(src._brightness);

		return(*this);
	}

	cExplosionGameObject::cExplosionGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _animation(instance_), _brightness(DEFAULT_BRIGHTNESS)
	{
		instance_->setOwnerGameObject<cExplosionGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cExplosionGameObject::OnVoxel);

		// random start angle
		instance_->setAzimuth(v2_rotation_t(PsuedoRandomFloat() * XM_2PI));
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cExplosionGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cExplosionGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cExplosionGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		static constexpr float const NORMALIZE = 1.0f / float(UINT8_MAX);
		static constexpr float const DENORMALIZE = float(UINT8_MAX);
		
		uint32_t const udensity(voxel.Color & 0xff);
		uint32_t const uflames((voxel.Color >> 8) & 0xff);
		
		voxel.Color = 0; // default color is always initialized to "background" (pure black)
		
		if (uflames) { // flames present?
			float const density((float)udensity * NORMALIZE);
			float const temperature((float)uflames * NORMALIZE);

			uvec4_v const black_body(MinCity::VoxelWorld->blackbody(temperature + _brightness)); // convert temperature to color

			XMVECTOR xmColor(XMVectorScale(black_body.v4f(), NORMALIZE));
			// area's that are less dense are brighter than denser areas - creates dark "spots" where density is high
			xmColor = XMVectorScale(xmColor, (1.0f - density)); // darken

			voxel.Emissive = SFM::XMColorRGBToLuminance(xmColor) > 0.25f; // some light does not need to be added (optimization)

			uvec4_t rgba;
			SFM::saturate_to_u8(XMVectorScale(xmColor, DENORMALIZE), rgba);
			voxel.Color = 0x00ffffff & SFM::pack_rgba(rgba);
		}
		else if (udensity) { // smoke present?
			
			voxel.Color = 0x00ffffff & SFM::pack_rgba(udensity); // this looks best for smoke, voxel.Color must be initialized to zero.
		}
		
		
		return(voxel);
	}

	void cExplosionGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		if (_animation.update(getModelInstance(), tDelta)) {

			// random start angle
			getModelInstance()->setAzimuth(v2_rotation_t(PsuedoRandomFloat() * XM_2PI));
		}
	}


} // end ns world

