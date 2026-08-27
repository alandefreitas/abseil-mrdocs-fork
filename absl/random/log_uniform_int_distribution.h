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

#ifndef ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_
#define ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/base/config.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"
#include "absl/random/uniform_int_distribution.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// Returns a random variate R in range [min, max] such that
/// floor(log(R-min, base)) is uniformly distributed.
///
/// We ensure uniformity by discretization using the
/// boundary sets [0, 1, base, base * base, ... min(base*n, max)].
///
template <typename IntType = int>
class log_uniform_int_distribution {
 private:
  using unsigned_type =
      typename random_internal::make_unsigned_bits<IntType>::type;

 public:
  /// The type of the values produced by the distribution.
  using result_type = IntType;

  /// The parameter set of the distribution.
  class param_type {
   public:
    /// The distribution type associated with this parameter set.
    using distribution_type = log_uniform_int_distribution;

    /// Constructs the parameter set from the bounds and logarithm base.
    ///
    /// @param min The lower bound of the closed interval.
    /// @param max The upper bound of the closed interval.
    /// @param base The base of the logarithm.
    explicit param_type(
        result_type min = 0,
        result_type max = (std::numeric_limits<result_type>::max)(),
        result_type base = 2)
        : min_(min),
          max_(max),
          base_(base),
          range_(static_cast<unsigned_type>(max_) -
                 static_cast<unsigned_type>(min_)),
          log_range_(0) {
      assert(max_ >= min_);
      assert(base_ > 1);

      if (base_ == 2) {
        // Determine where the first set bit is on range(), giving a log2(range)
        // value which can be used to construct bounds.
        log_range_ = (std::min)(random_internal::BitWidth(range()),
                                std::numeric_limits<unsigned_type>::digits);
      } else {
        // NOTE: Computing the logN(x) introduces error from 2 sources:
        // 1. Conversion of int to double loses precision for values >=
        // 2^53, which may cause some log() computations to operate on
        // different values.
        // 2. The error introduced by the division will cause the result
        // to differ from the expected value.
        //
        // Thus a result which should equal K may equal K +/- epsilon,
        // which can eliminate some values depending on where the bounds fall.
        const double inv_log_base = 1.0 / std::log(static_cast<double>(base_));
        const double log_range = std::log(static_cast<double>(range()) + 0.5);
        log_range_ = static_cast<int>(std::ceil(inv_log_base * log_range));
      }
    }

    /// Returns the lower bound of the interval.
    ///
    /// @return The lower bound.
    result_type(min)() const { return min_; }
    /// Returns the upper bound of the interval.
    ///
    /// @return The upper bound.
    result_type(max)() const { return max_; }
    /// Returns the base of the logarithm.
    ///
    /// @return The logarithm base.
    result_type base() const { return base_; }

    /// Compares two parameter sets for equality.
    ///
    /// @param a The first parameter set to compare.
    /// @param b The second parameter set to compare.
    /// @return `true` if the parameter sets are equal.
    friend bool operator==(const param_type& a, const param_type& b) {
      return a.min_ == b.min_ && a.max_ == b.max_ && a.base_ == b.base_;
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
    friend class log_uniform_int_distribution;

    int log_range() const { return log_range_; }
    unsigned_type range() const { return range_; }

    result_type min_;
    result_type max_;
    result_type base_;
    unsigned_type range_;  // max - min
    int log_range_;        // ceil(logN(range_))

    static_assert(random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::log_uniform_int_distribution<> must be "
                  "parameterized using an integral type.");
  };

  /// Constructs a distribution over the entire range of `result_type`.
  log_uniform_int_distribution() : log_uniform_int_distribution(0) {}

  /// Constructs a distribution over the closed interval [min, max].
  ///
  /// @param min The lower bound of the closed interval.
  /// @param max The upper bound of the closed interval.
  /// @param base The base of the logarithm.
  explicit log_uniform_int_distribution(
      result_type min,
      result_type max = (std::numeric_limits<result_type>::max)(),
      result_type base = 2)
      : param_(min, max, base) {}

  /// Constructs a distribution from the given parameter set.
  ///
  /// @param p The parameter set.
  explicit log_uniform_int_distribution(const param_type& p) : param_(p) {}

  /// Resets the internal state of the distribution.
  ///
  /// This is a no-op for this distribution.
  void reset() {}

