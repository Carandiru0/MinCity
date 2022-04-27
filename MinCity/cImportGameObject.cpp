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

// helper functions
STATIC_INLINE_PURE bool const isVoxelWindow(Volumetric::voxB::voxelDescPacked const& __restrict voxel)
{
	return(voxel.Emissive && (Volumetric::Konstants::PALETTE_WINDOW_INDEX == voxel.getColor()));
}

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
	Volumetric::ImportProxy	cImportGameObject::_proxy;

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
		: _source{}, _target {}, _lights{}, _tStamp(zero_time_point)
	{
	}

	void cImportGameObject::signal(tTime const& __restrict tNow)
	{
		_tStamp = tNow;

		for (uint32_t i = 0; i < _numLights; ++i) {
			XMStoreFloat3A(&_source[i], _lights[i]->getModelInstance()->getLocation3D());
		}
	}

	void __vectorcall cImportGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{		
		static constexpr float const tInvInterval(1.0f / fp_seconds(milliseconds(2000)).count());

		fp_seconds const tDuration(tNow - _tStamp);

		float const t(SFM::saturate(tDuration.count() * tInvInterval));

		XMVECTOR const xmWorldOrigin(world::getOriginNoFractionalOffset());

		XMVECTOR xmOrigin, xmControl;
		rect2D_t const rectLocalArea(r2D_grow(_proxy.model->_LocalArea, point2D_t(1)));
		constexpr uint32_t const color(0xFFFFFFFF);

		// vertical part
		XMVECTOR const xmCenter(XMLoadFloat3A(&_proxy.bounds.Center)), 
			           xmExtents(XMLoadFloat3A(&_proxy.bounds.Extents));
 		XMVECTOR const xmMin(XMVectorSubtract(xmCenter, xmExtents)),
			           xmMax(XMVectorAdd(xmCenter, xmExtents));

		xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.left_top());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, 0x000000ff, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(0, 0, 1, 0);      // "left bottom"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.right_top());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, 0x0000ff00, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(1, 0, 0, 0);      // "right top"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.right_bottom());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, 0x00ff0000, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		xmControl = XMVectorSelectControl(1, 0, 1, 0);       // "right bottom"
		xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl); 
		xmOrigin = XMVectorSetY(xmOrigin, 0.0f);
		//xmOrigin = p2D_to_v2(rectLocalArea.left_bottom());
		//xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_line(gui::axis::y, xmOrigin, 0x0000ffff, (uint32_t)_proxy.model->_maxDimensions.y, gui::flags::emissive);

		// horizontal part
		static constexpr fp_seconds const tInterval(milliseconds(2500));
		constinit static fp_seconds tAccumulate(zero_time_duration);

		float const height_reset((float)_proxy.model->_maxDimensions.y * Iso::MINI_VOX_SIZE * 2.0f);
		tAccumulate += tDelta;

		//float const height = SFM::lerp(height_reset, 0.0f, SFM::saturate(tAccumulate / tInterval));

		if (tAccumulate >= tInterval) {
			tAccumulate -= tInterval;
		}

		point2D_t const vWidthHeight(v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(XMVectorAbs(XMVectorSubtract(xmMax, xmMin)))));

		for (float height = height_reset; height >= 0.0f; --height) {

			xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x << 1, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(0, 0, 1, 0);      // "left bottom"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::x, xmOrigin, color, (uint32_t)vWidthHeight.x << 1, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(0, 0, 0, 0);      // "left top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::z, xmOrigin, color, (uint32_t)vWidthHeight.y << 1, gui::flags::emissive | gui::flags::hidden);

			xmControl = XMVectorSelectControl(1, 0, 0, 0);      // "right top"
			xmOrigin = XMVectorSelect(xmMin, xmMax, xmControl);
			xmOrigin = XMVectorSetY(xmOrigin, -height);
			xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
			gui::draw_line(gui::axis::z, xmOrigin, color, (uint32_t)vWidthHeight.y << 1, gui::flags::emissive | gui::flags::hidden);
		}

		for (uint32_t i = 0; i < _numLights; ++i) {

			XMVECTOR xmLocation(_lights[i]->getModelInstance()->getLocation3D());

			if (zero_time_point != _tStamp) {

				if (t >= 1.0f) {

					xmLocation = XMLoadFloat3A(&_target[i]);
					_source[i] = _target[i];
				}
				else {
				
					xmLocation = SFM::lerp(XMLoadFloat3A(&_source[i]), XMLoadFloat3A(&_target[i]), t);
				}
			}

			xmLocation = v3_rotate_azimuth(xmLocation, v2_rotation_t(tDelta.count() * XM_PI));

			_lights[i]->getModelInstance()->setLocation3D(xmLocation);
		}

		if (t >= 1.0f) {
			_tStamp = zero_time_point; // reset, sequence complete
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
		else if (isVoxelWindow(voxel)) { // Only for specific emissive voxels, with matching palette index for building windows

			voxel.Emissive = true;
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
		else if (isVoxelWindow(voxel)) { // Only for specific emissive voxels, with matching palette index for building windows

			voxel.Emissive = true;
		}

		return(voxel);
	}

	STATIC_INLINE XMVECTOR const __vectorcall next(FXMVECTOR xmWorldOrigin, FXMVECTOR xmLocalOrigin, float const radius, float const t)
	{
		XMVECTOR xmP1 = XMVectorAdd(xmWorldOrigin, XMVectorScale(xmLocalOrigin, Iso::MINI_VOX_STEP));
		XMVECTOR xmP0 = xmWorldOrigin;

		XMVECTOR xmDir = XMVector3Normalize(XMVectorSubtract(xmP1, xmP0));

		v2_rotation_t const approx_angle = v2_rotation_t::lerp(-v2_rotation_constants::v270, v2_rotation_constants::v270, t);
		xmDir = v3_rotate_pitch(xmDir, approx_angle);
		xmDir = v3_rotate_azimuth(xmDir, approx_angle);

		//                                                     +........ renormalize for accurate result
		XMVECTOR const xmPos = XMVectorAdd(xmP1, XMVectorScale(XMVector3Normalize(xmDir), radius * Iso::MINI_VOX_SIZE));

		FMT_LOG(INFO_LOG, "next @ {:f}", fp_seconds(now() - start()).count());

		return(xmPos);
	}

	void cImportGameObject_Dynamic::signal(tTime const& __restrict tNow)  // must be done *after* apply_material
	{
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

		uint32_t const light_model_index(Volumetric::eVoxelModel::DYNAMIC::MISC::LIGHT_X1);
		auto const* const light_model(Volumetric::getVoxelModel<Volumetric::eVoxelModels_Dynamic::MISC>(light_model_index));

		float light_radius(0.0f);

		[[likely]] if (light_model)
		{
			light_radius = XMVectorGetX(XMVector3Length(XMVectorScale(XMLoadFloat3A(&light_model->_Extents), 2.0f))); // hrmmm not sure why this is double of what it should be @todo
		}

		// create light instances....if not created before
		XMVECTOR const xmLocalLocation(XMLoadFloat3A(&_proxy.bounds.Center));
		float const local_radius(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_proxy.bounds.Extents))));

		for (uint32_t i = 0; i < _numLights; ++i) {

			XMVECTOR const xmPos = next(instance->getLocation3D(), xmLocalLocation, local_radius + light_radius, (float)i / (float)(_numLights - 1));

			if (nullptr == _lights[i]) {

				using flags = Volumetric::eVoxelModelInstanceFlags;
				_lights[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cLightGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(
					xmPos,
					Volumetric::eVoxelModel::DYNAMIC::MISC::LIGHT_X1,
					flags::INSTANT_CREATION | flags::IGNORE_EXISTING);
			}

			XMStoreFloat3A(&_target[i], xmPos); // new target position to lerp to.
		}

		// last
		cImportGameObject::signal(tNow);
	}

	void cImportGameObject_Static::signal(tTime const& __restrict tNow)  // must be done *after* apply_material
	{
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

		uint32_t const light_model_index(Volumetric::eVoxelModel::DYNAMIC::MISC::LIGHT_X1);
		auto const* const light_model(Volumetric::getVoxelModel<Volumetric::eVoxelModels_Dynamic::MISC>(light_model_index));

		float light_radius(0.0f);

		[[likely]] if (light_model)
		{
			light_radius = XMVectorGetX(XMVector3Length(XMVectorScale(XMLoadFloat3A(&light_model->_Extents), 2.0f))); // hrmmm not sure why this is double of what it should be @todo
		}

		// create light instances....if not created before
		XMVECTOR const xmLocalLocation(XMLoadFloat3A(&_proxy.bounds.Center));
		float const local_radius(XMVectorGetX(XMVector3Length(XMLoadFloat3A(&_proxy.bounds.Extents))));

		for (uint32_t i = 0; i < _numLights; ++i) {

			XMVECTOR const xmPos = next(instance->getLocation3D(), xmLocalLocation, local_radius + light_radius, (float)i / (float)(_numLights - 1));

			if (nullptr == _lights[i]) {

				using flags = Volumetric::eVoxelModelInstanceFlags;
				_lights[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cLightGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(
					xmPos,
					Volumetric::eVoxelModel::DYNAMIC::MISC::LIGHT_X1,
					flags::INSTANT_CREATION | flags::IGNORE_EXISTING);
			}

			XMStoreFloat3A(&_target[i], xmPos); // new target position to lerp to.
		}

		// last
		cImportGameObject::signal(tNow);
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
