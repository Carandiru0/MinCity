#include "pch.h"
#include "cUser.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "cYXIGameObject.h"
#include "cYXISphereGameObject.h"

cUser::cUser()
	: _ship(nullptr), _shipSphereLeft(nullptr), _shipSphereRight(nullptr)
{
	// this method is called during new construction of a user which happens on any main onloaded event of voxelworld.
	// it is safe therefore to interact with the world (place instances etc) at this point (new map ready)

	static constexpr float const
		SPHERE_ORIGIN_OFFSET_X = 34.0f, // in minivoxels
		SPHERE_ORIGIN_OFFSET_Y = -10.0f,
		SPHERE_ORIGIN_OFFSET_Z = -7.0f;

	point2D_t const spawnOrigin(MinCity::VoxelWorld->getVisibleGridCenter());

	_ship = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXIGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOrigin,
		Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);

	_shipSphereLeft = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOrigin,
		Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_SPHERE, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
	_shipSphereLeft->setParent(_ship, XMVectorSet(-SPHERE_ORIGIN_OFFSET_X, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));

	_shipSphereRight = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOrigin,
		Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_SPHERE, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
	_shipSphereRight->setParent(_ship, XMVectorSet(SPHERE_ORIGIN_OFFSET_X, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));
}

void cUser::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{

}
void cUser::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	if (down) {

		XMVECTOR xmThrust(XMVectorZero()), xmAngularThrust(XMVectorZero());

		switch (key) // keys are filtered before this function in cUserInterface, which is good as here a known key and down event means there is no conditional needed on the forces applied after - it is always the case.
		{
		case GLFW_KEY_LEFT:  // Joystick
			xmAngularThrust = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			break;
		case GLFW_KEY_RIGHT:
			xmAngularThrust = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
			break;
		case GLFW_KEY_UP: // Throttle
			xmThrust = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			break;
		case GLFW_KEY_DOWN:
			xmThrust = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
			break;
		}

		XMVECTOR xmSphereAngularForce;

		xmSphereAngularForce = _shipSphereLeft->applyThrust(xmAngularThrust);
		_ship->applyAngularThrust(xmSphereAngularForce);

		xmSphereAngularForce = _shipSphereRight->applyThrust(xmAngularThrust);
		_ship->applyAngularThrust(xmSphereAngularForce);

		_ship->applyThrust(XMVectorScale(xmThrust, _ship->getMass()));
	}
}

cUser::~cUser()
{
}