  /// Generates a random value in the interval [min, max].
  ///
  /// @param g The uniform random bit generator.
  /// @return A random value in the closed interval [min, max].
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  /// Generates a random value using the given parameter set.
  ///
  /// @param g The uniform random bit generator.
  /// @param p The parameter set to use for this call.
  /// @return A random value in the closed interval [min, max].
  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p) {
    return static_cast<result_type>((p.min)() + Generate(g, p));
  }

  /// Returns the smallest value the distribution can produce.
  ///
  /// @return The lower bound.
  result_type(min)() const { return (param_.min)(); }
  /// Returns the largest value the distribution can produce.
  ///
  /// @return The upper bound.
  result_type(max)() const { return (param_.max)(); }
  /// Returns the base of the logarithm.
  ///
  /// @return The logarithm base.
  result_type base() const { return param_.base(); }

  /// Returns the parameter set of the distribution.
  ///
  /// @return The current parameter set.
  param_type param() const { return param_; }
  /// Sets the parameter set of the distribution.
  ///
  /// @param p The new parameter set.
  void param(const param_type& p) { param_ = p; }

  /// Compares two distributions for equality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have equal parameter sets.
  friend bool operator==(const log_uniform_int_distribution& a,
                         const log_uniform_int_distribution& b) {
    return a.param_ == b.param_;
  }
  /// Compares two distributions for inequality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have differing parameter sets.
  friend bool operator!=(const log_uniform_int_distribution& a,
                         const log_uniform_int_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  // Returns a log-uniform variate in the range [0, p.range()]. The caller
  // should add min() to shift the result to the correct range.
  template <typename URNG>
  unsigned_type Generate(URNG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param_;
};

template <typename IntType>
template <typename URBG>
typename log_uniform_int_distribution<IntType>::unsigned_type
log_uniform_int_distribution<IntType>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  // sample e over [0, log_range]. Map the results of e to this:
  // 0 => 0
  // 1 => [1, b-1]
  // 2 => [b, (b^2)-1]
  // n => [b^(n-1)..(b^n)-1]
  const int e = absl::uniform_int_distribution<int>(0, p.log_range())(g);
  if (e == 0) {
    return 0;
  }
  const int d = e - 1;

  unsigned_type base_e, top_e;
  if (p.base() == 2) {
    base_e = static_cast<unsigned_type>(1) << d;

    top_e = (e >= std::numeric_limits<unsigned_type>::digits)
                ? (std::numeric_limits<unsigned_type>::max)()
                : (static_cast<unsigned_type>(1) << e) - 1;
  } else {
    const double r = std::pow(static_cast<double>(p.base()), d);
    const double s = (r * static_cast<double>(p.base())) - 1.0;

    base_e =
        (r > static_cast<double>((std::numeric_limits<unsigned_type>::max)()))
            ? (std::numeric_limits<unsigned_type>::max)()
            : static_cast<unsigned_type>(r);

    top_e =
        (s > static_cast<double>((std::numeric_limits<unsigned_type>::max)()))
            ? (std::numeric_limits<unsigned_type>::max)()
            : static_cast<unsigned_type>(s);
  }

  const unsigned_type lo = (base_e >= p.range()) ? p.range() : base_e;
  const unsigned_type hi = (top_e >= p.range()) ? p.range() : top_e;

  // choose uniformly over [lo, hi]
  return absl::uniform_int_distribution<result_type>(
      static_cast<result_type>(lo), static_cast<result_type>(hi))(g);
}

/// Writes the distribution to an output stream.
///
/// @param os The output stream to write to.
/// @param x The distribution to write.
/// @return A reference to the output stream.
template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const log_uniform_int_distribution<IntType>& x) {
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;
  auto saver = random_internal::make_ostream_state_saver(os);
  os << static_cast<stream_type>((x.min)()) << os.fill()
     << static_cast<stream_type>((x.max)()) << os.fill()
     << static_cast<stream_type>(x.base());
  return os;
}

/// Reads the distribution from an input stream.
///
/// @param is The input stream to read from.
/// @param x The distribution to read into.
/// @return A reference to the input stream.
template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,       // NOLINT(runtime/references)
    log_uniform_int_distribution<IntType>& x) {  // NOLINT(runtime/references)
  using param_type = typename log_uniform_int_distribution<IntType>::param_type;
  using result_type =
      typename log_uniform_int_distribution<IntType>::result_type;
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;

  stream_type min;
  stream_type max;
  stream_type base;

  auto saver = random_internal::make_istream_state_saver(is);
  is >> min >> max >> base;
  if (!is.fail()) {
    x.param(param_type(static_cast<result_type>(min),
                       static_cast<result_type>(max),
                       static_cast<result_type>(base)));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_
