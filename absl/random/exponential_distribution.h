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

#ifndef ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_
#define ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_

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

/// Generates a number conforming to an exponential distribution.
///
/// This is equivalent to the standard [rand.dist.pois.exp] distribution.
template <typename RealType = double>
class exponential_distribution {
 public:
  /// The type of the values produced by the distribution.
  using result_type = RealType;

  /// The parameter set of the distribution.
  class param_type {
   public:
    /// The distribution type associated with this parameter set.
    using distribution_type = exponential_distribution;

    /// Constructs the parameter set from the rate parameter.
    ///
    /// @param lambda The rate parameter.
    explicit param_type(result_type lambda = 1) : lambda_(lambda) {
      assert(lambda > 0);
      neg_inv_lambda_ = -result_type(1) / lambda_;
    }

    /// Returns the rate parameter.
    ///
    /// @return The rate parameter.
    result_type lambda() const { return lambda_; }

    /// Compares two parameter sets for equality.
    ///
    /// @param a The first parameter set to compare.
    /// @param b The second parameter set to compare.
    /// @return `true` if the parameter sets are equal.
    friend bool operator==(const param_type& a, const param_type& b) {
      return a.lambda_ == b.lambda_;
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
    friend class exponential_distribution;

    result_type lambda_;
    result_type neg_inv_lambda_;

    static_assert(
        std::is_floating_point_v<RealType>,
        "Class-template absl::exponential_distribution<> must be parameterized "
        "using a floating-point type.");
  };

  /// Constructs a distribution with rate parameter 1.
  exponential_distribution() : exponential_distribution(1) {}

  /// Constructs a distribution with the given rate parameter.
  ///
  /// @param lambda The rate parameter.
  explicit exponential_distribution(result_type lambda) : param_(lambda) {}

  /// Constructs a distribution from the given parameter set.
  ///
  /// @param p The parameter set.
  explicit exponential_distribution(const param_type& p) : param_(p) {}

  /// Resets the internal state of the distribution.
  ///
  /// This is a no-op for this distribution.
  void reset() {}

  /// Generates a random value.
  ///
  /// @param g The uniform random bit generator.
  /// @return A random value drawn from the distribution.
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  /// Generates a random value using the given parameter set.
  ///
  /// @param g The uniform random bit generator.
  /// @param p The parameter set to use for this call.
  /// @return A random value drawn from the distribution.
  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  /// Returns the parameter set of the distribution.
  ///
  /// @return The current parameter set.
  param_type param() const { return param_; }
  /// Sets the parameter set of the distribution.
  ///
  /// @param p The new parameter set.
  void param(const param_type& p) { param_ = p; }

  /// Returns the smallest value the distribution can produce.
  ///
  /// @return `0`.
  result_type(min)() const { return 0; }
  /// Returns the largest value the distribution can produce.
  ///
  /// @return Positive infinity.
  result_type(max)() const {
    return std::numeric_limits<result_type>::infinity();
  }

  /// Returns the rate parameter.
  ///
  /// @return The rate parameter.
  result_type lambda() const { return param_.lambda(); }

  /// Compares two distributions for equality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have equal parameter sets.
  friend bool operator==(const exponential_distribution& a,
                         const exponential_distribution& b) {
    return a.param_ == b.param_;
  }
  /// Compares two distributions for inequality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have differing parameter sets.
  friend bool operator!=(const exponential_distribution& a,
                         const exponential_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

// --------------------------------------------------------------------------
// Implementation details follow
// --------------------------------------------------------------------------

template <typename RealType>
template <typename URBG>
typename exponential_distribution<RealType>::result_type
exponential_distribution<RealType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GenerateNegativeTag;
  using random_internal::GenerateRealFromBits;
  using real_type =
      std::conditional_t<std::is_same_v<RealType, float>, float, double>;

  const result_type u = GenerateRealFromBits<real_type, GenerateNegativeTag,
                                             false>(fast_u64_(g));  // U(-1, 0)

  // log1p(-x) is mathematically equivalent to log(1 - x) but has more
  // accuracy for x near zero.
  return p.neg_inv_lambda_ * std::log1p(u);
}

/// Writes the distribution to an output stream.
///
/// @param os The output stream to write to.
/// @param x The distribution to write.
/// @return A reference to the output stream.
template <typename CharT, typename Traits, typename RealType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const exponential_distribution<RealType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<RealType>::kPrecision);
  os << x.lambda();
  return os;
}

/// Reads the distribution from an input stream.
///
/// @param is The input stream to read from.
/// @param x The distribution to read into.
/// @return A reference to the input stream.
template <typename CharT, typename Traits, typename RealType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,    // NOLINT(runtime/references)
    exponential_distribution<RealType>& x) {  // NOLINT(runtime/references)
  using result_type = typename exponential_distribution<RealType>::result_type;
  using param_type = typename exponential_distribution<RealType>::param_type;
  result_type lambda;

  auto saver = random_internal::make_istream_state_saver(is);
  lambda = random_internal::read_floating_point<result_type>(is);
  if (!is.fail()) {
    x.param(param_type(lambda));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_
