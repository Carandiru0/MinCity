#include "pch.h"
#include "cHeliumGasGameObject.h"
#include "voxelModelInstance.h"
#include "MinCity.h"
#include "cPhysics.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cHeliumGasGameObject::remove(static_cast<cHeliumGasGameObject const* const>(_this));
		}
	}

	cHeliumGasGameObject::cHeliumGasGameObject(cHeliumGasGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src)), _animation(std::move(src._animation))
	{
		src.free_ownership();

		// important
		{
			if (Validate()) {
				Instance->setOwnerGameObject<cHeliumGasGameObject>(this, &OnRelease);
				Instance->setVoxelEventFunction(&cHeliumGasGameObject::OnVoxel);
			}
		}
		// important
		{
			if (src.Validate()) {
				Instance->setOwnerGameObject<cHeliumGasGameObject>(nullptr, nullptr);
				Instance->setVoxelEventFunction(nullptr);
			}
		}
	}
	cHeliumGasGameObject& cHeliumGasGameObject::operator=(cHeliumGasGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		{
			if (Validate()) {
				Instance->setOwnerGameObject<cHeliumGasGameObject>(this, &OnRelease);
				Instance->setVoxelEventFunction(&cHeliumGasGameObject::OnVoxel);
			}
		}
		// important
		{
			if (src.Validate()) {
				Instance->setOwnerGameObject<cHeliumGasGameObject>(nullptr, nullptr);
				Instance->setVoxelEventFunction(nullptr);
			}
		}

		_animation = std::move(src._animation);
		
		return(*this);
	}

	cHeliumGasGameObject::cHeliumGasGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_)
		: tUpdateableGameObject(instance_), _animation(instance_)
	{
		instance_->setOwnerGameObject<cHeliumGasGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cHeliumGasGameObject::OnVoxel);

		// random start angle
		instance_->setYaw(v2_rotation_t(PsuedoRandomFloat() * XM_2PI));
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cHeliumGasGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cHeliumGasGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cHeliumGasGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		static constexpr float const NORMALIZE = 1.0f / float(UINT8_MAX);
		static constexpr float const DENORMALIZE = float(UINT8_MAX);
		
		// density - velocity x - velocity y 
		uint32_t const udensity(voxel.Color & 0xff);
		uint32_t const uvelocity_x((voxel.Color >> 8) & 0xff);
		uint32_t const uvelocity_y((voxel.Color >> 16) & 0xff);
		
		float velocity(0.0f);

		voxel.Transparent = true; // transparency is controlled by the density value, darker = more transparent (less dense) - lighter = less transparent (more dense)

		if (uvelocity_x) { // velocity_x present?
			
			float const velocity_x = (float)uvelocity_x * NORMALIZE;
			velocity += velocity_x;
			voxel.Emissive |= velocity_x > 0.78f;
		}
		
		if (uvelocity_y) { // velocity_y present?

			float const velocity_y = (float)uvelocity_y * NORMALIZE;
			velocity += velocity_y;
			voxel.Emissive |= velocity_y > 0.78f;
		}

		XMVECTOR xmColor = XMVectorScale(GRADIENT_COLOR_VECTOR, velocity * 0.5f);

		float const elapsed = _animation.getElapsed();
		float const density = 1.0f - (float)udensity * NORMALIZE;
		XMVECTOR const xmDensity(XMVectorReplicate(density));

		xmColor = XMVectorMultiply(xmColor, xmDensity);
		XMVECTOR const xmPeakColor(XMVectorAdd(xmColor, xmDensity));

		xmColor = SFM::lerp(xmPeakColor, SFM::lerp(xmPeakColor, xmColor, velocity), elapsed); // perfect
		
		voxel.Emissive |= (velocity) < 0.7f;

		uvec4_t rgba;
		SFM::saturate_to_u8(XMVectorScale(xmColor, DENORMALIZE), rgba);
		voxel.Color = 0x00ffffff & SFM::pack_rgba(rgba);

		// (mass of voxel is 1.0) f = ma   So FORCE = ACCELERATION
		// therefore this is setup to be in voxels/second squared [acceleration]
		// obey cPhysics:MIN_FORCE and cPhysics:MAX_FORCE limits or else the explosion will explode (as suggested by github pilot)
		MinCity::Physics->add_force(xmIndex);
		
		return(voxel);
	}

	void cHeliumGasGameObject::setElevation(float const elevation)
	{
		[[unlikely]] if (!Validate())
			return;

		Instance->setElevation(elevation);
	}

	void cHeliumGasGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;

		if (_animation.update(*Instance, tDelta)) {

			Instance->setTransparentCount(Instance->getCount()); // all transparent
			Instance->destroy(milliseconds(0));
			return;
		}
		else {
			Instance->setTransparentCount(Instance->getCount()); // all transparent
		}
	}


} // end ns world

