#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

- attempted interpolation on v2_rotation_t, does not work due to way vector interpolations work - not worth it
- only one Interpolator.set(xxx, value) can occur for xxx per frame, otherwise the last value is overwritten incorrectly and last becomes target rather than the current value. Only SET once / frame the interpolated<> value thru Interpolator.set()

*/

#include "globals.h"
#include <tbb/tbb.h>
#include <Math/superfastmath.h>
#include <vector>

#define COMPONENT_X 0u
#define COMPONENT_Y 1u
#define COMPONENT_Z 2u
#define COMPONENT_W 3u

template<typename T>   // replace your stored data structure with this
struct interpolated
{
	friend class interpolator; // * only Interpolator can access index or directly modify the value *

private:
	T			value;
	size_t		index;

public: // only constant access
	operator T const& () const {
		return(value);
	}

	interpolated()
		: value{}, index{}
	{}
};

// if it crashes, there was no removal of the interpolator before or during the objects destruction. Any object or usage of an interpolator requires the owner to remove the interpolator when it is destroyed. 
class alignas(CACHE_LINE_BYTES) interpolator
{
	static constexpr uint32_t const
		INTERP_COMPONENT_X = 1 << COMPONENT_X,
		INTERP_COMPONENT_Y = 1 << COMPONENT_Y,
		INTERP_COMPONENT_Z = 1 << COMPONENT_Z,
		INTERP_COMPONENT_W = 1 << COMPONENT_W;

private:
	template<typename T> // type must be compatible with SFM::lerp, float or XMFLOAT4A, 3A, etc
	struct range
	{
		T					last, target;
		interpolated<T>*	current;
		uint32_t            interp;

		void __vectorcall lerp(float const t) {
			if constexpr (std::is_same<T,XMFLOAT4A>::value) {
				XMStoreFloat4A(&current->value, SFM::lerp(XMLoadFloat4A(&last), XMLoadFloat4A(&target), t)); interp = (INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z | INTERP_COMPONENT_W);
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				XMStoreFloat3A(&current->value, SFM::lerp(XMLoadFloat3A(&last), XMLoadFloat3A(&target), t)); interp = (INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z);
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				XMStoreFloat2A(&current->value, SFM::lerp(XMLoadFloat2A(&last), XMLoadFloat2A(&target), t)); interp = (INTERP_COMPONENT_X | INTERP_COMPONENT_Y);
			}
			else {
				current->value = SFM::lerp(last, target, t); interp = INTERP_COMPONENT_X;
			}
		}

		void __vectorcall set(FXMVECTOR target_) {

			if constexpr (std::is_same<T, XMFLOAT4A>::value) {
				if ((INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z | INTERP_COMPONENT_W) & interp) {
					XMStoreFloat4A(&last, XMLoadFloat4A(&current->value));
					XMStoreFloat4A(&current->value, XMLoadFloat4A(&target));
				}
				XMStoreFloat4A(&target, target_);
				interp = 0; // reset
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				if ((INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z) & interp) {
					XMStoreFloat3A(&last, XMLoadFloat3A(&current->value));
					XMStoreFloat3A(&current->value, XMLoadFloat3A(&target));
				}
				XMStoreFloat3A(&target, target_);
				interp = 0; // reset
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				if ((INTERP_COMPONENT_X | INTERP_COMPONENT_Y) & interp) {
					XMStoreFloat2A(&last, XMLoadFloat2A(&current->value));
					XMStoreFloat2A(&current->value, XMLoadFloat2A(&target));
				}
				XMStoreFloat2A(&target, target_);
				interp = 0; // reset
			}

		 	// last becomes what is current
			// current always transitions to target
			// set new target
		}

		void set(float const& target_) {
			if (INTERP_COMPONENT_X & interp) {
				last = current->value;			// last becomes what is current
				current->value = target;		// current always transitions to target
			}
			target = target_;				// set new target
			interp = 0; // reset
		}

