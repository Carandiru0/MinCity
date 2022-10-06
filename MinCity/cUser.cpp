#include "pch.h"
#include "cUser.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

#include "cYXIGameObject.h"
#include "cYXISphereGameObject.h"
#include "cLightConeGameObject.h"
#include "cPhysics.h"

#include "cExplosionGameObject.h"
#include "cBeaconGameObject.h"

static constexpr float const
	SPHERE_ORIGIN_OFFSET_X = 34.0f, // in minivoxels
	SPHERE_ORIGIN_OFFSET_Y = -10.0f,
	SPHERE_ORIGIN_OFFSET_Z = -7.0f,

	LIGHT_CONE_ORIGIN_OFFSET_X = 0.5f,
	LIGHT_CONE_ORIGIN_OFFSET_Y = -100.0f,
	LIGHT_CONE_ORIGIN_OFFSET_Z = 124.0f;

static constexpr uint64_t const USER_ID_CATEGORY{ 0xFEEDFEEDFEEDFEED }; // always hash these type of constants before usage
constinit uint64_t cUser::user_count(0);

cUser::cUser()
	: _ship(nullptr), _shipRingX{}, _shipRingY{}, _shipRingZ{}, _light_cone(nullptr), _destroyed(true),
	_sphere_engine_offset(XMVectorGetX(XMVector3Length(XMVectorScale(XMVectorSet(SPHERE_ORIGIN_OFFSET_X, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f), Iso::MINI_VOX_STEP * cPhysics::TORQUE_OFFSET_SCALAR)))),
	_total_mass(0.0f), _beacon_accumulator{}, _beacon_loaded(false)
{
	// this method is called during new construction of a user which happens on any main onloaded event of voxelworld.
	// it is safe therefore to interact with the world (place instances etc) at this point (new map ready)

	// use create() instead!!
}

void cUser::create()
{
	_id = user_count + 1;
	
	uint64_t const key = Hash(_id, Hash((int64_t)USER_ID_CATEGORY)); // all further hashes are encoded to this key.

	point2D_t const spawnOrigin;

	_ship = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXIGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOrigin,
		Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
	_ship->setParent(this);
	_shipAlias = Hash((int64_t)(uintptr_t)_ship->getModelInstance(), key);

	for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {

		float const offset_to_engine(0 == i ? -SPHERE_ORIGIN_OFFSET_X : SPHERE_ORIGIN_OFFSET_X);
		point2D_t const spawnOriginOffset(p2D_add(spawnOrigin, point2D_t((int32_t)offset_to_engine / (int32_t)MINIVOXEL_FACTOR, 0)));

		_shipRingX[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_X, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingX[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));
		_shipRingXAlias[i] = Hash((int64_t)(uintptr_t)_shipRingX[i]->getModelInstance(), key);

		_shipRingY[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_Y, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingY[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));
		_shipRingYAlias[i] = Hash((int64_t)(uintptr_t)_shipRingY[i]->getModelInstance(), key);
		_shipRingY[i]->enableThrusterFire(_ship->getThrusterPower(1));

		_shipRingZ[i] = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cYXISphereGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOriginOffset,
			Volumetric::eVoxelModel::DYNAMIC::NAMED::YXI_RING_Z, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
		_shipRingZ[i]->setParent(_ship, XMVectorSet(offset_to_engine, SPHERE_ORIGIN_OFFSET_Y, SPHERE_ORIGIN_OFFSET_Z, 0.0f));
		_shipRingZAlias[i] = Hash((int64_t)(uintptr_t)_shipRingZ[i]->getModelInstance(), key);
	}

	_light_cone = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cLightConeGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(spawnOrigin,
		Volumetric::eVoxelModel::DYNAMIC::NAMED::LIGHT_CONE, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
	_light_cone->setParent(_ship, XMVectorSet(LIGHT_CONE_ORIGIN_OFFSET_X, LIGHT_CONE_ORIGIN_OFFSET_Y, LIGHT_CONE_ORIGIN_OFFSET_Z, 0.0f));
	_light_cone_alias = Hash((int64_t)(uintptr_t)_light_cone->getModelInstance(), key);
	_light_cone->setPitch(v2_rotation_constants::v45);

	float mass = _ship->getMass();
	for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
		mass += _shipRingX[i]->getMass();
		mass += _shipRingY[i]->getMass();
		mass += _shipRingZ[i]->getMass();
	}
	_total_mass = voxels_to_kg(mass);

	// last
	_destroyed = false;
	++user_count;
}

