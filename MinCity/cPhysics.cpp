#include "pch.h"
#include "globals.h"
#include "cPhysics.h"
#include "adjacency.h"
#include <Utility/async_long_task.h>

#define PAST STAGING // use with care

cPhysics::cPhysics()
	: _force_field_direction{ nullptr, nullptr }, _AsyncClearTaskID(0)
{

}

bool const cPhysics::Initialize()
{
	_force_field_direction[STAGING] = force_volume::create();
	_force_field_direction[COHERENT] = force_volume::create();
	
	return(true);
}

void cPhysics::AsyncClear()
{
	// CURRENT MAIN THREAD //
	_AsyncClearTaskID = async_long_task::enqueue<background_critical>([&] {

		// this update ensures at every game loop tick() that the latest COHERENT forcefield is used, and the STAGING forcefield is cleared for next frame.

		{ // direction
			// all staging must be complete by the time this update method is called //
			std::swap<force_volume* __restrict>(_force_field_direction[COHERENT], _force_field_direction[STAGING]);
		}

		{ // magnitude
			// all staging must be complete by the time this update method is called //
			//auto const old_ptr = _force_field_magnitude[COHERENT];
			//_force_field_magnitude[COHERENT] = _force_field_magnitude[STAGING];
			//_force_field_magnitude[STAGING] = old_ptr;
		}

		// - NOW WERE COHERENT - //

		// PREPARE / CLEAR STAGING FOR NEXT RENDER STAGING ASYNCHRONOUSLY, ALLOW MAIN THREAD TO CONTINUE //
		//async_long_task::wait<background>(_task_id_physics, "physics");
		//_task_id_physics = async_long_task::enqueue<background>([&] {

			// direction
		_force_field_direction[STAGING]->clear(); // reset for next stage!

		{ // magnitude
			// @todo ******performance of this memset is laughable 500MB / frame haha or 16 GB of BW per second. DDR4 RAM can handle.... ~40GB/s 4 channel i believe. My system is gimped at 3 channel, one DIMM destroyed so 30 GB/s - this uses half of the available BW if it were ideal - multithreaded memset maybe needed?
			//__memset_threaded<64>(_force_field_magnitude[STAGING], 0, force_volume::total_bit_count * sizeof(float)); // reset for next stage!
		}

		// NEW STAGING CAN BEGIN //
	//});
	});
	// CONTINUE, NO DATA DEPENDENCY ON CLEARING THE OLD DATA THAT WILL BE USED FOR STAGING THE NEXT FRAME //
}

void cPhysics::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	async_long_task::wait<background_critical>(_AsyncClearTaskID, "physics async clear"); // synchronize with clearing being done for physics
	_AsyncClearTaskID = 0; // reset *important*
}

XMVECTOR const __vectorcall	cPhysics::get_force(uvec4_v const xmIndex) const
{
	XMVECTOR xmForce(XMVectorZero());
	
	ivec4_t iIndex;
	ivec4_v(xmIndex).xyzw(iIndex);
	//BETTER_ENUM(adjacency, uint32_t const,  // matching the same values to voxelModel.h values
	//	left = voxB::BIT_ADJ_LEFT,
	//	right = voxB::BIT_ADJ_RIGHT,
	//	front = voxB::BIT_ADJ_FRONT,
	//	back = voxB::BIT_ADJ_BACK,
	//	above = voxB::BIT_ADJ_ABOVE

	uint32_t adjacent(0);

	// force right
	if (iIndex.x - 1 >= 0) {
		size_t const index(force_volume::get_index(iIndex.x - 1, iIndex.y, iIndex.z));
		
		uint32_t const direction = _force_field_direction[COHERENT]->read_bit(index) << Volumetric::adjacency::left;
		if (direction) {
			float const magnitude = 1.0f + float(_force_field_direction[PAST]->read_bit(index));
			xmForce = XMVectorAdd(xmForce, XMVectorSet(magnitude, 0.0f, 0.0f, 0.0f)); // adjacent to force on the left, outgoing force is left to right.
			adjacent |= direction;
		}
	}
	// force left
	if (iIndex.x + 1 < force_volume::width()) {
		size_t const index(force_volume::get_index(iIndex.x + 1, iIndex.y, iIndex.z));
		
		uint32_t const direction = _force_field_direction[COHERENT]->read_bit(index) << Volumetric::adjacency::right;
		if (direction) {
			float const magnitude = 1.0f + float(_force_field_direction[PAST]->read_bit(index));
			xmForce = XMVectorAdd(xmForce, XMVectorSet(-magnitude, 0.0f, 0.0f, 0.0f)); // adjacent to force on the right, outgoing force is right to left.
			adjacent |= direction;
		}
	}
	// force forward
	if (iIndex.z - 1 >= 0) {
		size_t const index(force_volume::get_index(iIndex.x, iIndex.y, iIndex.z - 1));
		
		uint32_t const direction = _force_field_direction[COHERENT]->read_bit(index) << Volumetric::adjacency::front;
		if (direction) {
			float const magnitude = 1.0f + float(_force_field_direction[PAST]->read_bit(index));
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, 0.0f, magnitude, 0.0f)); // adjacent to force in front, outgoing force is front to back.
			adjacent |= direction;
		}
	}
	// force backward
	if (iIndex.z + 1 < force_volume::depth()) {
		size_t const index(force_volume::get_index(iIndex.x, iIndex.y, iIndex.z + 1));
		
		uint32_t const direction = _force_field_direction[COHERENT]->read_bit(index) << Volumetric::adjacency::back;
		if (direction) {
			float const magnitude = 1.0f + float(_force_field_direction[PAST]->read_bit(index));
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, 0.0f, -magnitude, 0.0f)); // adjacent to force in back, outgoing force is back to front.
			adjacent |= direction;
		}
	}
	
	// force upwards
	if ((Volumetric::adjacency::left | Volumetric::adjacency::right | Volumetric::adjacency::front | Volumetric::adjacency::back) == (adjacent & (Volumetric::adjacency::left | Volumetric::adjacency::right | Volumetric::adjacency::front | Volumetric::adjacency::back))) { // if all surrounding, introduce an upwards force
		xmForce = XMVectorSet(0.0f, XMVectorGetX(XMVector3Length(xmForce)), 0.0f, 0.0f); // adjacent to force above, outgoing force is below to above.
	}

	// force downwards
	if (iIndex.y + 1 < force_volume::height()) {
		size_t const index(force_volume::get_index(iIndex.x, iIndex.y + 1, iIndex.z));
		
		uint32_t const direction = _force_field_direction[COHERENT]->read_bit(index) << Volumetric::adjacency::above;
		if (direction) {
			float const magnitude = 1.0f + float(_force_field_direction[PAST]->read_bit(index));
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, -magnitude, 0.0f, 0.0f)); // adjacent to force above, outgoing force is above to below.
			adjacent |= direction;
		}
	}
	
	// returns a NON-normalized force vector, it has a magnitude that can be obtained from the vectors length.
	return(xmForce);	
}

void cPhysics::CleanUp()
{
	async_long_task::wait<background_critical>(_AsyncClearTaskID, "physics async clear"); // ensure the async clear done
	
	for (uint32_t i = 0; i < 2; ++i) {
		if (_force_field_direction[i]) {
			force_volume::destroy(_force_field_direction[i]);
			_force_field_direction[i] = nullptr;
		}
	}
}