#include "pch.h"
#include "cUser.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "cYXIGameObject.h"
#include "cYXISphereGameObject.h"

#include "cPhysics.h"

static constexpr float const
	SPHERE_ORIGIN_OFFSET_X = 34.0f, // in minivoxels
	SPHERE_ORIGIN_OFFSET_Y = -10.0f,
	SPHERE_ORIGIN_OFFSET_Z = -7.0f;

cUser::cUser()
	: _ship(nullptr), _shipRingX{}, _shipRingY{}, _shipRingZ{},
	_sphere_engine_offset(XMVectorGetX(XMVector3Length(XMVectorScale(XMVectorSet(SPHERE_ORIGIN_OFFSET_X, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f), Iso::MINI_VOX_STEP * cPhysics::TORQUE_OFFSET_SCALAR))))
{
	// this method is called during new construction of a user which happens on any main onloaded event of voxelworld.
	// it is safe therefore to interact with the world (place instances etc) at this point (new map ready)

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

		// sphere engine offset:
		// displacement (offset from origin to edge is where force is applied, affecting the force directly, greater displacement means greater force) 
		// T = F x r (Torque) [not using cross product, already done, just scale]

		// sphere thruster engines running forces
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingX[i]->getAngularForce(), _sphere_engine_offset));
			_ship->applyAngularThrust(XMVectorScale(_shipRingY[i]->getAngularForce(), _sphere_engine_offset));
			_ship->applyAngularThrust(XMVectorScale(_shipRingZ[i]->getAngularForce(), _sphere_engine_offset));
		}

		MinCity::VoxelWorld->updateCameraFollow(instance->getLocation(), _ship->getVelocity(), XMVectorScale(XMLoadFloat3A(&instance->getModel()._Extents), Iso::MINI_VOX_STEP), tDelta);
	}
}
void cUser::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	static constexpr float const
		SPHERE_THRUST = 0.5f,
		MAIN_THRUST = 4.0f;

	float const direction(down ? 1.0f : -1.0f);
	
	// applied thrust (down)
	// counter control thrust (up)

	// sphere engine offset:
	// displacement (offset from origin to edge is where force is applied, affecting the force directly, greater displacement means greater force) 
	// T = F x r (Torque) [not using cross product, already done, just scale]

	switch (key) // keys are filtered before this function in cUserInterface, which is good as here a known key and down event means there is no conditional needed on the forces applied after - it is always the case.
	{
	case GLFW_KEY_HOME:
	case GLFW_KEY_R:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingX[i]->applyAngularThrust(XMVectorSet(direction * SPHERE_THRUST, 0.0f, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // pitch - sphere thruster engines
		break;
	case GLFW_KEY_END:
	case GLFW_KEY_F:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingX[i]->applyAngularThrust(XMVectorSet(-direction * SPHERE_THRUST, 0.0f, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // pitch - sphere thruster engines
		break;

	case GLFW_KEY_LEFT:
	case GLFW_KEY_A:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, direction * SPHERE_THRUST, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // yaw - sphere thruster engines
		break;
	case GLFW_KEY_RIGHT:
	case GLFW_KEY_D:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, -direction * SPHERE_THRUST, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // yaw - sphere thruster engines
		break;

	case GLFW_KEY_DELETE:
	case GLFW_KEY_Q:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, -direction * SPHERE_THRUST, 0.0f), !down), _sphere_engine_offset));
		} // roll - sphere thruster engines
		break;
	case GLFW_KEY_PAGE_DOWN:
	case GLFW_KEY_E:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, direction * SPHERE_THRUST, 0.0f), !down), _sphere_engine_offset));
		} // roll - sphere thruster engines
		break;
	

	case GLFW_KEY_UP:
	case GLFW_KEY_W:
		_ship->applyThrust(XMVectorScale(XMVectorSet(0.0f, 0.0f, MAIN_THRUST, 0.0f), _ship->getMass()), true); // main thruster
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