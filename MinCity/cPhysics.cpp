#include "pch.h"
#include "globals.h"
#include "cPhysics.h"
#include "adjacency.h"

cPhysics::cPhysics()
	: _force_field{ nullptr, nullptr }
{

}

bool const cPhysics::Initialize()
{
	_force_field[STAGING] = force_volume::create();
	_force_field[COHERENT] = force_volume::create();
	
	return(true);
}

void cPhysics::Update()
{
	// this update ensures at ivery game loop tick() that the latest COHERENT forcefield is used, and the STAGING forcefield is cleared for next frame.
	auto const pOLD(_force_field[COHERENT]);
	// all staging must be complete by the time this update method is called //
	_force_field[COHERENT] = std::move(_force_field[STAGING]);
	_force_field[STAGING] = std::move(pOLD);
	_force_field[STAGING]->clear(); // reset for next stage!
}
	
XMVECTOR const __vectorcall	cPhysics::get_force(size_t const x, size_t const y, size_t const z) const
{
	XMVECTOR xmForce(XMVectorZero());
	
	//BETTER_ENUM(adjacency, uint32_t const,  // matching the same values to voxelModel.h values
	//	left = voxB::BIT_ADJ_LEFT,
	//	right = voxB::BIT_ADJ_RIGHT,
	//	front = voxB::BIT_ADJ_FRONT,
	//	back = voxB::BIT_ADJ_BACK,
	//	above = voxB::BIT_ADJ_ABOVE

	uint32_t adjacent(0);

	if (x - 1 >= 0) {
		uint32_t const direction = _force_field[COHERENT]->read_bit(x - 1, y, z) << Volumetric::adjacency::left;
		if (direction) {
			xmForce = XMVectorAdd(xmForce, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)); // adjacent to force on the left, outgoing force is left to right.
			adjacent |= direction;
		}
	}
	if (x + 1 < force_volume::width()) {
		uint32_t const direction = _force_field[COHERENT]->read_bit(x + 1, y, z) << Volumetric::adjacency::right;
		if (direction) {
			xmForce = XMVectorAdd(xmForce, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f)); // adjacent to force on the right, outgoing force is right to left.
			adjacent |= direction;
		}
	}
	if (z - 1 >= 0) {
		uint32_t const direction = _force_field[COHERENT]->read_bit(x, y, z - 1) << Volumetric::adjacency::front;
		if (direction) {
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)); // adjacent to force in front, outgoing force is front to back.
			adjacent |= direction;
		}
	}
	if (z + 1 < force_volume::depth()) {
		uint32_t const direction = _force_field[COHERENT]->read_bit(x, y, z + 1) << Volumetric::adjacency::back;
		if (direction) {
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)); // adjacent to force in back, outgoing force is back to front.
			adjacent |= direction;
		}
	}
	if ((Volumetric::adjacency::left | Volumetric::adjacency::right | Volumetric::adjacency::front | Volumetric::adjacency::back) == (adjacent & (Volumetric::adjacency::left | Volumetric::adjacency::right | Volumetric::adjacency::front | Volumetric::adjacency::back))) { // if all surrounding, introduce an upwards force
		xmForce = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // adjacent to force above, outgoing force is below to above.
	}
	if (y + 1 < force_volume::height()) {
		uint32_t const direction = _force_field[COHERENT]->read_bit(x, y + 1, z) << Volumetric::adjacency::above;
		if (direction) {
			xmForce = XMVectorAdd(xmForce, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f)); // adjacent to force above, outgoing force is above to below.
			adjacent |= direction;
		}
	}
	
	return(xmForce);	
}

void cPhysics::CleanUp()
{
	if (_force_field[STAGING]) {
		force_volume::destroy(_force_field[STAGING]); _force_field[STAGING] = nullptr;
	}

	if (_force_field[COHERENT]) {
		force_volume::destroy(_force_field[COHERENT]); _force_field[COHERENT] = nullptr;
	}	
}