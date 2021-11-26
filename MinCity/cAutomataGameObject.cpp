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
		: tProceduralGameObject(instance_, model_), _accumulator(zero_duration), _born(0), _die(0), _firstUpdate(true), _changed(false)
	{
		instance_->setOwnerGameObject<cAutomataGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cAutomataGameObject::OnVoxel);

		if (nullptr == _bits) { // static inline "singleton"

			_bits = volume::create();
		}

		_worldIndex = (uvec4_t* const __restrict)scalable_aligned_malloc(sizeof(uvec4_t) * Model->_numVoxels, 16);
		__memclr_stream<16>(_worldIndex, Model->_numVoxels * sizeof(uvec4_t));
	}

	cAutomataGameObject::cAutomataGameObject(cAutomataGameObject&& src) noexcept
		: tProceduralGameObject(std::forward<tProceduralGameObject&&>(src)), _accumulator(src._accumulator), _born(src._born), _die(src._die), _firstUpdate(src._firstUpdate), _changed(src._changed)
	{
		src.free_ownership();

		_worldIndex = src._worldIndex;
		src._worldIndex = nullptr;
		src._changed = false;

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

		_accumulator = src._accumulator;
		_born = src._born;
		_die = src._die;
		_firstUpdate = src._firstUpdate;
		_changed = src._changed;
		src._changed = false;

		_worldIndex = src._worldIndex;
		src._worldIndex = nullptr;
		
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

		Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);
		//tTime const tNow(now());

		// required on first and all further renders
		uvec4_v const uvIndex(SFM::floor_to_u32(XMVectorAdd(xmIndex, _mm_set1_ps(0.5f))));

		uvIndex.xyzw(_worldIndex[vxl_index]); // produce association index --> worldIndex for next update() (data dependency)

		if (!_firstUpdate) { // any modifications to voxel or its state go after the first render/update only.

			uint32_t const adjacency(encode_adjacency(uvIndex));
			voxel.setAdjacency(adjacency);
		}

		// unsafe ?
		const_cast<cAutomataGameObject* const>(this)->_firstUpdate = false;

		return(voxelState);
	}

	void cAutomataGameObject::autoState(Automata const&& in, VecAutomata::reference& __restrict out, VecAutomata::reference& __restrict empty)
	{
		ivec4_t iIndex;
		ivec4_v(in.index).xyzw(iIndex);


		// Von-Neumann Neighbourhood
		bool occupiedIndex[6]{};
		uint32_t adjacent(0);

		// ascending order by occupied index
		if (iIndex.y + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y) {
			occupiedIndex[Volumetric::adjacency::above] = _bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::above];
		}
		else {
			occupiedIndex[Volumetric::adjacency::above] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}

		if (iIndex.z + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z) {
			occupiedIndex[Volumetric::adjacency::back] = _bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::back];
		}
		else {
			occupiedIndex[Volumetric::adjacency::back] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}

		if (iIndex.z - 1 >= 0) {
			occupiedIndex[Volumetric::adjacency::front] = _bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::front];
		}
		else {
			occupiedIndex[Volumetric::adjacency::front] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}

		if (iIndex.x + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X) {
			occupiedIndex[Volumetric::adjacency::right] = _bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::right];
		}
		else {
			occupiedIndex[Volumetric::adjacency::right] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}

		if (iIndex.x - 1 >= 0) {
			occupiedIndex[Volumetric::adjacency::left] = _bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::left];
		}
		else {
			occupiedIndex[Volumetric::adjacency::left] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}

		if (iIndex.y - 1 >= 0) {
			occupiedIndex[Volumetric::adjacency::below] = _bits->read_bit(iIndex.x, iIndex.y - 1, iIndex.z);
			adjacent += (uint32_t)occupiedIndex[Volumetric::adjacency::below];
		}
		else {
			occupiedIndex[Volumetric::adjacency::below] = true; // prevent adding out of bounds to empty vector below - not actually occupied - does not affect anything else,
		}
		

		if (adjacent >= _die) {
			
			_bits->clear_bit(iIndex.x, iIndex.y, iIndex.z);
		}
		else {

			_bits->set_bit(iIndex.x, iIndex.y, iIndex.z);

			// bit is still "set" in volume, pass onto current vector.
			out.emplace_back(in);

			// ascending order by occupied index
			// empty neighbours get added to empty vector only, no bits are set in volume.
			if (!occupiedIndex[Volumetric::adjacency::above]) {

				empty.emplace_back(uvec4_v(iIndex.x, iIndex.y + 1, iIndex.z), in.voxel, in.state);
			}
			if (!occupiedIndex[Volumetric::adjacency::back]) {

				empty.emplace_back(uvec4_v(iIndex.x, iIndex.y, iIndex.z + 1), in.voxel, in.state);
			}
			if (!occupiedIndex[Volumetric::adjacency::front]) {

				empty.emplace_back(uvec4_v(iIndex.x, iIndex.y, iIndex.z - 1), in.voxel, in.state);
			}
			if (!occupiedIndex[Volumetric::adjacency::right]) {

				empty.emplace_back(uvec4_v(iIndex.x + 1, iIndex.y, iIndex.z), in.voxel, in.state);
			}
			if (!occupiedIndex[Volumetric::adjacency::left]) {

				empty.emplace_back(uvec4_v(iIndex.x - 1, iIndex.y, iIndex.z), in.voxel, in.state);
			}
			if (!occupiedIndex[Volumetric::adjacency::below]) {

				empty.emplace_back(uvec4_v(iIndex.x, iIndex.y - 1, iIndex.z), in.voxel, in.state);
			}
		}
	}

	// empty in only, current out
	void cAutomataGameObject::autoState(Automata const& in, VecAutomata::reference& __restrict out)
	{
		ivec4_t iIndex;
		ivec4_v(in.index).xyzw(iIndex);


		// Von-Neumann Neighbourhood
		//bool occupiedIndex[6]{};
		uint32_t adjacent(0);

		// ascending order by occupied index
		if (iIndex.y + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Y) {
			//occupiedIndex[Volumetric::adjacency::above] = _bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x, iIndex.y + 1, iIndex.z); //occupiedIndex[Volumetric::adjacency::above];
		}
		if (iIndex.z + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z) {
			//occupiedIndex[Volumetric::adjacency::back] = _bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x, iIndex.y, iIndex.z + 1); //occupiedIndex[Volumetric::adjacency::back];
		}
		if (iIndex.z - 1 >= 0) {
			//occupiedIndex[Volumetric::adjacency::front] = _bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x, iIndex.y, iIndex.z - 1); //occupiedIndex[Volumetric::adjacency::front];
		}
		if (iIndex.x + 1 < Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X) {
			//occupiedIndex[Volumetric::adjacency::right] = _bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x + 1, iIndex.y, iIndex.z); //occupiedIndex[Volumetric::adjacency::right];
		}
		if (iIndex.x - 1 >= 0) {
			//occupiedIndex[Volumetric::adjacency::left] = _bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x - 1, iIndex.y, iIndex.z); //occupiedIndex[Volumetric::adjacency::left];
		}
		if (iIndex.y - 1 >= 0) {
			//occupiedIndex[Volumetric::adjacency::below] = _bits->read_bit(iIndex.x, iIndex.y - 1, iIndex.z);
			adjacent += (uint32_t)_bits->read_bit(iIndex.x, iIndex.y - 1, iIndex.z); //occupiedIndex[Volumetric::adjacency::below];
		}

		if (adjacent <= _born) {

			// set the volume bit
			_bits->set_bit(iIndex.x, iIndex.y, iIndex.z);

			Automata born(in);
			born.state.Transparent = false;
			born.state.Emissive = true;

			// pass onto current vector
			out.emplace_back(born);
		}
	}

	void cAutomataGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		static constexpr fp_seconds const interval_update(fp_seconds(milliseconds(350)));

		if (_firstUpdate) {
			return; // always skip 1st update. Update happens b4 render. However this update runs one-frame behind.
					// this is due to the data dependecy on the render being done 1st. (_worldIndex)
					// this gets cleared during the 1st render so the following update succeeds this test.
		}

		_changed = false;
		_accumulator += tDelta;
		while (_accumulator >= interval_update) {

			_accumulator -= interval_update;
			_changed = true;
		}
		
		if (_changed) {
			Volumetric::voxelModel_Dynamic* const __restrict model(getModel());

			// temporary thread_local vectors
			VecAutomata current, empty;

			//  iterate over voxels from linear access array used last
			//  depending on state outcome, add to current or not, add empty voxels surrounding voxel if added to current
			//
			tbb::parallel_for(uint32_t(0), model->_numVoxels,
				[&](uint32_t const vxl_index) {

					VecAutomata::reference local_current(current.local()),
										   local_empty(empty.local());

					// state 
					autoState({ uvec4_v(_worldIndex[vxl_index]), model->_Voxels[vxl_index], model->_State[vxl_index] }, local_current, local_empty);

				});

			// iterate over all empty neighbour voxels produced from current
			// depending on state outcome, add to current or not
			tbb::parallel_for(uint32_t(0), (uint32_t)empty.size(),
				[&](uint32_t const not_used) {

					VecAutomata::reference		  local_current(current.local());
					VecAutomata::const_reference  local_empty(empty.local());

					// Volumetric::voxB::voxelDescPacked
					Automata const* automata(local_empty.data()),
						    * const end(local_empty.data() + local_empty.size());

					for (; automata != end; ++automata) {

						// state 
						autoState(*automata, local_current);

					}

				});
			// discard whats in the empty container, current container now includes all voxels that succeeded state from a.) last (not die) b.) surrounding empty neighbours (born)
			// 

			uint32_t numAutomata(0);

			tbb::flattened2d<VecAutomata> flat_view = tbb::flatten2d(current);
			for (tbb::flattened2d<VecAutomata>::const_iterator
				i = flat_view.begin(); i != flat_view.end(); ++i) {

				++numAutomata;
			}

			if (0 != numAutomata) {

				model->_Voxels = (Volumetric::voxB::voxelDescPacked* __restrict)scalable_aligned_realloc((void*)model->_Voxels, sizeof(Volumetric::voxB::voxelDescPacked) * numAutomata, 16);
				model->_State = (Volumetric::voxB::voxelState* __restrict)scalable_aligned_realloc((void*)model->_State, sizeof(Volumetric::voxB::voxelState) * numAutomata, 16);

				numAutomata = 0;
				uint32_t numEmissive(0), numTransparent(0);
				uvec4_v xmMin(UINT32_MAX, UINT32_MAX, UINT32_MAX, 0),
						xmMax(0);

				for (tbb::flattened2d<VecAutomata>::const_iterator
					i = flat_view.begin(); i != flat_view.end(); ++i) {

					__m128i const xmPosition(i->voxel.getPosition());
					xmMin.v = SFM::min(xmMin.v, xmPosition);
					xmMax.v = SFM::max(xmMax.v, xmPosition);

					memcpy((void* __restrict)&model->_Voxels[numAutomata], &i->voxel, sizeof(Volumetric::voxB::voxelDescPacked));
					memcpy(&model->_State[numAutomata], &i->voxel, sizeof(Volumetric::voxB::voxelState));

					++numAutomata;
					numEmissive += (uint32_t const)(bool const)i->state.Emissive;
					numTransparent += (uint32_t const)(bool const)i->state.Transparent;
				}

				model->_numVoxels = numAutomata;
				model->_numVoxelsEmissive = numEmissive;
				model->_numVoxelsTransparent = numTransparent;

				// Actual dimensiuons of model saved, bugfix for "empty space around true model extents"
				uvec4_v xmDimensions(SFM::max(_mm_set1_epi32(1), _mm_sub_epi32(xmMax.v, xmMin.v)));  // bugfix: minimum 1 voxel dimension size on any axis

				// only require calculated dimensions

				// store final dimensions
				XMStoreFloat3A(&model->_maxDimensionsInv, XMVectorReciprocal(xmDimensions.v4f()));
				xmDimensions.xyzw(model->_maxDimensions);

				model->ComputeLocalAreaAndExtents(); // local area is xz dimensions only (no height), extents are based off local area calculation inside function - along with the spherical radius// bugfix: minimum 1 voxel dimension size on any axis

				// TODO hopefully this is not neccessary : Sort the voxels by "slices" on .y (height offset) axis
				//passedVoxels.shrink_to_fit();
				//tbb::parallel_sort(pa	ssedVoxels.begin(), passedVoxels.end());

				// reallocate last linear buffers to new size & copy
				

				_worldIndex = (uvec4_t* const __restrict)scalable_aligned_realloc((void*)_worldIndex, sizeof(uvec4_t) * numAutomata, 16);
				__memclr_stream<16>(_worldIndex, numAutomata * sizeof(uvec4_t));

				// finally synchronize instance w/updated model to grid
				Volumetric::voxelModelInstance_Dynamic* const __restrict instance(getModelInstance());
				instance->synchronize(instance->getLocation(), instance->getAzimuth());
			}
			else {
				// release this gameobject and it's associatted model and instance of that model?
				(*Instance)->destroy();
			}
		}
	}

	cAutomataGameObject::~cAutomataGameObject()
	{
		SAFE_DELETE(Model);

		if (_bits) {

			volume::destroy(_bits);
			_bits = nullptr;
		}

		if (_worldIndex) {

			scalable_aligned_free(_worldIndex);
			_worldIndex = nullptr;
		}
	}
} // end ns