void cUser::destroy() // function must be re-entrable and not affect state unless neccessary
{
	// notified of model instance destroyed

	if (!_destroyed) {
		// all state is invalid, destruction sequence has begun
		_destroyed = true;

		uint64_t const key = Hash(_id, Hash((int64_t)USER_ID_CATEGORY)); // all further hashes are decoded from this key.

		// game objects are no longer valid. game objects are automatically released when they're associated model instance gets destroyed.
		if (_light_cone) {

			if (Hash((uintptr_t)_light_cone->getModelInstance(), key) == _light_cone_alias) {
				_light_cone->getModelInstance()->destroy();

				auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_light_cone->getModelInstance()->getVoxelIndex(),
					Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
				if (instance) {
					instance->setElevation(_light_cone->getModelInstance()->getElevation());
				}
			}

			_light_cone = nullptr;
		}
		if (_ship) {

			if (Hash((uintptr_t)_ship->getModelInstance(), key) == _shipAlias) {
				_ship->getModelInstance()->destroy();

				auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_ship->getModelInstance()->getVoxelIndex(),
					Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
				if (instance) {
					instance->setElevation(_ship->getModelInstance()->getElevation());
				}
			}

			_ship = nullptr;
		}
	
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {

			if (_shipRingX[i]) {
				if (Hash((uintptr_t)_shipRingX[i]->getModelInstance(), key) == _shipRingXAlias[i]) {
					_shipRingX[i]->getModelInstance()->destroy();

					auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_shipRingX[i]->getModelInstance()->getVoxelIndex(),
						Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
					if (instance) {
						instance->setElevation(_shipRingX[i]->getModelInstance()->getElevation());
					}
				}

				_shipRingX[i] = nullptr;
			}

			if (_shipRingY[i]) {
				if (Hash((uintptr_t)_shipRingY[i]->getModelInstance(), key) == _shipRingYAlias[i]) {
					_shipRingY[i]->getModelInstance()->destroy();

					auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_shipRingY[i]->getModelInstance()->getVoxelIndex(),
						Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
					if (instance) {
						instance->setElevation(_shipRingY[i]->getModelInstance()->getElevation());
					}
				}

				_shipRingY[i] = nullptr;
			}

			if (_shipRingZ[i]) {
				if (Hash((uintptr_t)_shipRingZ[i]->getModelInstance(), key) == _shipRingZAlias[i]) {
					_shipRingZ[i]->getModelInstance()->destroy();

					auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cExplosionGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_shipRingZ[i]->getModelInstance()->getVoxelIndex(),
						Volumetric::eVoxelModel::DYNAMIC::NAMED::TINY_EXPLOSION, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING);
					if (instance) {
						instance->setElevation(_shipRingZ[i]->getModelInstance()->getElevation());
					}
				}

				_shipRingZ[i] = nullptr;
			}
		}
	}
}

bool const cUser::secure_validation() const
{
	uint64_t const key = Hash(_id, Hash((int64_t)USER_ID_CATEGORY)); // all further hashes are decoded from this key.

	// game objects are no longer valid. game objects are automatically released when they're associated model instance gets destroyed.
	if (_light_cone) {

		if (Hash((uintptr_t)_light_cone->getModelInstance(), key) != _light_cone_alias) {
			return(false);
		}
	}
	if (_ship) {

		if (Hash((uintptr_t)_ship->getModelInstance(), key) != _shipAlias) {
			return(false);
		}
	}

	for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {

		if (_shipRingX[i]) {
			if (Hash((uintptr_t)_shipRingX[i]->getModelInstance(), key) != _shipRingXAlias[i]) {
				return(false);
			}
		}

		if (_shipRingY[i]) {
			if (Hash((uintptr_t)_shipRingY[i]->getModelInstance(), key) != _shipRingYAlias[i]) {
				return(false);
			}
		}

		if (_shipRingZ[i]) {
			if (Hash((uintptr_t)_shipRingZ[i]->getModelInstance(), key) != _shipRingZAlias[i]) {
				return(false);
			}
		}
	}

	return(true); // only what exists can be validated
}

