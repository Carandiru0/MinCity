#pragma once

#include <chrono>

#ifndef tTime
using tTime = std::chrono::time_point<std::chrono::steady_clock>;
constexpr tTime const _zero_time = tTime{ std::chrono::nanoseconds(0) };
#define zero_time_point (_zero_time)

using namespace std::chrono;

// A floating point seconds type
using fp_seconds = std::chrono::duration<double, std::chrono::seconds::period>;	      // internally double precision for long last application time accuracy. to get maximum benefit overloaded operators exposed by fp_seconds for calculations rather than using
constexpr duration const _zero_duration = duration{ std::chrono::nanoseconds(0) };	  // .count() and performing the calculations after. Only output to float with count after time calculations (differences, etc.) use conversion macro (time_to_float) below at this point:
#define zero_time_duration (_zero_duration)

static constexpr float const _to_float(fp_seconds const d) { return((float)(d.count())); }
static constexpr float const _to_float(double const d) { return((float)(d)); }
#define time_to_float _to_float

// use fp_seconds on a duration like:
// auto const fpDuration = duration_cast<fp_seconds>(interval)
// acceptable:
// float const tDelta( duration_cast<fp_seconds>(tNow - tLast).count() );
//  fp_seconds(milliseconds(24));

static_assert(std::chrono::treat_as_floating_point<fp_seconds::rep>::value, "Rep required to be double precision floating point");

static constexpr size_t const FRAME_UPDATE_INTERVAL_NS = 33333333ULL; // in nanoseconds (maximizes precision)
static constexpr nanoseconds const fixed_delta_duration = nanoseconds(FRAME_UPDATE_INTERVAL_NS);
static constexpr nanoseconds const fixed_delta_x2_duration = nanoseconds(FRAME_UPDATE_INTERVAL_NS<<1ULL);

static inline constexpr nanoseconds const delta() { return(fixed_delta_duration); }
#endif 



