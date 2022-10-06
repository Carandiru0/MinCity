#include "pch.h"
#include "cLightConeGameObject.h"
#include "voxelModelInstance.h"
#include <Math/quat_t.h>
#include "cBlueNoise.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cLightConeGameObject::remove(static_cast<cLightConeGameObject const* const>(_this));
		}
	}

	cLightConeGameObject::cLightConeGameObject(cLightConeGameObject&& src) noexcept
		: cAttachableGameObject(std::forward<cAttachableGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cLightConeGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cLightConeGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cLightConeGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_activity_lights = std::move(src._activity_lights);
	}
	cLightConeGameObject& cLightConeGameObject::operator=(cLightConeGameObject&& src) noexcept
	{
		cAttachableGameObject::operator=(std::forward<cAttachableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cLightConeGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cLightConeGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cLightConeGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_activity_lights = std::move(src._activity_lights);

		return(*this);
	}

	cLightConeGameObject::cLightConeGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: cAttachableGameObject(instance_), _activity_lights{}
	{
		instance_->setOwnerGameObject<cLightConeGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cLightConeGameObject::OnVoxel);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLightConeGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cLightConeGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLightConeGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		constexpr uint32_t const ray_color(0x00ff007f);

		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());
		uint32_t const voxel_count(instance->getModel()._numVoxels);

		float const t(SFM::saturate(time_to_float(_activity_lights.accumulator) / time_to_float(_activity_lights.interval)));

		uint32_t const ring(SFM::saturate_to_u8((1.0f - t) * 255.0f));

		voxel.Hidden = true;
		voxel.Transparent = true;

		if (COLOR_LIGHT_VOXEL == voxel.Color) {

			voxel.Color = ray_color;
			voxel.Emissive = true;
			voxel.Hidden = true;
		}
		else {

			uint32_t const ring_shade(voxel.Color & 0xff);

			if (ring_shade == ring) {
				
				uvec4_t rgba;
				SFM::unpack_rgba(voxel.Color | ray_color, rgba);

				XMVECTOR xmColor(XMVectorScale(uvec4_v(rgba).v4f(), 1.0f / 255.0f));

				xmColor = XMVectorMultiply(xmColor, xmColor); // over-saturate color
				
				float const ring_shade_fp(((float)ring_shade / 255.0f));

				xmColor = SFM::saturate(XMVectorScale(xmColor, 2.0f * ring_shade_fp)); // brighten and put back into 0.0f ... 255.0f range
				xmColor = XMVectorScale(xmColor, 255.0f);

				uvec4_v(xmColor).rgba(rgba);
			
				voxel.Color = SFM::pack_rgba(rgba);
				voxel.Emissive = (0 != voxel.Color);
			}
			else {
				voxel.Color = SFM::pack_rgba(SFM::saturate_to_u8(32.0f * (supernoise::blue.get1D(voxel.Color+frame()*64)))); // larger steps between frames for blue noise index (*64) ensures no correlation
				voxel.Emissive = false;// (voxel.Color > 126.0f);
			}
			
			voxel.Hidden = false;
		}

		return(voxel);
	}

	void cLightConeGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		cAttachableGameObject::OnUpdate(tNow, tDelta);

		_activity_lights.accumulator += tDelta;
		if (_activity_lights.accumulator >= _activity_lights.interval) {
			_activity_lights.accumulator -= _activity_lights.interval;
		}
	}

} // end ns world

