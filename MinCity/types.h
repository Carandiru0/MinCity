#pragma once

#ifndef TYPES_H
#define TYPES_H

#ifndef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#endif

#include <vector>
#include <unordered_set>
#include <tbb/tbb.h>
#include <Utility/scalable_aligned_allocator.h>

#ifdef vector
#undef vector
#endif
#ifdef unordered_set
#undef unordered_set
#endif

template<typename T>
using vector = std::vector<T, tbb::scalable_allocator<T>>;
template<typename T, size_t const alignment = alignof(T)>
using vector_aligned = std::vector<T, tbb::scalable_aligned_allocator<T, alignment>>;
template<typename T>
using unordered_set = std::unordered_set<T, std::hash<T>, std::equal_to<T>, tbb::scalable_allocator<T>>;

#include "betterenums.h"

namespace world {
	
	namespace types {

		BETTER_ENUM(game_object_t, uint32_t const,
			NonSaveable = 0, // bad number for enum, has no type excluded from serialization when saving
			NoOwner = 1, 
			NonUpdateable = 2, // basic
			Updateable = 3,	   // basic
			Procedural = 4,    // advanced
			// add game objects as needed here : //
			CharacterGameObject,
			ExplosionGameObject,
			BuildingGameObject,
			CarGameObject,
			PoliceCarGameObject,
			CopterPropGameObject,
			CopterBodyGameObject,
			RemoteUpdateGameObject,
			TestGameObject,
			VideoScreenGameObject,
			TrafficSignGameObject,
			TrafficControlGameObject,
			SignageGameObject,
			LightGameObject
		);

	} // end ns types
} // end ns world

#endif