		template<uint32_t const component> 
		void set_component(float const target_)
		{
			if constexpr (COMPONENT_X == component) {
				if (INTERP_COMPONENT_X & interp) {
					last.x = current->value.x;
					current->value.x = target.x;
				}
				target.x = target_;
				interp &= ~INTERP_COMPONENT_X;
			}
			else if constexpr (COMPONENT_Y == component) {
				if (INTERP_COMPONENT_Y & interp) {
					last.y = current->value.y;
					current->value.y = target.y;
				}
				target.y = target_;
				interp &= ~INTERP_COMPONENT_Y;
			}
			else if constexpr (COMPONENT_Z == component) {
				if (INTERP_COMPONENT_Z & interp) {
					last.z = current->value.z;
					current->value.z = target.z;
				}
				target.z = target_;
				interp &= ~INTERP_COMPONENT_Z;
			}
			else if constexpr (COMPONENT_W == component) {
				if (INTERP_COMPONENT_W & interp) {
					last.w = current->value.w;
					current->value.w = target.w;
				}
				target.w = target_;
				interp &= ~INTERP_COMPONENT_W;
			}
		}

		void __vectorcall reset(FXMVECTOR all_) {

			if constexpr (std::is_same<T, XMFLOAT4A>::value) {
				XMStoreFloat4A(&last, all_);
				XMStoreFloat4A(&current->value, all_);
				XMStoreFloat4A(&target, all_);
				interp |= (INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z | INTERP_COMPONENT_W);
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				XMStoreFloat3A(&last, all_);
				XMStoreFloat3A(&current->value, all_);
				XMStoreFloat3A(&target, all_);
				interp |= (INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z);
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				XMStoreFloat2A(&last, all_);
				XMStoreFloat2A(&current->value, all_);
				XMStoreFloat2A(&target, all_);
				interp |= (INTERP_COMPONENT_X | INTERP_COMPONENT_Y);
			}
		}
		void reset(float const& all_) {
			last = target = current->value = all_; interp = INTERP_COMPONENT_X;
		}

		template<uint32_t component>
		void reset_component(float const all_) {
			
			if constexpr (COMPONENT_X == component) {
				last.x = target.x = current->value.x = all_;
				interp |= INTERP_COMPONENT_X;
			}
			else if constexpr (COMPONENT_Y == component) {
				last.y = target.y = current->value.y = all_;
				interp |= INTERP_COMPONENT_Y;
			}
			else if constexpr (COMPONENT_Z == component) {
				last.z = target.z = current->value.z = all_;
				interp |= INTERP_COMPONENT_Z;
			}
			else if constexpr (COMPONENT_W == component) {
				last.w = target.w = current->value.w = all_;
				interp |= INTERP_COMPONENT_W;
			}
		}

		range(interpolated<T>* const initial)
			: current{initial}, last{*initial}, target{*initial}, interp(INTERP_COMPONENT_X | INTERP_COMPONENT_Y | INTERP_COMPONENT_Z | INTERP_COMPONENT_W) // initial
		{}
	};


public:
	template <typename T>
	void set(interpolated<T> const& source, T const target) { _finterpolators[source.index].set(target); } // index must always be within bounds of vector
	
