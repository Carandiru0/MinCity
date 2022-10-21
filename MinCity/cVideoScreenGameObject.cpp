#include "pch.h"
#include "cVideoScreenGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cVideoScreenGameObject::remove(static_cast<cVideoScreenGameObject const* const>(_this));
		}
	}

	cVideoScreenGameObject::cVideoScreenGameObject(Volumetric::voxelModelInstance_Static* const& instance_)
		: tNonUpdateableGameObject(instance_),
		_videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cVideoScreenGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cVideoScreenGameObject::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}
	}

	cVideoScreenGameObject::cVideoScreenGameObject(cVideoScreenGameObject&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cVideoScreenGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cVideoScreenGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cVideoScreenGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
	}
	cVideoScreenGameObject& cVideoScreenGameObject::operator=(cVideoScreenGameObject&& src) noexcept
	{
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Validate()) {
			Instance->setOwnerGameObject<cVideoScreenGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cVideoScreenGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cVideoScreenGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;

		return(*this);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cVideoScreenGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cVideoScreenGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cVideoScreenGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

		// alive !
		if (voxel.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

			// if video color is pure black turn off emission
			voxel.Emissive = !(0 == voxel.Color);
		}

		return(voxel);
	}

	void cVideoScreenGameObject::setSequence(uint32_t const index)
	{
		_videoscreen->setForcedSequence(index);
	}

	cVideoScreenGameObject::~cVideoScreenGameObject()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
	}
} // end ns
