#pragma once

#ifndef TYPES_H
#define TYPES_H

#ifndef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#endif

#include <vector>
#include <tbb/tbb.h>
#include <Utility/scalable_aligned_allocator.h>

#ifdef vector
#undef vector
#endif

template<typename T>
using vector = std::vector<T, tbb::scalable_allocator<T>>;
template<typename T, size_t const alignment = alignof(T)>
using vector_aligned = std::vector<T, tbb::scalable_aligned_allocator<T, alignment>>;

#include "betterenums.h"

namespace world {
	
	namespace types {

		BETTER_ENUM(game_object_t, uint32_t const,
			NoOwner = 0, // bad number for enum, has no type
			NonUpdateable = 1, // basic
			Updateable = 2,	   // basic
			// add game objects as needed here : //
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
			SignageGameObject

		);

	} // end ns types
} // end ns world

#endif