	template <typename T>
	void __vectorcall set(interpolated<T> const& source, XMVECTOR const target) {

		if constexpr (std::is_same<T, XMFLOAT4A>::value) {
			_v4interpolators[source.index].set(target);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].set(target);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].set(target);
		}
	} // index must always be within bounds of vector

	template<uint32_t component, typename T>
	void set_component(interpolated<T> const& source, float const target_)
	{
		if constexpr (std::is_same<T, XMFLOAT4A>::value) {
			_v4interpolators[source.index].template set_component<component>(target_);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].template set_component<component>(target_);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].template set_component<component>(target_);
		}
	}

	template <typename T>
	void reset(interpolated<T> const& source, T const all) { _finterpolators[source.index].reset(all); } // index must always be within bounds of vector

	template <typename T>
	void __vectorcall reset(interpolated<T> const& source, XMVECTOR const all) {

		if constexpr (std::is_same<T, XMFLOAT4A>::value) {
			_v4interpolators[source.index].reset(all);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].reset(all);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].reset(all);
		}
	} // index must always be within bounds of vector

	template<uint32_t component, typename T>
	void reset_component(interpolated<T> const& source, float const all_)
	{
		if constexpr (std::is_same<T, XMFLOAT4A>::value) {
			_v4interpolators[source.index].template reset_component<component>(all_);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].template reset_component<component>(all_);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].template reset_component<component>(all_);
		}
	}

	template <typename T>
	void push(interpolated<T>& initial) {

		size_t const index(_finterpolators.size());
		_finterpolators.emplace_back(&initial);
		initial.index = index;
	}
	template <>
	void push<XMFLOAT4A>(interpolated<XMFLOAT4A>& initial) {

		size_t const index(_v4interpolators.size());
		_v4interpolators.emplace_back(&initial);
		initial.index = index;
	}
	template <>
	void push<XMFLOAT3A>(interpolated<XMFLOAT3A>& initial) {

		size_t const index(_v3interpolators.size());
		_v3interpolators.emplace_back(&initial);
		initial.index = index;
	}
	template <>
	void push<XMFLOAT2A>(interpolated<XMFLOAT2A>& initial) {

		size_t const index(_v2interpolators.size());
		_v2interpolators.emplace_back(&initial);
		initial.index = index;
	}

	template <typename T, typename U = T>
	U const __vectorcall get(interpolated<T> const& source) {
		return(_finterpolators[source.index].target); 
	} // index must always be within bounds of vector

	template <>
	XMVECTOR const __vectorcall get<XMFLOAT4A, XMVECTOR>(interpolated<XMFLOAT4A> const& source) {
		return(XMLoadFloat4A(&_v4interpolators[source.index].target));
	} // index must always be within bounds of vector

	template <>
	XMVECTOR const __vectorcall get<XMFLOAT3A, XMVECTOR>(interpolated<XMFLOAT3A> const& source) {
		return(XMLoadFloat3A(&_v3interpolators[source.index].target));
	} // index must always be within bounds of vector

	template <>
	XMVECTOR const __vectorcall get<XMFLOAT2A, XMVECTOR>(interpolated<XMFLOAT2A> const& source) {
		return(XMLoadFloat2A(&_v2interpolators[source.index].target));
	} // index must always be within bounds of vector

private:

	template<typename T>
	class unordered_vector_special : public std::vector<range<T>, tbb::cache_aligned_allocator<range<T>>>
	{
	public:
		void const remove(size_t const index)	// efficient removal and erasure of singular element : https://stackoverflow.com/questions/39912/how-do-i-remove-an-item-from-a-stl-vector-with-a-certain-value
		{
			if (index < this->size()) {

				auto it(this->begin() + index);

				std::swap(*it, this->back());

				if (this->end() != it) {
					it->current->index = index; // update the index of the moved instance
				}

				this->pop_back();
			}
		}
	};

	unordered_vector_special<XMFLOAT4A>		_v4interpolators;
	unordered_vector_special<XMFLOAT3A>		_v3interpolators;
	unordered_vector_special<XMFLOAT2A>		_v2interpolators;
	unordered_vector_special<float>			_finterpolators;

public:

	template<typename T>
	void remove(interpolated<T> const& source) {

		if constexpr (std::is_same<T, XMFLOAT4A>::value) {
			_v4interpolators.remove(source.index);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators.remove(source.index);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators.remove(source.index);
		}
		else {
			_finterpolators.remove(source.index);
		}
	}
	void interpolate(float const t) {

		//tbb::parallel_invoke( // better distribution, was too fine grained before - disabled threading - not effective, not enough work says microprofiler
			/*[&]*/ {
				// 4 component vectors
				size_t const count(_v4interpolators.size());
				for (size_t i = 0; i < count; ++i) {
					_v4interpolators[i].lerp(t);
				}
			}//,
			/*[&]*/ {
				// 3 component vectors
				size_t const count(_v3interpolators.size());
				for (size_t i = 0; i < count; ++i) {
					_v3interpolators[i].lerp(t);
				}
			}//,
			/*[&]*/ {
				// 2 component vectors
				size_t const count(_v2interpolators.size());
				for (size_t i = 0; i < count; ++i) {
					_v2interpolators[i].lerp(t);
				}
			}//,
			/*[&]*/ {
				// 1 component vectors
				size_t const count(_finterpolators.size());
				for (size_t i = 0; i < count; ++i) {
					_finterpolators[i].lerp(t);
				}
			}
		//);
	}

public:
	interpolator() = default;
	~interpolator() = default;
}; // singleton global instance
__declspec(selectany) extern inline interpolator Interpolator{};

