#include "pch.h"
#include "cTrafficSignGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "cTrafficControlGameObject.h"

namespace world
{
	static constexpr fp_seconds const	CHANGE_DURATION = fp_seconds(milliseconds(150));
	static constexpr float const		INV_CHANGE_DURATION = 1.0f / CHANGE_DURATION.count();

	static constexpr int32_t const CHANCE_VIDEOSCREEN = 15;

	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cTrafficSignGameObject::remove(static_cast<cTrafficSignGameObject const* const>(_this));
		}
	}

	cTrafficSignGameObject::cTrafficSignGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tNonUpdateableGameObject(instance_), _videoscreen(nullptr), _control(nullptr), _accumulator{},
		_color_signal{ COLOR_RED }, _last_color_signal{}, _next_color_signal{}, _state(eTrafficLightState::RED_STOP)
	{
		instance_->setOwnerGameObject<cTrafficSignGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cTrafficSignGameObject::OnVoxel);

		if (PsuedoRandomNumber(0, 100) < CHANCE_VIDEOSCREEN) {
			Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
			if (nullptr != voxelscreen) {// this copy is small, the local copy provides better locality of reference
				_videoscreen = &ImageAnimation::emplace_back(ImageAnimation(*voxelscreen, instance_->getHash()));
			}
		}
	}

	cTrafficSignGameObject::cTrafficSignGameObject(cTrafficSignGameObject&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTrafficSignGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cTrafficSignGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTrafficSignGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
		_control = std::move(src._control); src._control = nullptr;

		for (uint32_t i = 0; i < 2; ++i) {
			_color_signal[i] = std::move(src._color_signal[i]);
			_last_color_signal[i] = std::move(src._last_color_signal[i]);
			_next_color_signal[i] = std::move(src._next_color_signal[i]);
		}
	}
	cTrafficSignGameObject& cTrafficSignGameObject::operator=(cTrafficSignGameObject&& src) noexcept
	{
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cTrafficSignGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cTrafficSignGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cTrafficSignGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
		_control = std::move(src._control); src._control = nullptr;
		
		for (uint32_t i = 0; i < 2; ++i) {
			_color_signal[i] = std::move(src._color_signal[i]);
			_last_color_signal[i] = std::move(src._last_color_signal[i]);
			_next_color_signal[i] = std::move(src._next_color_signal[i]);
		}

		return(*this);
	}

	// If currently visible event:
	Volumetric::voxB::voxelState const __vectorcall cTrafficSignGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
	{
		return(reinterpret_cast<cTrafficSignGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, rOriginalVoxelState, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	Volumetric::voxB::voxelState const __vectorcall cTrafficSignGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);
		tTime const tNow(now());

		if (MASK_COLOR_SIGNAL_TURN == voxel.Color) {
			voxel.Color = _color_signal[TURN];
		}
		else if (MASK_COLOR_SIGNAL == voxel.Color) {
			voxel.Color = _color_signal[THRU];
		}

		// alive !
		if (rOriginalVoxelState.Video) {

			if (nullptr != _videoscreen) {
				_videoscreen->setAllowedObtainNewSequences(true);

				voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

				// if video color is pure black turn off emission
				voxelState.Emissive = !(0 == voxel.Color);

				//voxel.Alpha = Volumetric::eVoxelTransparency::ALPHA_75;
				//voxelState.Transparent = true;
			}
			else {
				voxelState.Hidden = true;
			}
		}
		
		return(voxelState);
	}

	void cTrafficSignGameObject::setController(world::cTrafficControlGameObject* const& control)
	{
		_control = control;
	}

	void cTrafficSignGameObject::updateLightColor(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		for (uint32_t i = 0; i < 2; ++i) {

			if (_next_color_signal[i]) {

				if ((_accumulator[i] += tDelta) <= CHANGE_DURATION) {

					// Fade light turning off
					_color_signal[i] = SFM::lerp(_last_color_signal[i], 0, _accumulator[i].count() * INV_CHANGE_DURATION);
				}
				else {
					// instant on new color after turn off
					_color_signal[i] = _next_color_signal[i];
					_next_color_signal[i] = 0; // flagged as no color change pending
					_accumulator[i] = zero_time_duration; // just to be safe, reset
				}
			}
		}
	}

	cTrafficSignGameObject::~cTrafficSignGameObject()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}

		if (nullptr != _control) {
			_control->Remove(this);
			_control = nullptr;
		}
	}
} // end ns
