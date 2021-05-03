#include "pch.h"
#include "cSignageGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cSignageGameObject::remove(static_cast<cSignageGameObject const* const>(_this));
		}
	}

	cSignageGameObject::cSignageGameObject(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tNonUpdateableGameObject(instance_), _videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cSignageGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cSignageGameObject::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {									// this copy is small, the local copy provides better locality of reference
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}
	}

	cSignageGameObject::cSignageGameObject(cSignageGameObject&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cSignageGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cSignageGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cSignageGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
	}
	cSignageGameObject& cSignageGameObject::operator=(cSignageGameObject&& src) noexcept
	{
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cSignageGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cSignageGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cSignageGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;

		return(*this);
	}

	// If currently visible event:
	Volumetric::voxB::voxelState const cSignageGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
	{
		return(reinterpret_cast<cSignageGameObject const* const>(_this)->OnVoxel(voxel, rOriginalVoxelState, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	Volumetric::voxB::voxelState const cSignageGameObject::OnVoxel(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);
		tTime const tNow(now());

		// alive !
		if (rOriginalVoxelState.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

			// if video color is pure black turn off emission
			voxelState.Emissive = !(0 == voxel.Color);

			//voxel.Alpha = Volumetric::eVoxelTransparency::ALPHA_75;
			//voxelState.Transparent = true;

		}
		
		return(voxelState);
	}



	cSignageGameObject::~cSignageGameObject()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
	}
} // end ns