void cUser::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	if (_destroyed) {
		return;
	}

	if (!secure_validation()) {
		destroy();
		return;
	}

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

			_shipRingY[i]->updateThrusterFire(_ship->getThrusterPower(1));
		}

		// gravity
		_ship->applyForce(XMVectorSet(0.0f, _total_mass * cPhysics::GRAVITY, 0.0f, 0.0f));

		_beacon_accumulator += tDelta;
		if (_beacon_accumulator >= BEACON_LAUNCH_INTERVAL) {
			_beacon_accumulator -= BEACON_LAUNCH_INTERVAL;
			_beacon_loaded = true;
		}

		MinCity::VoxelWorld->updateCameraFollow(instance->getLocation(), _ship->getVelocity(), instance, tDelta);
	}
}

void cUser::Paint()
{
	

}


void cUser::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	if (_destroyed) {
		return;
	}

	static constexpr float const
		SPHERE_THRUST = world::cYXISphereGameObject::MAX_THRUST,
		MAIN_THRUST = world::cYXIGameObject::MAX_MAIN_THRUST,
		UP_THRUST = world::cYXIGameObject::MAX_UP_THRUST;

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
			_ship->applyAngularThrust(XMVectorScale(_shipRingX[i]->applyAngularThrust(XMVectorSet(direction * SPHERE_THRUST, 0.0f, 0.0f, 0.0f), !down), _sphere_engine_offset)); // T = F x r
		} // pitch (x) - sphere thruster engines
		break;
	case GLFW_KEY_END:
	case GLFW_KEY_F:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingX[i]->applyAngularThrust(XMVectorSet(-direction * SPHERE_THRUST, 0.0f, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // pitch (x) - sphere thruster engines
		break;

	case GLFW_KEY_LEFT:
	case GLFW_KEY_A:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, -direction * SPHERE_THRUST, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // yaw (y) - sphere thruster engines
		break;
	case GLFW_KEY_RIGHT:
	case GLFW_KEY_D:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingY[i]->applyAngularThrust(XMVectorSet(0.0f, direction * SPHERE_THRUST, 0.0f, 0.0f), !down), _sphere_engine_offset));
		} // yaw (y) - sphere thruster engines
		break;

	case GLFW_KEY_DELETE:
	case GLFW_KEY_Q:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, direction * SPHERE_THRUST, 0.0f), !down), _sphere_engine_offset));
		} // roll (z) - sphere thruster engines
		break;
	case GLFW_KEY_PAGE_DOWN:
	case GLFW_KEY_E:
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			_ship->applyAngularThrust(XMVectorScale(_shipRingZ[i]->applyAngularThrust(XMVectorSet(0.0f, 0.0f, -direction * SPHERE_THRUST, 0.0f), !down), _sphere_engine_offset));
		} // roll (z) - sphere thruster engines
		break;
	

	case GLFW_KEY_UP:
	case GLFW_KEY_W: // forward main thrust
		_ship->applyThrust(XMVectorScale(XMVectorSet(0.0f, 0.0f, MAIN_THRUST, 0.0f), _ship->getMass())); // main thruster
		break;
	case GLFW_KEY_DOWN:
	case GLFW_KEY_S: // up thrust
		_ship->applyThrust(XMVectorScale(XMVectorSet(0.0f, UP_THRUST, 0.0f, 0.0f), _ship->getMass())); // up thruster
		for (uint32_t i = 0; i < ENGINE_COUNT; ++i) {
			if (_ship->getThrusterPower(1) <= 0.0f) {}
			_shipRingY[i]->enableThrusterFire(_ship->getThrusterPower(1));
		}
		break;
	case GLFW_KEY_SPACE:
		break;
	case GLFW_KEY_B: // beacon
		{
		    if (_beacon_loaded) {

				auto instance = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cBeaconGameObject, Volumetric::eVoxelModels_Dynamic::NAMED>(_ship->getModelInstance()->getVoxelIndex(),
					Volumetric::eVoxelModel::DYNAMIC::NAMED::BEACON, Volumetric::eVoxelModelInstanceFlags::NOT_FADEABLE | Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING | Volumetric::eVoxelModelInstanceFlags::GROUND_CONDITIONING);
				if (instance) {
					_beacon_loaded = false;
					_beacon_accumulator = zero_time_duration;
				}
			}
		}
		break;
	}
}

cUser::~cUser()
{
	destroy();
	--user_count;
}