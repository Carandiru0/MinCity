#pragma once
#include <utility>
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include <Random/superrandom.hpp> // mincity does not use an initial deterministic seed, so it is generated every start of mincity. The generated secure random seed is reserved for saving & loading. Including superrandom.hpp doesn't require any predefines if there is no deterministic seed chosen. Safe to include everywhere as needed.


typedef struct money_t : no_copy {

private:
	uint64_t	hash;

public:
	int64_t		amount;

	money_t() // invalid money!
		: amount(0), hash(0)
	{}

	money_t(int64_t const amount_)
		: amount(amount_), hash(Hash(amount_))
	{}

	bool const valid() const {	// cheat protection. If the value of this money_t's amount changes in memory
								// then it's hash will not be equal to the hash it had when it was created.
								// requires implementor to call this function prior to using the money_t instance in any operation.
		return(hash && Hash(amount) == hash);
	}

	money_t(money_t const&) = delete; // money always "moves" and is never "copied".
	money_t(money_t&& rhs)
		: amount(std::move(rhs.amount)), hash(std::move(rhs.hash))
	{
		rhs.amount = 0;
		rhs.hash = 0;
	}
	money_t& operator=(money_t const&) = delete; // money always "moves" and is never "copied".
	money_t& operator=(money_t&& rhs)
	{
		amount = std::move(rhs.amount); rhs.amount = 0;
		hash = std::move(rhs.hash); rhs.hash = 0;

		return(*this);
	}
	money_t& operator=(int64_t const amount_) // creates money equal to a constant value (not conserving)
	{
		amount = amount_;
		hash = Hash(amount);

		return(*this);
	}

	money_t& operator+=(money_t&& rhs) // moves money to *this from other (conserving)
	{
		if (valid() && rhs.valid()) {
			amount += rhs.amount; rhs.amount = 0;
			hash = Hash(amount); rhs.hash = 0;
		}
		return(*this);
	}
	money_t& operator-=(money_t&& rhs) // moves money from *this to other (conserving)
	{
		if (valid() && rhs.valid()) {
			int64_t const amount_(amount);

			amount = SFM::max(0, amount - rhs.amount);	 // remove n money
			Hash(amount);

			rhs.amount += amount_ - amount;  // add n money
			rhs.hash = Hash(rhs.amount);
		}
		return(*this);
	}

	money_t& operator+=(int64_t const amount_) // creates money from a constant value (eg.) money += 64i64;) (not conserving)
	{
		if (valid()) {
			amount += amount_;
			hash = Hash(amount);
		}
		return(*this);
	}
	money_t& operator-=(int64_t const amount_) // burns money by a constant value (eg.) money -= 64i64;) (not conserving)
	{
		if (valid()) {
			amount -= amount_;
			hash = Hash(amount);
		}
		return(*this);
	}

	operator int64_t const() const // can be cast to int64_t representing the amount (not the hash, ever)
	{
		if (valid()) {
			return(amount);
		}

		return(0);
	}
} money_t;
