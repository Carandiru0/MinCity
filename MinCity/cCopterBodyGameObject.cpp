#include "pch.h"
#include "cCopterBodyGameObject.h"
#include "cCopterGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "cVoxelWorld.h"
#include "MinCity.h"

namespace world
{
	namespace light_state
	{
		static constexpr int32_t const
			DEFAULT = 0,
			CHANGING = -1,
			OPPOSITE = 1;
	}// end ns

	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cCopterBodyGameObject::remove(static_cast<cCopterBodyGameObject const* const>(_this));
		}
	}

	cCopterBodyGameObject::cCopterBodyGameObject(Volumetric::voxelModelInstance_Dynamic* const&& instance_)
		: tUpdateableGameObject(instance_)
	{
		instance_->setOwnerGameObject<cCopterBodyGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cCopterBodyGameObject::OnVoxel);

		_this.ai.setOwner(instance_->getHash(), instance_->getModel()._LocalArea);
		_this.ai.setClearance(8.0f);
	}

	cCopterBodyGameObject::cCopterBodyGameObject(cCopterBodyGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();
		/*
		// important
		if (check_instance()) {
			getModelInstance()->setOwnerGameObject<cCopterBodyGameObject>(this, &OnRelease);
			getModelInstance()->setVoxelEventFunction(&cCopterBodyGameObject::OnVoxel);
		}
		// important
		if (src.check_instance()) {
			getModelInstance()->setOwnerGameObject<cCopterBodyGameObject>(nullptr, nullptr);
			getModelInstance()->setVoxelEventFunction(nullptr);
		}
		*/
		_this = std::move(src._this);
	}
	cCopterBodyGameObject& cCopterBodyGameObject::operator=(cCopterBodyGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();
		/*
		// important
		if (check_instance()) {
			getModelInstance()->setOwnerGameObject<cCopterBodyGameObject>(this, &OnRelease);
			getModelInstance()->setVoxelEventFunction(&cCopterBodyGameObject::OnVoxel);
		}
		// important
		if (src.check_instance()) {
			getModelInstance()->setOwnerGameObject<cCopterBodyGameObject>(nullptr, nullptr);
			getModelInstance()->setVoxelEventFunction(nullptr);
		}
		*/
		_this = std::move(src._this);

		return(*this);
	}

	void cCopterBodyGameObject::SetOwnerCopter(cCopterGameObject* const& owner)
	{
		_this.owner_copter = owner;
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCopterBodyGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cCopterBodyGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cCopterBodyGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		if (_this.bLightsOn) {

			if (MASK_COLOR_BLUE == voxel.Color) {
				voxel.Color = _this.colorBlueLight;
			}
			else if (MASK_COLOR_RED == voxel.Color) {
				voxel.Color = _this.colorRedLight;
			}
		}
		else {
			if (MASK_COLOR_BLUE == voxel.Color || MASK_COLOR_RED == voxel.Color) {
				voxel.Color = 0x00000000; // lights off
				voxel.Emissive = false;
			}
		}

		return(voxel);
	}

	bool const __vectorcall cCopterBodyGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		/*
		rect2D_t const visibleArea(MinCity::VoxelWorld->getVisibleGridBoundsClamped());

		//rect2D_t const focusArae(-128,-128,128,128);

		_this.ai.setFocusedArea(visibleArea);

		_this.ai.setSpeed(MIN_SPEED);
		_this.ai.setAngularSpeed(2.0f);

		bool bNewRoute(false);
		XMVECTOR xmLocation(getModelInstance()->getLocation());
		XMVECTOR xmR(getModelInstance()->getYaw().v2());

		auto const [xmNewLocation, xmNewR] = _this.ai.OnUpdate(xmLocation, xmR, tNow, tDelta, &bNewRoute);

		v2_rotation_t vR(getModelInstance()->getYaw());
		vR = xmNewR;
		getModelInstance()->setLocationYaw(xmNewLocation, vR);
		getModelInstance()->setElevation(_this.ai.getElevation());

		// lights switching
		if (bNewRoute) {
			_this.bLightsOn = PsuedoRandom5050();
		}

		if (_this.bLightsOn) {
			if ((_this.tLastLights += tDelta) > LIGHT_SWITCH_INTERVAL) {

				// update last full state
				if (light_state::CHANGING != _this.stateLights) {
					_this.laststateLights = _this.stateLights;
				}

				// setup new state
				switch (_this.stateLights)
				{
				case light_state::DEFAULT:
				case light_state::OPPOSITE:
					_this.stateLights = light_state::CHANGING;
					break;
				default:
					_this.stateLights = (light_state::DEFAULT == _this.laststateLights ? light_state::OPPOSITE : light_state::DEFAULT);
					break;
				}
					
				// set colors for new full state
				switch (_this.stateLights)
				{
				case light_state::DEFAULT:
					_this.colorBlueLight = MASK_COLOR_BLUE;
					_this.colorRedLight = MASK_COLOR_RED;
					break;
				case light_state::OPPOSITE:
					_this.colorBlueLight = MASK_COLOR_RED;
					_this.colorRedLight = MASK_COLOR_BLUE;
					break;
				// if state is changing, leave it unchanged here, change in transition below
				}
				_this.tLastLights -= LIGHT_SWITCH_INTERVAL;
			}
			else { // no switch

				if (light_state::CHANGING == _this.stateLights) {

					float const tFade(_this.tLastLights.count() * INV_LIGHT_SWITCH_INTERVAL);

					_this.colorBlueLight = SFM::lerp(_this.colorBlueLight, 0, tFade);
					_this.colorRedLight = SFM::lerp(_this.colorRedLight, 0, tFade);
				}
			}
		}
		*/
		return(true);
	}

	cCopterBodyGameObject::~cCopterBodyGameObject()
	{
		if (_this.owner_copter) {
			_this.owner_copter->releasePart(this);
		}
	}

} // end ns
