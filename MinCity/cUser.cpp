#include "pch.h"
#include "cUser.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "cYXIGameObject.h"
#include "cYXISphereGameObject.h"

cUser::cUser()
	: _ship(nullptr), _shipRingX{}, _shipRingY{}, _shipRingZ{}
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

	for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {

		float const offset_to_engine(0 == i ? -SPHERE_ORIGIN_OFFSET_X : SPHERE_ORIGIN_OFFSET_X);
		point2D_t const spawnOriginOffset(p2D_add(spawnOrigin, point2D_t((int32_t)offset_to_engine / (int32_t)MINIVOXEL_FACTOR, 0)));

		_shipRingX[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_X, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingX[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));

		_shipRingY[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_Y, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingY[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));

		_shipRingZ[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_Z, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingZ[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));

	}
}

void cUser::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	auto instance(_ship->getModelInstance());
	if (instance) {

		// sphere thruster engines running forces
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingX[i]->getAngularForce());
			_ship->applyAngularThrust(_shipRingY[i]->getAngularForce());
			_ship->applyAngularThrust(_shipRingZ[i]->getAngularForce());
		}

		MinCity::VoxelWorld->updateCameraFollow(instance->getLocation());
	}
}
void cUser::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	float const direction(down ? 1.0f : -1.0f);
	
	// applied thrust (down)
	// counter control thrust (up)

	switch (key) // keys are filtered before this function in cUserInterface, which is good as here a known key and down event means there is no conditional needed on the forces applied after - it is always the case.
	{
	case GLFW_KEY_HOME:
	case GLFW_KEY_R:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingX[i]->applyAngularThrust(XMVectorSet(direction, 0.0f, 0.0f, 0.0f), !down));
		} // roll - sphere thruster engines
		break;
	case GLFW_KEY_END:
	case GLFW_KEY_F:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingX[i]->applyAngularThrust(XMVectorSet(-direction, 0.0f, 0.0f, 0.0f), !down));
		} // roll - sphere thruster engines
		break;

	case GLFW_KEY_LEFT:
	case GLFW_KEY_A:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, direction, 0.0f, 0.0f), !down));
		} // yaw - sphere thruster engines
		break;
	case GLFW_KEY_RIGHT:
	case GLFW_KEY_D:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, -direction, 0.0f, 0.0f), !down));
		} // yaw - sphere thruster engines
		break;

	case GLFW_KEY_PAGE_DOWN:
	case GLFW_KEY_E:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, direction, 0.0f), !down));
		} // pitch - sphere thruster engines
		break;
	case GLFW_KEY_DELETE:
	case GLFW_KEY_Q:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, -direction, 0.0f), !down));
		} // pitch - sphere thruster engines
		break;

	case GLFW_KEY_UP:
	case GLFW_KEY_W:
		_ship->applyThrust(XMVectorScale(XMVectorSet(0.0f, 0.0f, 4.0f, 0.0f), _ship->getMass()), true); // main thruster
		break;
	case GLFW_KEY_DOWN:
	case GLFW_KEY_S:
		// reverse thrust - sphere thruster engines
		// @TODO
		break;
	case GLFW_KEY_SPACE:
		break;
	}
}

cUser::~cUser()
{
}