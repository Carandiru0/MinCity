#pragma once
#include "globals.h"
#include <tbb/tbb.h>
#include <Math/superfastmath.h>
#include <vector>

#define COMPONENT_X 0
#define COMPONENT_Y 1
#define COMPONENT_Z 2
#define COMPONENT_W 3

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
class interpolator
{
private:
	template<typename T> // type must be compatible with SFM::lerp, float or XMFLOAT4A, 3A, etc
	struct range
	{
		T					last, target;
		interpolated<T>*	current;

		void __vectorcall lerp(float const t) {
			if constexpr (std::is_same<T,XMFLOAT4A>::value) {
				XMStoreFloat4A(&current->value, SFM::lerp(XMLoadFloat4A(&last), XMLoadFloat4A(&target), t));
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				XMStoreFloat3A(&current->value, SFM::lerp(XMLoadFloat3A(&last), XMLoadFloat3A(&target), t));
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				XMStoreFloat2A(&current->value, SFM::lerp(XMLoadFloat2A(&last), XMLoadFloat2A(&target), t));
			}
			else {
				current->value = SFM::lerp(last, target, t);
			}
		}

		void set(T const& target_) {

			if (target_ != target) {
				last = current->value;			// last becomes what is current
				current->value = target;		// current always transitions to target
				target = target_;				// set new target
			}
		}

		void __vectorcall set(FXMVECTOR target_) {

			if constexpr (std::is_same<T, XMFLOAT4A>::value) {
				//if (XMVector4NotEqual(target_, XMLoadFloat4A(&target))) {
					XMStoreFloat4A(&last, XMLoadFloat4A(&current->value));
					XMStoreFloat4A(&current->value, XMLoadFloat4A(&target));
					XMStoreFloat4A(&target, target_);
				//}
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				//if (XMVector3NotEqual(target_, XMLoadFloat3A(&target))) {
					XMStoreFloat3A(&last, XMLoadFloat3A(&current->value));
					XMStoreFloat3A(&current->value, XMLoadFloat3A(&target));
					XMStoreFloat3A(&target, target_);
				//}
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				//if (XMVector2NotEqual(target_, XMLoadFloat2A(&target))) {
					XMStoreFloat2A(&last, XMLoadFloat2A(&current->value));
					XMStoreFloat2A(&current->value, XMLoadFloat2A(&target));
					XMStoreFloat2A(&target, target_);
				//}
			}

		 	// last becomes what is current
			// current always transitions to target
			// set new target
		}

		template<uint32_t const component> 
		void set_component(float const target_)
		{
			if constexpr (COMPONENT_X == component) {
				//if (target_ != target.x) {
					last.x = current->value.x;
					current->value.x = target.x;
					target.x = target_;
				//}
			}
			else if constexpr (COMPONENT_Y == component) {
				//if (target_ != target.y) {
					last.y = current->value.y;
					current->value.y = target.y;
					target.y = target_;
				//}
			}
			else if constexpr (COMPONENT_Z == component) {
				//if (target_ != target.z) {
					last.z = current->value.z;
					current->value.z = target.z;
					target.z = target_;
				//}
			}
			else if constexpr (COMPONENT_W == component) {
				//if (target_ != target.w) {
					last.w = current->value.w;
					current->value.w = target.w;
					target.w = target_;
				//}
			}
		}

		void reset(T const& all_) {
			last = target = current->value = all_;
		}
		void __vectorcall reset(FXMVECTOR all_) {

			if constexpr (std::is_same<T, XMFLOAT4A>::value) {
				XMStoreFloat4A(&last, all_);
				XMStoreFloat4A(&current->value, all_);
				XMStoreFloat4A(&target, all_);
			}
			else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
				XMStoreFloat3A(&last, all_);
				XMStoreFloat3A(&current->value, all_);
				XMStoreFloat3A(&target, all_);
			}
			else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
				XMStoreFloat2A(&last, all_);
				XMStoreFloat2A(&current->value, all_);
				XMStoreFloat2A(&target, all_);
			}

			// last becomes what is current
			// current always transitions to target
			// set new target
		}

		template<uint32_t component>
		void reset_component(float const all_) {
			
			if constexpr (COMPONENT_X == component) {
				last.x = target.x = current->value.x = all_;
			}
			else if constexpr (COMPONENT_Y == component) {
				last.y = target.y = current->value.y = all_;
			}
			else if constexpr (COMPONENT_Z == component) {
				last.z = target.z = current->value.z = all_;
			}
			else if constexpr (COMPONENT_W == component) {
				last.w = target.w = current->value.w = all_;
			}
		}

		range(interpolated<T>* const initial)
			: current{initial}, last{*initial}, target{*initial}
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
			_v4interpolators[source.index].set_component<component>(target_);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].set_component<component>(target_);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].set_component<component>(target_);
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
			_v4interpolators[source.index].reset_component<component>(all_);
		}
		else if constexpr (std::is_same<T, XMFLOAT3A>::value) {
			_v3interpolators[source.index].reset_component<component>(all_);
		}
		else if constexpr (std::is_same<T, XMFLOAT2A>::value) {
			_v2interpolators[source.index].reset_component<component>(all_);
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
	U const get(interpolated<T> const& source) { return(_finterpolators[source.index].target); } // index must always be within bounds of vector

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

				this->pop_back();

				if (this->end() != it) {
					it->current->index = index; // update the index of the moved instance
				}
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

		[[likely]] if (0 != _v4interpolators.size()) {

			tbb::parallel_for(uint32_t(0), uint32_t(_v4interpolators.size()), [this, t](uint32_t const i) {

				_v4interpolators[i].lerp(t);
			});
		}
		[[likely]] if (0 != _v3interpolators.size()) {

			tbb::parallel_for(uint32_t(0), uint32_t(_v3interpolators.size()), [this, t](uint32_t const i) {

				_v3interpolators[i].lerp(t);
			});
		}
		[[likely]] if (0 != _v2interpolators.size()) {

			tbb::parallel_for(uint32_t(0), uint32_t(_v2interpolators.size()), [this, t](uint32_t const i) {

				_v2interpolators[i].lerp(t);
			});
		}
		[[likely]] if (0 != _finterpolators.size()) {

			tbb::parallel_for(uint32_t(0), uint32_t(_finterpolators.size()), [this, t](uint32_t const i) {

				_finterpolators[i].lerp(t);
			});
		}
	}

public:
	interpolator() = default;
	~interpolator() = default;
}; // singleton global instance
__declspec(selectany) extern inline interpolator Interpolator{};

