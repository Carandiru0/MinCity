#include "pch.h"
#include "world.h"

#include "cYXIGameObject.h"
#include "cYXISphereGameObject.h"
#include "cThrusterFireGameObject.h"
#include "cBeaconGameObject.h"

#include "cLightGameObject.h"
#include "cLightConeGameObject.h"
#include "cImportGameObject.h"
//#include "cLevelSetGameObject.h"
#include "cExplosionGameObject.h"
#include "cHeliumGasGameObject.h"

#include "cImportGameObject.h"
//#include "cRemoteUpdateGameObject.h"
#include "cBuildingGameObject.h"
//#include "cTrafficSignGameObject.h"
//#include "cTrafficControlGameObject.h"
//#include "cPoliceCarGameObject.h"
//#include "cCarGameObject.h"
#include "cTestGameObject.h"
//#include "cSignageGameObject.h"
#include "ImageAnimation.h"

namespace world
{
	namespace access
	{
		void create_game_object(uint32_t const hash, uint32_t const gameobject_type, mapVoxelModelInstancesStatic&& __restrict static_instances, mapVoxelModelInstancesDynamic&& __restrict dynamic_instances)
		{
			// safe to mix dynamic and static //
			switch (gameobject_type)
			{
			case types::game_object_t::BuildingGameObject:
				cBuildingGameObject::emplace_back(std::move(static_instances[hash]));
				break;
			case types::game_object_t::TestGameObject:
				cTestGameObject::emplace_back(std::move(dynamic_instances[hash]));
				break;
			//case types::game_object_t::TrafficSignGameObject:
			//	cTrafficSignGameObject::emplace_back(std::move(dynamic_instances[hash]));
			//	break;
			//case types::game_object_t::TrafficControlGameObject:
			//	cTrafficControlGameObject::emplace_back(std::move(static_instances[hash]));
			//	break;
			//case types::game_object_t::SignageGameObject:
			//	cSignageGameObject::emplace_back(std::move(dynamic_instances[hash]));
			//	break;
			case types::game_object_t::LightGameObject:
				cLightGameObject::emplace_back(std::move(dynamic_instances[hash]));
				break;
			}

		} // end create

		void update_game_objects(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
		{
			{
				auto it = cYXIGameObject::begin();
				while (cYXIGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cYXISphereGameObject::begin();
				while (cYXISphereGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			
			// update all image animations //
			{
				auto it = ImageAnimation::begin();
				while (ImageAnimation::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			// update all dynamic/updateable game objects //

			{
				auto it = cLightConeGameObject::begin();
				while (cLightConeGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cThrusterFireGameObject::begin();
				while (cThrusterFireGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cBeaconGameObject::begin();
				while (cBeaconGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cExplosionGameObject::begin();
				while (cExplosionGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cHeliumGasGameObject::begin();
				while (cHeliumGasGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}

			// import //
			{
				auto it = cImportGameObject_Dynamic::begin();
				while (cImportGameObject_Dynamic::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			{
				auto it = cImportGameObject_Static::begin();
				while (cImportGameObject_Static::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			

			{
				// select buildings only update
				cBuildingGameObject::UpdateAll(tNow, tDelta);
			}
			/*
			{
				// traffic controllers - *should be done b4 cars* //
				auto it = cTrafficControlGameObject::begin();
				while (cTrafficControlGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
				::cCarGameObject::UpdateAll(tNow, tDelta);
				::cPoliceCarGameObject::UpdateAll(tNow, tDelta);
			}
			*/
			/*
			{
				auto it = cRemoteUpdateGameObject::begin();
				while (cRemoteUpdateGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			*/
			{
				auto it = cTestGameObject::begin();
				while (cTestGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			/*
			{
				auto it = cLevelSetGameObject::begin();
				while (cLevelSetGameObject::end() != it) {

					it->OnUpdate(tNow, tDelta);
					++it;
				}
			}
			*/

		} // end update

		void release_game_objects()
		{
			cImportGameObject_Dynamic::clear();
			cImportGameObject_Static::clear();
			cBuildingGameObject::clear();
			//cCarGameObject::clear();
			//cPoliceCarGameObject::clear();
			//cCopterPropGameObject::clear();
			//cCopterBodyGameObject::clear();
			//cRemoteUpdateGameObject::clear();
			cTestGameObject::clear();
			//cTrafficControlGameObject::clear();
			//cTrafficSignGameObject::clear();
			//cSignageGameObject::clear();
			cLightGameObject::clear();
		} // end release




	} // end ns
} // ends ns
