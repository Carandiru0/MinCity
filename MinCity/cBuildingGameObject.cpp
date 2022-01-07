#include "pch.h"
#include "cBuildingGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cBuildingGameObject::remove(static_cast<cBuildingGameObject const* const>(_this));
		}
	}

	cBuildingGameObject::cBuildingGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_)
		: tNonUpdateableGameObject(instance_), _tLightChangeInterval(0),
		_videoscreen(nullptr), _MutableState(nullptr)
	{
		instance_->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}

		_MutableState = new sMutableState{};
		_MutableState->_tCurrentInterval = 0;
		_MutableState->_changedWindowIndex = 0;

		static constexpr int32_t const
			CITY_LIGHTS_RANGE_BEGIN = 400, // milliseconds
			CITY_LIGHTS_RANGE_END = 600;

		uint32_t const hash(instance_->getHash());

		SetSeed((int32_t)hash);

		int32_t const interval = PsuedoRandomNumber(CITY_LIGHTS_RANGE_BEGIN, CITY_LIGHTS_RANGE_END);  // unique interval for light changes per building instance

		_tLightChangeInterval = milliseconds(interval);
	}

	cBuildingGameObject::cBuildingGameObject(cBuildingGameObject&& src) noexcept
		: tNonUpdateableGameObject(std::forward<tNonUpdateableGameObject&&>(src))
	{
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cBuildingGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_tLightChangeInterval = src._tLightChangeInterval;

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
		_MutableState = std::move(src._MutableState);
		src._MutableState = nullptr;
	}
	cBuildingGameObject& cBuildingGameObject::operator=(cBuildingGameObject&& src) noexcept
	{
		tNonUpdateableGameObject::operator=(std::forward<tNonUpdateableGameObject&&>(src));
		// important 
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cBuildingGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		_tLightChangeInterval = src._tLightChangeInterval;

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
		_MutableState = std::move(src._MutableState);
		src._MutableState = nullptr;

		return(*this);
	}

	// helper functions
	STATIC_INLINE_PURE bool const isVoxelWindow(Volumetric::voxB::voxelDescPacked const& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState)
	{
		return(rOriginalVoxelState.Emissive && (Volumetric::Konstants::PALETTE_WINDOW_INDEX == voxel.getColor()));
	}

	// If currently visible event:
	Volumetric::voxB::voxelState const __vectorcall cBuildingGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
	{
		return(reinterpret_cast<cBuildingGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, rOriginalVoxelState, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	Volumetric::voxB::voxelState const __vectorcall cBuildingGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

		Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);
		tTime const tNow(now());

		// destruction sequence ?
		if (zero_time_point != instance->getDestructionTime()) {

			uint32_t const maxHeight(instance->getModel()._maxDimensions.y);
			uint32_t heightLimit(maxHeight);

			// check if still within "creation" sequence window
			{
				fp_seconds const tDelta = tNow - instance->getCreationTime();
				fp_seconds const tSequenceLength = milliseconds(Volumetric::Konstants::CREATION_SEQUENCE_LENGTH * maxHeight);

				uint32_t const newHeight = SFM::floor_to_u32(SFM::lerp(0.0f, (float)maxHeight, SFM::saturate(time_to_float(tDelta / tSequenceLength))));

				// new "maximum" for destruction to limit to (height of current creation)
				heightLimit = SFM::min(newHeight, heightLimit);
			}
			fp_seconds const tDelta = tNow - instance->getDestructionTime();
			fp_seconds const tSequenceLength = milliseconds(Volumetric::Konstants::DESTRUCTION_SEQUENCE_LENGTH * maxHeight);
			if (tDelta < tSequenceLength)
			{
				uint32_t const newHeight = SFM::floor_to_u32(SFM::lerp((float)maxHeight, 0.0f, SFM::saturate(time_to_float(tDelta / tSequenceLength))));

				heightLimit = SFM::min(newHeight, heightLimit);

				if (voxel.y > heightLimit) {
					voxelState.Hidden = true;
				}
				else if (voxel.y == heightLimit) {
					voxelState.Emissive = true;
				}
				else if (isVoxelWindow(voxel, rOriginalVoxelState)) { // window lights eratic during destruction
					voxelState.Emissive = PsuedoRandom5050();
				}
				else if (rOriginalVoxelState.Video) {
					if (!PsuedoRandom5050()) {
						voxel.Color = 0; // screen random during destruction //
						voxelState.Emissive = false;
					}
				}
				return(voxelState);
			}
		}
		// creation sequence ?
		else if (!(Volumetric::eVoxelModelInstanceFlags::INSTANT_CREATION & instance->getFlags()))
		{
			uint32_t const maxHeight(instance->getModel()._maxDimensions.y);

			fp_seconds const tDelta = tNow - instance->getCreationTime();
			fp_seconds const tSequenceLength = milliseconds(Volumetric::Konstants::CREATION_SEQUENCE_LENGTH * maxHeight);
			if (tDelta < tSequenceLength)
			{
				uint32_t heightLimit(maxHeight);
				uint32_t const newHeight = SFM::floor_to_u32(SFM::lerp(0.0f, (float)maxHeight, SFM::saturate(time_to_float(tDelta / tSequenceLength))));

				// new "maximum" for destruction to limit to (height of current creation)
				heightLimit = SFM::min(newHeight, heightLimit);

				if (voxel.y > heightLimit) {
					voxelState.Hidden = true;
				}
				else if (voxel.y == heightLimit) {
					voxelState.Emissive = true;
				}
				else if (isVoxelWindow(voxel, rOriginalVoxelState)) { // window lights off during creation
					voxelState.Emissive = false;
				}
				else if (rOriginalVoxelState.Video) {
					voxel.Color = 0; // screen off during creation //
					voxelState.Emissive = false;
				}
				return(voxelState);
			}
		}

		// alive !
		if (rOriginalVoxelState.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha
			voxel.Alpha = Volumetric::eVoxelTransparency::ALPHA_75;

			// if video color is pure black turn off emission
			voxelState.Emissive = !(0 == voxel.Color);
		}
		else if (isVoxelWindow(voxel, rOriginalVoxelState) ) { // Only for specific emissive voxels, with matching palette index for building windows

			int32_t found_index(-1);

			sMutableState::Window const* const& windows(_MutableState->_changedWindows);
			uint32_t const windowCount(_MutableState->_changedWindowIndex);
			for (uint32_t iDx = 0; iDx < sMutableState::CACHE_SZ; ++iDx) {
				if (vxl_index == windows[iDx].vxl_index) {
					found_index = int32_t(iDx);
					break;
				}
			}
			if (found_index < 0) {
				milliseconds const tElapsed = duration_cast<milliseconds>(tNow - start());

				int64_t const nextInterval = SFM::roundToMultipleOf<true>(tElapsed.count(), milliseconds(_tLightChangeInterval).count());

				if (_MutableState->_tCurrentInterval != nextInterval) {
					//if ((nextInterval - tElapsed.count()) < (milliseconds(_tLightChangeInterval).count() >> 6)) {
					bool const curEmissive(rOriginalVoxelState.Emissive);
					bool const nextEmissive = PsuedoRandomNumber(-1, instance->getModel()._numVoxelsEmissive - 1) >= 0;	// this is thread safe as vxl_index is always unique
																			// note: previously, Psuedo has already set seed unique to the hash of the instance

					if (nextEmissive != curEmissive) {
						voxelState.Emissive = nextEmissive;
						_MutableState->_tCurrentInterval = nextInterval;

						uint32_t const window_index = _MutableState->_changedWindowIndex;
						_MutableState->_changedWindows[window_index] = sMutableState::Window(vxl_index, voxelState.Emissive);
						_MutableState->_changedWindowIndex = (window_index + 1) & (sMutableState::CACHE_SZ - 1);
					}
				}
			}
			else {
				voxelState.Emissive = windows[found_index].emissive;
			}
		}

		return(voxelState);
	}



	cBuildingGameObject::~cBuildingGameObject()
	{
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
		SAFE_DELETE(_MutableState);
	}
} // end ns
