// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: uniform_real_distribution.h
// -----------------------------------------------------------------------------
//
// This header defines a class for representing a uniform floating-point
// distribution over a half-open interval [a,b). You use this distribution in
// combination with an Abseil random bit generator to produce random values
// according to the rules of the distribution.
//
// `absl::uniform_real_distribution` is a drop-in replacement for the C++11
// `std::uniform_real_distribution` [rand.dist.uni.real] but is considerably
// faster than the libstdc++ implementation.
//
// Note: the standard-library version may occasionally return `1.0` when
// default-initialized. See https://bugs.llvm.org//show_bug.cgi?id=18767
// `absl::uniform_real_distribution` does not exhibit this behavior.

#ifndef ABSL_RANDOM_UNIFORM_REAL_DISTRIBUTION_H_
#define ABSL_RANDOM_UNIFORM_REAL_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// This distribution produces random floating-point values uniformly
/// distributed over the half-open interval [a, b).
///
/// Example:
///
///   absl::BitGen gen;
///
///   // Use the distribution to produce a value between 0.0 (inclusive)
///   // and 1.0 (exclusive).
///   double value = absl::uniform_real_distribution<double>(0, 1)(gen);
///
template <typename RealType = double>
class uniform_real_distribution {
 public:
  /// The type of the values produced by the distribution.
  using result_type = RealType;

  /// The parameter set of the distribution.
  class param_type {
   public:
    /// The distribution type associated with this parameter set.
    using distribution_type = uniform_real_distribution;

    /// Constructs the parameter set from the interval bounds.
    ///
    /// @param lo The lower bound of the half-open interval.
    /// @param hi The upper bound of the half-open interval.
    explicit param_type(result_type lo = 0, result_type hi = 1)
        : lo_(lo), hi_(hi), range_(hi - lo) {
      // [rand.dist.uni.real] preconditions 2 & 3
      assert(lo <= hi);

      // NOTE: For integral types, we can promote the range to an unsigned type,
      // which gives full width of the range. However for real (fp) types, this
      // is not possible, so value generation cannot use the full range of the
      // real type.
      assert(range_ <= (std::numeric_limits<result_type>::max)());
    }

    /// Returns the lower bound of the interval.
    ///
    /// @return The lower bound `a`.
    result_type a() const { return lo_; }
    /// Returns the upper bound of the interval.
    ///
    /// @return The upper bound `b`.
    result_type b() const { return hi_; }

    /// Compares two parameter sets for equality.
    ///
    /// @param a The first parameter set to compare.
    /// @param b The second parameter set to compare.
    /// @return `true` if the parameter sets are equal.
    friend bool operator==(const param_type& a, const param_type& b) {
      return a.lo_ == b.lo_ && a.hi_ == b.hi_;
    }

    /// Compares two parameter sets for inequality.
    ///
    /// @param a The first parameter set to compare.
    /// @param b The second parameter set to compare.
    /// @return `true` if the parameter sets are not equal.
    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class uniform_real_distribution;
    result_type lo_, hi_, range_;

    static_assert(std::is_floating_point_v<RealType>,
                  "Class-template absl::uniform_real_distribution<> must be "
                  "parameterized using a floating-point type.");
  };

  /// Constructs a distribution over the half-open interval [0, 1).
  uniform_real_distribution() : uniform_real_distribution(0) {}

  /// Constructs a distribution over the half-open interval [lo, hi).
  ///
  /// @param lo The lower bound of the half-open interval.
  /// @param hi The upper bound of the half-open interval.
  explicit uniform_real_distribution(result_type lo, result_type hi = 1)
      : param_(lo, hi) {}

  /// Constructs a distribution from the given parameter set.
  ///
  /// @param param The parameter set.
  explicit uniform_real_distribution(const param_type& param) : param_(param) {}

  /// Resets the uniform real distribution.
  ///
  /// Note that this function has no effect because the distribution already
  /// produces independent values.
  void reset() {}

  /// Generates a random value in the interval [a, b).
  ///
  /// @param gen The uniform random bit generator.
  /// @return A random value in the half-open interval [a, b).
  template <typename URBG>
  result_type operator()(URBG& gen) {  // NOLINT(runtime/references)
    return operator()(gen, param_);
  }

  /// Generates a random value using the given parameter set.
  ///
  /// @param gen The uniform random bit generator.
  /// @param p The parameter set to use for this call.
  /// @return A random value in the half-open interval [a, b).
  template <typename URBG>
  result_type operator()(URBG& gen,  // NOLINT(runtime/references)
                         const param_type& p);

  /// Returns the lower bound of the interval.
  ///
  /// @return The lower bound `a`.
  result_type a() const { return param_.a(); }
  /// Returns the upper bound of the interval.
  ///
  /// @return The upper bound `b`.
  result_type b() const { return param_.b(); }

  /// Returns the parameter set of the distribution.
  ///
  /// @return The current parameter set.
  param_type param() const { return param_; }
  /// Sets the parameter set of the distribution.
  ///
  /// @param params The new parameter set.
  void param(const param_type& params) { param_ = params; }

  /// Returns the smallest value the distribution can produce.
  ///
  /// @return The lower bound `a`.
  result_type(min)() const { return a(); }
  /// Returns the largest value the distribution can produce.
  ///
  /// @return The upper bound `b`.
  result_type(max)() const { return b(); }

  /// Compares two distributions for equality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have equal parameter sets.
  friend bool operator==(const uniform_real_distribution& a,
                         const uniform_real_distribution& b) {
    return a.param_ == b.param_;
  }
  /// Compares two distributions for inequality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have differing parameter sets.
  friend bool operator!=(const uniform_real_distribution& a,
                         const uniform_real_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

// -----------------------------------------------------------------------------
// Implementation details follow
// -----------------------------------------------------------------------------
template <typename RealType>
template <typename URBG>
typename uniform_real_distribution<RealType>::result_type
uniform_real_distribution<RealType>::operator()(
    URBG& gen, const param_type& p) {  // NOLINT(runtime/references)
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  using real_type =
      std::conditional_t<std::is_same_v<RealType, float>, float, double>;

  while (true) {
    const result_type sample =
        GenerateRealFromBits<real_type, GeneratePositiveTag, true>(
            fast_u64_(gen));
    const result_type res = p.a() + (sample * p.range_);
    if (res < p.b() || p.range_ <= 0 || !std::isfinite(p.range_)) {
      return res;
    }
    // else sample rejected, try again.
  }
}

/// Writes the distribution to an output stream.
///
/// @param os The output stream to write to.
/// @param x The distribution to write.
/// @return A reference to the output stream.
template <typename CharT, typename Traits, typename RealType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const uniform_real_distribution<RealType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<RealType>::kPrecision);
  os << x.a() << os.fill() << x.b();
  return os;
}

/// Reads the distribution from an input stream.
///
/// @param is The input stream to read from.
/// @param x The distribution to read into.
/// @return A reference to the input stream.
template <typename CharT, typename Traits, typename RealType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,     // NOLINT(runtime/references)
    uniform_real_distribution<RealType>& x) {  // NOLINT(runtime/references)
  using param_type = typename uniform_real_distribution<RealType>::param_type;
  using result_type = typename uniform_real_distribution<RealType>::result_type;
  auto saver = random_internal::make_istream_state_saver(is);
  auto a = random_internal::read_floating_point<result_type>(is);
  if (is.fail()) return is;
  auto b = random_internal::read_floating_point<result_type>(is);
  if (!is.fail()) {
    x.param(param_type(a, b));
  }
  return is;
}
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_UNIFORM_REAL_DISTRIBUTION_H_
