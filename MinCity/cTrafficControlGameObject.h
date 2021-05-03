#pragma once

#include "cUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include <vector>

// forward decl
namespace world
{
	class cTrafficSignGameObject;
} // end ns;

namespace world
{
	class cTrafficControlGameObject : public tUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cTrafficControlGameObject>
	{
		typedef struct cTrafficSignSet
		{
			cTrafficSignGameObject* sign[2];

			cTrafficSignSet(cTrafficSignGameObject* const& signA, cTrafficSignGameObject* const& signB)
				: sign{ signA, signB }
			{}

		} cTrafficSignSet;

	public:
#ifndef NDEBUG
		// every child of this class should override to_string with approprate string
		virtual std::string_view const to_string() const override { return("cTrafficControlGameObject"); }
#endif
		void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

		void Add(cTrafficSignGameObject* const& sign);
		void Remove(cTrafficSignGameObject* const& sign);
	private:
		void deduce_sign_pairs();

	public:
		cTrafficControlGameObject(cTrafficControlGameObject&& src) noexcept;
		cTrafficControlGameObject& operator=(cTrafficControlGameObject&& src) noexcept;
	private:
		fp_seconds					_accumulator,
									_interval,
									_blink_accumulator;

		cTrafficSignGameObject*		_signs[4];
		uint32_t					_sign_count;
		
		uint32_t _counter[2],
				 _state[2];
		
		bool	 _turning_side[2];

		using set_sign_pair = std::vector<cTrafficSignSet>;
		set_sign_pair				_sign_pair;
	public:
		cTrafficControlGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
	};

	STATIC_INLINE_PURE void swap(cTrafficControlGameObject& __restrict left, cTrafficControlGameObject& __restrict right) noexcept
	{
		cTrafficControlGameObject tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

 } // end ns


