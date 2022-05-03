#include "pch.h"
#include "cImportGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "importproxy.h"
#include "cVoxelWorld.h"
#include <Math/v2_rotation_t.h>
#include "eVoxelModels.h"
#include "gui.h"

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
		XMVECTOR const xmWorldOrigin(world::getOriginNoFractionalOffset());

		XMVECTOR xmOrigin, xmControl;

		constexpr uint32_t const color(0xFFFFFFFF);

		// vertical part
		XMVECTOR const xmExtents(XMVectorScale(XMLoadFloat3A(&_proxy.model->_Extents), 2.05f)); // 5% extra
						
		XMVECTOR const xmCenter(XMVectorZero());
		
 		XMVECTOR const xmMin(XMVectorSubtract(xmCenter, xmExtents)),
			           xmMax(XMVectorAdd(xmCenter, xmExtents));

		xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.left_top());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, color, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(0, 0, 1, 0);      // "left bottom"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.right_top());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, color, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(1, 0, 0, 0);      // "right top"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.right_bottom());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, color, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(1, 0, 1, 0);       // "right bottom"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.left_bottom());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, color, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		// horizontal part
		float const height_maximum((float)_proxy.model->_maxDimensions.y * Iso::MINI_VOX_SIZE * 2.0f);
		float height(0.0f);																						                                             // doubled on purpose here
		point2D_t const vWidthHeight(v2_to_p2D_rounded(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorScale(xmExtents, 2.0f / Iso::MINI_VOX_STEP)))); // in minivoxels (units)
		
		// layers of "lights" forming a cubic area over the highlighted model part
		/*
		for (height = height_maximum; height >= 0.0f; --height) {

			xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(0, 0, 1, 0);      // "left bottom"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::z, xmOrigin, color, (uint32_t)vWidthHeight.y, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(1, 0, 0, 0);      // "right top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::z, xmOrigin, color, (uint32_t)vWidthHeight.y, gui::flags::emissive | gui::flags::hidden);
		}
		*/
		
		// moving wall of light inside
		// v = d/t, t = d/v
		static constexpr float const WALL_SPEED(30.0f); // in minivoxels (units) / s
		constinit static bool bAxis(false);
		
		fp_seconds const wall_duration(((float)(bAxis ? vWidthHeight.x : vWidthHeight.y)) / WALL_SPEED);
		constinit static fp_seconds tAccumulator{};
		
		if ( (tAccumulator += tDelta) >= wall_duration ) {
		
			tAccumulator -= wall_duration;
			bAxis = !bAxis;
		}
		
		xmOrigin = SFM::lerp(xmMin, xmMax, tAccumulator / wall_duration);
		
		if (bAxis) {
			
			xmControl = XMVectorSelectControl(0, 0, 1, 0);      // only lerp z-axis, otherwise set to min
			xmOrigin = XMVectorSelect(xmMin, xmOrigin, xmControl);
		}
		else {
			xmControl = XMVectorSelectControl(1, 0, 0, 0);      // only lerp x-axis, otherwise set to min
			xmOrigin = XMVectorSelect(xmMin, xmOrigin, xmControl);
		}
		
		for (height = height_maximum; height >= 0.0f; height -= 4.0f) {
			
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			if (bAxis) {
				gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x, gui::flags::emissive | gui::flags::hidden);
			}
			else {
				gui::draw_line(gui::axis::z, xmOrigin, color, (uint32_t)vWidthHeight.y, gui::flags::emissive | gui::flags::hidden);
			}
		}
		
		// bottom //
		height = SFM::lerp(0.0f, height_maximum, SFM::triangle_wave(0.0f, 1.0f, time_to_float(tAccumulator / wall_duration)));
		for (uint32_t depth = 0; depth < vWidthHeight.y; depth += 16) {

			xmOrigin = SFM::lerp(xmMin, xmMax, (float)depth / (float)vWidthHeight.y);
			xmControl = XMVectorSelectControl(0, 0, 1, 0);      // only lerp z-axis, otherwise set to min
			xmOrigin = XMVectorSelect(xmMin, xmOrigin, xmControl);

			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x, gui::flags::emissive | gui::flags::hidden);
		}
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
