#include "pch.h"
#include "cImportGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "importproxy.h"
#include "cVoxelWorld.h"
#include <Math/v2_rotation_t.h>
#include "eVoxelModels.h"

// common between dynamic & static
STATIC_INLINE VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxelProxy(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS, Volumetric::ImportProxy const& __restrict proxy)
{
	if (0 != proxy.active_color.material.Color)  // if active color is active (proxy is active)
	{
		if (voxel.Color == proxy.active_color.material.Color) {
			voxel.RGBM = proxy.active_color.material.RGBM;
		}
		else {
			voxel.Hidden = true;
		}
	}
	return(voxel);
}

namespace world
{
	inline Volumetric::ImportProxy	cImportGameObject::_proxy;

	static void OnRelease_Static(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cImportGameObject_Static::remove(static_cast<cImportGameObject_Static const* const>(_this));
		}
	}
	static void OnRelease_Dynamic(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cImportGameObject_Dynamic::remove(static_cast<cImportGameObject_Dynamic const* const>(_this));
		}
	}

	cImportGameObject::cImportGameObject()
	{
	}

	void __vectorcall cImportGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{		
	}

	cImportGameObject_Dynamic::cImportGameObject_Dynamic(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_),
		_videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cImportGameObject_Dynamic>(this, &OnRelease_Dynamic);
		instance_->setVoxelEventFunction(&cImportGameObject_Dynamic::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back(ImageAnimation(*voxelscreen, instance_->getHash()));
		}

		_proxy.load(this, reinterpret_cast<Volumetric::voxB::voxelModelBase const* const>(&instance_->getModel())); // safe up-cast to base
	}
	cImportGameObject_Static::cImportGameObject_Static(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_),
		_videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cImportGameObject_Static>(this, &OnRelease_Static);
		instance_->setVoxelEventFunction(&cImportGameObject_Static::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}

		_proxy.load(this, reinterpret_cast<Volumetric::voxB::voxelModelBase const* const>(&instance_->getModel())); // safe up-cast to base
	}

	cImportGameObject_Dynamic::cImportGameObject_Dynamic(cImportGameObject_Dynamic&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cImportGameObject_Dynamic>(this, &OnRelease_Dynamic);
			(*Instance)->setVoxelEventFunction(&cImportGameObject_Dynamic::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cImportGameObject_Dynamic>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
	}
	cImportGameObject_Dynamic& cImportGameObject_Dynamic::operator=(cImportGameObject_Dynamic&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cImportGameObject_Dynamic>(this, &OnRelease_Dynamic);
			(*Instance)->setVoxelEventFunction(&cImportGameObject_Dynamic::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cImportGameObject_Dynamic>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;

		return(*this);
	}
	cImportGameObject_Static::cImportGameObject_Static(cImportGameObject_Static&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cImportGameObject_Static>(this, &OnRelease_Static);
			(*Instance)->setVoxelEventFunction(&cImportGameObject_Static::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cImportGameObject_Static>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
	}
	cImportGameObject_Static& cImportGameObject_Static::operator=(cImportGameObject_Static&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cImportGameObject_Static>(this, &OnRelease_Static);
			(*Instance)->setVoxelEventFunction(&cImportGameObject_Static::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cImportGameObject_Static>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;

		return(*this);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cImportGameObject_Dynamic::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cImportGameObject_Dynamic const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cImportGameObject_Dynamic::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		voxel = OnVoxelProxy(xmIndex, voxel, vxl_index, _proxy);

		// alive !
		if (voxel.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

			// if video color is pure black turn off emission
			voxel.Emissive = !(0 == voxel.Color);
		}

		return(voxel);
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cImportGameObject_Static::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cImportGameObject_Static const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cImportGameObject_Static::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		voxel = OnVoxelProxy(xmIndex, voxel, vxl_index, _proxy);

		// alive !
		if (voxel.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

			// if video color is pure black turn off emission
			voxel.Emissive = !(0 == voxel.Color);
		}

		return(voxel);
	}

	void cImportGameObject_Dynamic::OnVideoScreen(bool const bEnable)
	{
		if (bEnable) {
			if (nullptr != _videoscreen) {
				ImageAnimation::remove(_videoscreen);
				_videoscreen = nullptr;
			}

			Volumetric::voxB::voxelScreen const* const voxelscreen(getModelInstance()->getModel()._Features.videoscreen);
			if (nullptr != voxelscreen) {
				_videoscreen = &ImageAnimation::emplace_back(ImageAnimation(*voxelscreen, getModelInstance()->getHash()));
			}
		}
		else {
			if (nullptr != _videoscreen) {
				ImageAnimation::remove(_videoscreen);
				_videoscreen = nullptr;
			}
		}
	}
	void cImportGameObject_Static::OnVideoScreen(bool const bEnable)
	{
		if (bEnable) {
			if (nullptr != _videoscreen) {
				ImageAnimation::remove(_videoscreen);
				_videoscreen = nullptr;
			}

			Volumetric::voxB::voxelScreen const* const voxelscreen(getModelInstance()->getModel()._Features.videoscreen);
			if (nullptr != voxelscreen) {
				_videoscreen = &ImageAnimation::emplace_back(ImageAnimation(*voxelscreen, getModelInstance()->getHash()));
			}
		}
		else {
			if (nullptr != _videoscreen) {
				ImageAnimation::remove(_videoscreen);
				_videoscreen = nullptr;
			}
		}
	}
	
	cImportGameObject_Dynamic::~cImportGameObject_Dynamic()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
	}
	cImportGameObject_Static::~cImportGameObject_Static()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
	}
} // end ns
