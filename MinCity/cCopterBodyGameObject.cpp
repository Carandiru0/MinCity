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

	cCopterBodyGameObject::cCopterBodyGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
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

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cCopterBodyGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cCopterBodyGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cCopterBodyGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_this = std::move(src._this);
	}
	cCopterBodyGameObject& cCopterBodyGameObject::operator=(cCopterBodyGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cCopterBodyGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cCopterBodyGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cCopterBodyGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_this = std::move(src._this);

		return(*this);
	}

	void cCopterBodyGameObject::SetOwnerCopter(cCopterGameObject* const& owner)
	{
		_this.owner_copter = owner;
	}

	// If currently visible event:
	void __vectorcall cCopterBodyGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, void const* const __restrict _this, uint32_t const vxl_index)
	{
		reinterpret_cast<cCopterBodyGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index);
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	void __vectorcall cCopterBodyGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, uint32_t const vxl_index) const
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
	}

	bool const __vectorcall cCopterBodyGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		rect2D_t const visibleArea(MinCity::VoxelWorld->getVisibleGridBoundsClamped());

		//rect2D_t const focusArae(-128,-128,128,128);

		_this.ai.setFocusedArea(visibleArea);

		_this.ai.setSpeed(MIN_SPEED);
		_this.ai.setAngularSpeed(2.0f);

		bool bNewRoute(false);
		XMVECTOR xmLocation((*Instance)->getLocation());
		XMVECTOR xmR((*Instance)->getAzimuth().v2());

		auto const [xmNewLocation, xmNewR] = _this.ai.OnUpdate(xmLocation, xmR, tNow, tDelta, &bNewRoute);

		v2_rotation_t vR((*Instance)->getAzimuth());
		vR = xmNewR;
		(*Instance)->setLocationAzimuth(xmNewLocation, vR);
		(*Instance)->setElevation(_this.ai.getElevation());

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

		return(true);
	}

	cCopterBodyGameObject::~cCopterBodyGameObject()
	{
		if (_this.owner_copter) {
			_this.owner_copter->releasePart(this);
		}
	}

} // end ns
