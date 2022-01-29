#include "pch.h"
#include "globals.h"
#include "cAutomataGameObject.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cAutomataGameObject::remove(static_cast<cAutomataGameObject const* const>(_this));
		}
	}

	cAutomataGameObject::cAutomataGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_, Volumetric::voxelModel_Dynamic* const& model_)
		: tProceduralGameObject(instance_, model_), _accumulator(zero_time_duration), _born(0), _die(0), _firstUpdate(true)
	{
		instance_->setOwnerGameObject<cAutomataGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cAutomataGameObject::OnVoxel);

		if (nullptr == _bits) { // static inline "singleton"

			_bits = volume::create();
		}


		_state = (Volumetric::voxB::voxelState* const __restrict)scalable_aligned_malloc(sizeof(Volumetric::voxB::voxelState) * Model->_numVoxels, 16);
		__memclr_stream<16>(_state, Model->_numVoxels * sizeof(Volumetric::voxB::voxelState));
	}

	cAutomataGameObject::cAutomataGameObject(cAutomataGameObject&& src) noexcept
		: tProceduralGameObject(std::forward<tProceduralGameObject&&>(src)), _accumulator(src._accumulator), _born(src._born), _die(src._die), _firstUpdate(src._firstUpdate)
	{
		src.free_ownership();

		_state = src._state;
		src._state = nullptr;

		_accumulator = src._accumulator;
		_born = src._born;
		_die = src._die;
		_firstUpdate = src._firstUpdate;

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cAutomataGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cAutomataGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cAutomataGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}
	}
	cAutomataGameObject& cAutomataGameObject::operator=(cAutomataGameObject&& src) noexcept
	{
		tProceduralGameObject::operator=(std::forward<tProceduralGameObject&&>(src));
		src.free_ownership();

		_state = src._state;
		src._state = nullptr;

		_accumulator = src._accumulator;
		_born = src._born;
		_die = src._die;
		_firstUpdate = src._firstUpdate;
		
		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cAutomataGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cAutomataGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cAutomataGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		return(*this);
	}

	// If currently visible event:
	Volumetric::voxB::voxelState const __vectorcall cAutomataGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
	{
		return(reinterpret_cast<cAutomataGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, rOriginalVoxelState, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	Volumetric::voxB::voxelState const __vectorcall cAutomataGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		//Volumetric::voxelModelInstance_Dynamic const* const __restrict instance(getModelInstance());
		//Volumetric::voxelModel_Dynamic const* const __restrict model(getModel());

		//tTime const tNow(now());
		Volumetric::voxB::voxelState voxelState;
		uint32_t adjacent;

		if (_firstUpdate) {
			voxelState = rOriginalVoxelState;
			adjacent = voxel.getAdjacency();
		}
		else {
			voxelState = _state[vxl_index];
			adjacent = voxelState.getAdjacency();
		}

		bool dead(true), born(false);
		dead = (!voxelState.Emissive && voxelState.Hidden);
		born = dead && (voxelState.Emissive && !voxelState.Hidden);
		
		voxelState.Hidden = dead && !born;

		if (adjacent >= _die) {

			voxelState.Emissive = born;
		}
		else if (adjacent <= _born) {

			voxelState.Emissive = true;
			voxelState.Hidden = false;
		}

		voxelState.setAdjacency(adjacent);
		_state[vxl_index] = voxelState; // update per-automata game object instance state array (parallel safe) every single voxel is unique having a unique index, allowing parallel processing.
		
		return(voxelState);
	}

	void cAutomataGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		constinit static uint32_t delay(4);

		if (_firstUpdate) {

			if (0 == --delay) {
				_firstUpdate = false;
			}

			return; // always skip 1st update. Update happens b4 render. However this update runs one-frame behind.
					// this is due to the data dependecy on the render being done 1st. (_worldIndex)
					// this gets cleared during the 1st render so the following update succeeds this test.
		}
	}

	cAutomataGameObject::~cAutomataGameObject()
	{
		SAFE_DELETE(Model);

		if (_bits) {

			volume::destroy(_bits);
			_bits = nullptr;
		}

		if (_state) {

			scalable_aligned_free(_state);
			_state = nullptr;
		}
	}
} // end ns