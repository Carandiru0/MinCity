#pragma once
#include <tbb/tbb.h>

template<typename T>
struct references   // for thread local instances, grouped together so they can be accessed by any thread.
{
public:
	tbb::concurrent_vector<T*> const& reference() const { return(_reference); }

	void reference(T*&& reference)
	{
		_reference.emplace_back(std::forward<T*&&>(reference));
		reference->referenced(true);
	}

	void reserve(size_t const size) {

		_reference.reserve(size);
	}

	references() = default;

private:
	tbb::concurrent_vector<T*>	_reference;
};


