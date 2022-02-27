#include "pch.h"
#include "cImportGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "voxBinary.h"
#include <filesystem>
#include <Utility/stringconv.h>

namespace fs = std::filesystem;

namespace Volumetric
{
	void ImportProxy::apply_material(colormap::iterator const& iter_current_color)
	{
		constinit static uint32_t lastNumVoxelsEmissive{}, lastNumVoxelsTransparent{};

		if (colors.end() != iter_current_color) {

			// must reset //
			model->_numVoxelsEmissive = lastNumVoxelsEmissive;
			model->_numVoxelsTransparent = lastNumVoxelsTransparent;

			ImportMaterial const material(iter_current_color->material);
			
			uint32_t max_count(iter_current_color->count);

			Volumetric::voxB::voxelDescPacked* pVoxels(model->_Voxels);
			for (uint32_t i = 0; i < numVoxels; ++i) {

				if (material.Color == pVoxels->Color) { // safe 24bit comparison, not affected by material bits soi this is the only way to compare ther color correctly.
					pVoxels->RGBM = material.RGBM;
					pVoxels->Hidden = false; // always false here on output

					[[unlikely]] if (0 == --max_count) {
						break; // finished early
					}
				}
				++pVoxels;
			}

			if (material.Emissive) {
				lastNumVoxelsEmissive = model->_numVoxelsEmissive;
				model->_numVoxelsEmissive += iter_current_color->count;
			}
			
			if (material.Transparent) {
				lastNumVoxelsTransparent = model->_numVoxelsTransparent;
				model->_numVoxelsTransparent += iter_current_color->count;
			}
		}
	}
	void ImportProxy::load(voxB::voxelModelBase const* const model_)
	{
		reset(model_->_numVoxels);
		model = const_cast<voxB::voxelModelBase* const>(model_); // remember model

		// copy the base state of all voxels into proxy
		__memcpy_stream<16>(voxels, model->_Voxels, numVoxels * sizeof(Volumetric::voxB::voxelDescPacked));

		colors.reserve(256);
		colors.clear();

		Volumetric::voxB::voxelDescPacked const* pVoxels(model->_Voxels); // stream above is still storing, use the cached read of the models' voxels ++
		for (uint32_t i = 0; i < numVoxels; ++i) {

			Volumetric::voxB::voxelDescPacked const voxel(*pVoxels);

			uint32_t const color = voxel.Color;

			auto iter = std::lower_bound(colors.begin(), colors.end(), color);

			auto iter_found(colors.end());
			if (color == iter->color) {
				iter_found = iter;
			}
			else if (color == (iter - 1)->color) {
				iter_found = iter - 1;
			}

			if (colors.cend() == iter_found) { // only if unique
				colors.insert(iter, ImportColor(color)); // will be sorted from lowest color to highest color order from lowest vector index to highest vector index
			}
			else {
				++iter_found->count;
			}
			
			++pVoxels;
		}
		// all unique colors found

		// convient initialization before voxel model instance is added to scene

		active_color = colors[0]; // select first color on load
		inv_start_color_count = 1.0f / (float)colors.size();

		// last
	}
	void ImportProxy::save(std::string_view const name) const
	{
		if (model) {
			
			// reset counts, ensure accurate count b4 saving to file
			model->_numVoxelsEmissive = 0;
			model->_numVoxelsTransparent = 0;

			Volumetric::voxB::voxelDescPacked* pVoxels(model->_Voxels); // stream above is still storing, use the cached read of the models' voxels ++
			for (uint32_t i = 0; i < numVoxels; ++i) {
				pVoxels->Hidden = false; // always false here on save, ensure hidden is not set on any voxel b4 saving file

				if (pVoxels->Emissive) {
					++model->_numVoxelsEmissive;
				}
				if (pVoxels->Transparent) {
					++model->_numVoxelsTransparent;
				}

				++pVoxels;
			}

			std::wstring szCachedPathFilename(VOX_CACHE_DIR);
			szCachedPathFilename += fs::path(stringconv::toLower(name)).stem();
			szCachedPathFilename += V1X_FILE_EXT;

			// save / cache that model
			if (SaveV1XCachedFile(szCachedPathFilename, model)) {
				FMT_LOG_OK(VOX_LOG, " < {:s} > cached", stringconv::ws2s(szCachedPathFilename));
			}
			else {
				FMT_LOG_FAIL(VOX_LOG, "unable to cache to .V1X file: {:s}", stringconv::ws2s(szCachedPathFilename));
			}
		}
	}

} // end ns

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

	cImportGameObject_Dynamic::cImportGameObject_Dynamic(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_)
		: tNonUpdateableGameObject(instance_),
		_videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cImportGameObject_Dynamic>(this, &OnRelease_Dynamic);
		instance_->setVoxelEventFunction(&cImportGameObject_Dynamic::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back(ImageAnimation(*voxelscreen, instance_->getHash()));
		}

		_proxy.load(reinterpret_cast<Volumetric::voxB::voxelModelBase const* const>(&instance_->getModel())); // safe up-cast to base
	}
	cImportGameObject_Static::cImportGameObject_Static(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_)
		: tNonUpdateableGameObject(instance_),
		_videoscreen(nullptr)
	{
		instance_->setOwnerGameObject<cImportGameObject_Static>(this, &OnRelease_Static);
		instance_->setVoxelEventFunction(&cImportGameObject_Static::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}

		_proxy.load(reinterpret_cast<Volumetric::voxB::voxelModelBase const* const>(&instance_->getModel())); // safe up-cast to base
	}

	cImportGameObject_Dynamic::cImportGameObject_Dynamic(cImportGameObject_Dynamic&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
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
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
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
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
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
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
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
		Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());

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

		_proxy.voxels[vxl_index] = voxel;

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
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

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

		_proxy.voxels[vxl_index] = voxel;

		return(voxel);
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
