#include "pch.h"
#include "cBuildingGameObject.h"
#include "voxelModelInstance.h"
#include "voxelKonstants.h"
#include "voxelModel.h"
#include "cPhysics.h"
#include "MinCity.h"
#include "cExplosionGameObject.h"

namespace world
{
	tbb::concurrent_queue<cBuildingGameObject*>		cBuildingGameObject::_updateable;
	
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cBuildingGameObject::remove(static_cast<cBuildingGameObject const* const>(_this));
		}
	}

	cBuildingGameObject::cBuildingGameObject(Volumetric::voxelModelInstance_Static* const& instance_)
		: tNonUpdateableGameObject(instance_), _tLightChangeInterval(0),
		_destroyed(nullptr), _videoscreen(nullptr), _MutableState(nullptr)
	{
		instance_->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);

		Volumetric::voxB::voxelScreen const* const voxelscreen(instance_->getModel()._Features.videoscreen);
		if (nullptr != voxelscreen) {
			_videoscreen = &ImageAnimation::emplace_back( ImageAnimation(*voxelscreen, instance_->getHash()) );
		}

		_destroyed = Volumetric::voxB::model_volume::create();
		
		_MutableState = new sMutableState{};

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
		if (Validate()) {
			Instance->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cBuildingGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_destroyed = std::move(src._destroyed); src._destroyed = nullptr;
		
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
		if (Validate()) {
			Instance->setOwnerGameObject<cBuildingGameObject>(this, &OnRelease);
			Instance->setVoxelEventFunction(&cBuildingGameObject::OnVoxel);
		}
		// important
		if (src.Validate()) {
			src.Instance->setOwnerGameObject<cBuildingGameObject>(nullptr, nullptr);
			src.Instance->setVoxelEventFunction(nullptr);
		}

		_destroyed = std::move(src._destroyed); src._destroyed = nullptr;
		
		_tLightChangeInterval = src._tLightChangeInterval;

		_videoscreen = std::move(src._videoscreen); src._videoscreen = nullptr;
		_MutableState = std::move(src._MutableState);
		src._MutableState = nullptr;

		return(*this);
	}

	// helper functions
	STATIC_INLINE_PURE bool const isVoxelWindow(Volumetric::voxB::voxelDescPacked const& __restrict voxel)
	{
		return(voxel.Emissive && (Volumetric::Konstants::PALETTE_WINDOW_INDEX == voxel.getColor()));
	}

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cBuildingGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cBuildingGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cBuildingGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		if (_destroyed->read_bit(voxel.x, voxel.y, voxel.z)) {
			voxel.Hidden = true;
			return(voxel);
		}
		
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

		XMVECTOR xmForce = MinCity::Physics->get_force(xmIndex); // (mass of voxel is 1.0) f = ma   So FORCE = ACCELERATION

		{ // destroy voxel if force is present
			uint32_t Comp(0);
			XMVectorGreaterR(&Comp, SFM::abs(xmForce), XMVectorZero());

			if (XMComparisonAnyTrue(Comp)) {
				
				float const scalar_force(XMVectorGetX(XMVector3Length(xmForce)));
				
				uvec4_t rgba;													 // scaled by a factor to put the values in a suitable position for applied forces.
				MinCity::VoxelWorld->blackbody(scalar_force * 0.5f).rgba(rgba);  // burning effect, "extra flames"
				voxel.Color = 0x00ffffff & SFM::pack_rgba(rgba);
				voxel.Emissive = (0 != voxel.Color);

				if (scalar_force > cPhysics::MIN_FORCE) { // IF FORCE ISN'T HIGH ENOUGH, DON'T DESTROY, BURN!
					_destroyed->set_bit(voxel.x, voxel.y, voxel.z); // next time voxel will be "destroyed"
					
					++_MutableState->_destroyed_count.local(); // thread local count will be summed in queued update
					if (!_MutableState->_queued_updatable.test_and_set()) { // only a single voxel needs to schedule an update for the entire instance.
						_updateable.push(const_cast<cBuildingGameObject* const>(this));
					}
				}
				
				return(voxel);
			}
		}
		
		xmForce = XMVectorMultiply(xmForce, XMVectorSet(1.0f, cPhysics::GRAVITY, 1.0f, 0.0f));
		
		tTime const tNow(now());
		float const tDelta = time_to_float(tNow - instance->getDestructionTime());
		
		// destruction sequence ?
		if (zero_time_point != instance->getDestructionTime()) {

			float const maxHeight((float)instance->getModel()._maxDimensions.y);
			float const tSequenceLength = time_to_float(instance->getDestructionSequenceLength()) * maxHeight;
			float const t = (tDelta / tSequenceLength);
			
			if (isVoxelWindow(voxel)) { // burning windows
				
				uvec4_t rgba;
				MinCity::VoxelWorld->blackbody(1.0f - t).rgba(rgba);
				voxel.Color = 0x00ffffff & SFM::pack_rgba(rgba);
				voxel.Emissive |= (0 != voxel.Color);
			}
			
			// crappy pancake collapse
			float const expected_floor(SFM::lerp(maxHeight, 0.0f, t));
			float const top_floor(XMVectorGetY(xmIndex));
			
			if (expected_floor - top_floor >= 0.0f) {

				xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, -0.5f * SFM::min(cPhysics::GRAVITY, (t + 0.5f) * cPhysics::GRAVITY), 0.0f, 0.0f)); // stall floor collapse
			}
			
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, cPhysics::GRAVITY, 0.0f, 0.0f)); // gravity 
		}

		xmForce = XMVectorScale(xmForce, tDelta * tDelta); // acceleration -> force (current value in xmForce is acceleration, to make it acceleration we multiply by time squared. Since the mass of a voxel is 1.0, we get force) f = ma So FORCE = ACCELERATION
		xmIndex = XMVectorAdd(xmIndex, xmForce); // displace current position by the instaneous acceleration/force being applied [simplification]

		XMVECTOR const xmGround(XMVectorSet(0.0f, instance->getElevation(), 0.0f, 0.0f));
		xmIndex = SFM::clamp(xmIndex, xmGround, Volumetric::VOXEL_MINIGRID_VISIBLE_XYZ_MINUS_ONE);
		
		// creation sequence ?
		if (!(Volumetric::eVoxelModelInstanceFlags::INSTANT_CREATION & instance->getFlags()))
		{
			uint32_t const maxHeight(instance->getModel()._maxDimensions.y);

			fp_seconds const tDelta = tNow - instance->getCreationTime();
			fp_seconds const tSequenceLength = instance->getCreationSequenceLength() * maxHeight;
			if (tDelta < tSequenceLength)
			{
				uint32_t heightLimit(maxHeight);
				uint32_t const newHeight = SFM::floor_to_u32(SFM::lerp(0.0f, (float)maxHeight, SFM::saturate(time_to_float(tDelta / tSequenceLength))));

				// new "maximum" for destruction to limit to (height of current creation)
				heightLimit = SFM::min(newHeight, heightLimit);

				if (voxel.y > heightLimit) {
					voxel.Hidden = true;
				}
				else if (voxel.y == heightLimit) {
					voxel.Emissive = true;
				}
				else if (isVoxelWindow(voxel)) { // window lights off during creation
					voxel.Emissive = false;
				}
				else if (voxel.Video) {
					voxel.Color = 0; // screen off during creation //
					voxel.Emissive = false;
				}
				return(voxel); // early exit
			}
		}

		// alive !
		if (voxel.Video && nullptr != _videoscreen) {

			_videoscreen->setAllowedObtainNewSequences(true);

			voxel.Color = _videoscreen->getPixelColor(voxel.getPosition()) & 0x00FFFFFF; // no alpha

			// if video color is pure black turn off emission
			voxel.Emissive = !(0 == voxel.Color);
		}
		/*else if (isVoxelWindow(voxel)) { // Only for specific emissive voxels, with matching palette index for building windows

			static constexpr float UINT32_MAXF = (float)UINT32_MAX,
								   MIN_BRIGHTNESS = 136.0f; // found to be lowest usuable minimum for window shades.
			
			int32_t found_index(-1);
			
			uint8_t const brightness = SFM::saturate_to_u8(((float)Hash((int32_t)vxl_index) / UINT32_MAXF) * 255.0f + MIN_BRIGHTNESS);
			voxel.Color = 0x00ffffff & SFM::pack_rgba(uvec4_t{ brightness, brightness, brightness, 255u });
			
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
					bool const curEmissive(voxel.Emissive);
					bool const nextEmissive = PsuedoRandomNumber(-1, instance->getModel()._numVoxelsEmissive - 1) >= 0;	// this is thread safe as vxl_index is always unique
																			// note: previously, Psuedo has already set seed unique to the hash of the instance

					if (nextEmissive != curEmissive) {
						voxel.Emissive = nextEmissive;
						_MutableState->_tCurrentInterval = nextInterval;

						uint32_t const window_index = _MutableState->_changedWindowIndex;
						_MutableState->_changedWindows[window_index] = sMutableState::Window(vxl_index, voxel.Emissive);
						_MutableState->_changedWindowIndex = (window_index + 1) & (sMutableState::CACHE_SZ - 1);
					}
				}
			}
			else {
				voxel.Emissive = windows[found_index].emissive;
			}
		}*/

		return(voxel);
	}

	// select building updates if queued.
	void __vectorcall cBuildingGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		[[unlikely]] if (!Validate())
			return;

		uint32_t voxels_destroyed(0);
		for (thread_local_counter::const_iterator i = _MutableState->_destroyed_count.begin(); i != _MutableState->_destroyed_count.end(); ++i)
		{
			voxels_destroyed += *i;
		}

		if (voxels_destroyed > (Instance->getVoxelCount() >> 1)) { // 50% destroyed, destroy building.
			Instance->destroy();
		}
		
		
		if (_MutableState) {
			_MutableState->_queued_updatable.clear(); // reset this instance
		}
	}
	void cBuildingGameObject::UpdateAll(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		if (!_updateable.empty()) {
			
			while (!_updateable.empty()) {
				
				cBuildingGameObject* instance(nullptr);
				if (_updateable.try_pop(instance)) {
					if (instance) {
						
						instance->OnUpdate(tNow, tDelta);
					}
				}
			}
		}		
	}

	cBuildingGameObject::~cBuildingGameObject()
	{
		if (nullptr != _destroyed) {

			Volumetric::voxB::model_volume::destroy(_destroyed);
			_destroyed = nullptr;
		}
		if (nullptr != _videoscreen) {
			ImageAnimation::remove(_videoscreen);
			_videoscreen = nullptr;
		}
		SAFE_DELETE(_MutableState);
	}
} // end ns
