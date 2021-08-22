#pragma once

#include <chrono>

#ifndef tTime
using tTime = std::chrono::time_point<std::chrono::steady_clock>;
constexpr tTime const zero_time = tTime{ std::chrono::nanoseconds(0) };
#define zero_time_point (zero_time)

using namespace std::chrono;

// A floating point seconds type
using fp_seconds = std::chrono::duration<float, std::chrono::seconds::period>;
constexpr duration const zero_duration = duration{ std::chrono::nanoseconds(0) };
#define zero_time_duration (zero_duration)

// use fp_seconds on a duration like:
// auto const fpDuration = duration_cast<fp_seconds>(interval)
// acceptable:
// float const tDelta( duration_cast<fp_seconds>(tNow - tLast).count() );
//  fp_seconds(milliseconds(24));

static_assert(std::chrono::treat_as_floating_point<fp_seconds::rep>::value, "Rep required to be floating point");

static constexpr size_t const FRAME_UPDATE_INTERVAL_NS = 33333333ULL; // in nanoseconds (maximizes precision)
static constexpr nanoseconds const fixed_delta_duration = nanoseconds(FRAME_UPDATE_INTERVAL_NS);
static constexpr nanoseconds const fixed_delta_x2_duration = nanoseconds(FRAME_UPDATE_INTERVAL_NS<<1ULL);

static inline constexpr nanoseconds const delta() { return(fixed_delta_duration); }
#endif 



