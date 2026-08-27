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

#ifndef ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_
#define ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/random/bernoulli_distribution.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/uniform_int_distribution.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// A distribution over random integers.
///
/// A discrete distribution produces random integers i, where 0 <= i < n
/// distributed according to the discrete probability function:
///
///     P(i|p0,...,pn-1)=pi
///
/// This class is an implementation of discrete_distribution (see
/// [rand.dist.samp.discrete]).
///
/// The algorithm used is Walker's Aliasing algorithm, described in Knuth, Vol 2.
/// absl::discrete_distribution takes O(N) time to precompute the probabilities
/// (where N is the number of possible outcomes in the distribution) at
/// construction, and then takes O(1) time for each variate generation.  Many
/// other implementations also take O(N) time to construct an ordered sequence of
/// partial sums, plus O(log N) time per variate to binary search.
///
template <typename IntType = int>
class discrete_distribution {
 public:
  /// The type of the values produced by the distribution.
  using result_type = IntType;

  /// The parameter set of the distribution.
  class param_type {
   public:
    /// The distribution type associated with this parameter set.
    using distribution_type = discrete_distribution;

    /// Constructs a parameter set with a single, certain outcome.
    param_type() { init(); }

    /// Constructs a parameter set from a range of weights.
    ///
    /// @param begin An iterator to the first weight.
    /// @param end An iterator past the last weight.
    template <typename InputIterator>
    explicit param_type(InputIterator begin, InputIterator end)
        : p_(begin, end) {
      init();
    }

    /// Constructs a parameter set from a list of weights.
    ///
    /// @param weights The weights of the possible outcomes.
    explicit param_type(std::initializer_list<double> weights) : p_(weights) {
      init();
    }

    /// Constructs a parameter set by sampling a weight function.
    ///
    /// @param nw The number of weights to generate.
    /// @param xmin The lower bound of the sampled interval.
    /// @param xmax The upper bound of the sampled interval.
    /// @param fw The unary weight function to sample.
    template <class UnaryOperation>
    explicit param_type(size_t nw, double xmin, double xmax,
                        UnaryOperation fw) {
      if (nw > 0) {
        p_.reserve(nw);
        double delta = (xmax - xmin) / static_cast<double>(nw);
        assert(delta > 0);
        double t = delta * 0.5;
        for (size_t i = 0; i < nw; ++i) {
          p_.push_back(fw(xmin + i * delta + t));
        }
      }
      init();
    }

    /// Returns the normalized probabilities of the outcomes.
    ///
    /// @return The probability of each possible outcome.
    const std::vector<double>& probabilities() const { return p_; }
    /// Returns the largest value the distribution can produce.
    ///
    /// @return The index of the last possible outcome.
    size_t n() const { return p_.size() - 1; }

    /// Compares two parameter sets for equality.
    ///
    /// @param a The first parameter set to compare.
    /// @param b The second parameter set to compare.
    /// @return `true` if the parameter sets are equal.
    friend bool operator==(const param_type& a, const param_type& b) {
      return a.probabilities() == b.probabilities();
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
    friend class discrete_distribution;

    void init();

    std::vector<double> p_;                     // normalized probabilities
    std::vector<std::pair<double, size_t>> q_;  // (acceptance, alternate) pairs

    static_assert(std::is_integral_v<result_type>,
                  "Class-template absl::discrete_distribution<> must be "
                  "parameterized using an integral type.");
  };

  /// Constructs a distribution with a single, certain outcome.
  discrete_distribution() : param_() {}

  /// Constructs a distribution from the given parameter set.
  ///
  /// @param p The parameter set.
  explicit discrete_distribution(const param_type& p) : param_(p) {}

  /// Constructs a distribution from a range of weights.
  ///
  /// @param begin An iterator to the first weight.
  /// @param end An iterator past the last weight.
  template <typename InputIterator>
  explicit discrete_distribution(InputIterator begin, InputIterator end)
      : param_(begin, end) {}

  /// Constructs a distribution from a list of weights.
  ///
  /// @param weights The weights of the possible outcomes.
  explicit discrete_distribution(std::initializer_list<double> weights)
      : param_(weights) {}

  /// Constructs a distribution by sampling a weight function.
  ///
  /// @param nw The number of weights to generate.
  /// @param xmin The lower bound of the sampled interval.
  /// @param xmax The upper bound of the sampled interval.
  /// @param fw The unary weight function to sample.
  template <class UnaryOperation>
  explicit discrete_distribution(size_t nw, double xmin, double xmax,
                                 UnaryOperation fw)
      : param_(nw, xmin, xmax, std::move(fw)) {}

  /// Resets the internal state of the distribution.
  ///
  /// This is a no-op for this distribution.
  void reset() {}

  /// Generates a random integer.
  ///
  /// @param g The uniform random bit generator.
  /// @return A random integer distributed according to the parameter set.
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  /// Generates a random integer using the given parameter set.
  ///
  /// @param g The uniform random bit generator.
  /// @param p The parameter set to use for this call.
  /// @return A random integer distributed according to `p`.
  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  /// Returns the parameter set of the distribution.
  ///
  /// @return The current parameter set.
  const param_type& param() const { return param_; }
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
  /// @return The index of the last possible outcome, inclusive.
  result_type(max)() const {
    return static_cast<result_type>(param_.n());
  }  // inclusive

  /// Returns the normalized probabilities of the outcomes.
  ///
  /// @return The probability of each possible outcome.
  // NOTE [rand.dist.sample.discrete] returns a std::vector<double> not a
  // const std::vector<double>&.
  const std::vector<double>& probabilities() const {
    return param_.probabilities();
  }

  /// Compares two distributions for equality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have equal parameter sets.
  friend bool operator==(const discrete_distribution& a,
                         const discrete_distribution& b) {
    return a.param_ == b.param_;
  }
  /// Compares two distributions for inequality.
  ///
  /// @param a The first distribution to compare.
  /// @param b The second distribution to compare.
  /// @return `true` if the distributions have differing parameter sets.
  friend bool operator!=(const discrete_distribution& a,
                         const discrete_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
};

// --------------------------------------------------------------------------
// Implementation details only below
// --------------------------------------------------------------------------

namespace random_internal {

// Using the vector `*probabilities`, whose values are the weights or
// probabilities of an element being selected, constructs the proportional
// probabilities used by the discrete distribution.  `*probabilities` will be
// scaled, if necessary, so that its entries sum to a value sufficiently close
// to 1.0.
std::vector<std::pair<double, size_t>> InitDiscreteDistribution(
    std::vector<double>* probabilities);

}  // namespace random_internal

template <typename IntType>
void discrete_distribution<IntType>::param_type::init() {
  if (p_.empty()) {
    p_.push_back(1.0);
    q_.emplace_back(1.0, 0);
  } else {
    assert(n() <= (std::numeric_limits<IntType>::max)());
    q_ = random_internal::InitDiscreteDistribution(&p_);
  }
}

template <typename IntType>
template <typename URBG>
typename discrete_distribution<IntType>::result_type
discrete_distribution<IntType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  const auto idx = absl::uniform_int_distribution<result_type>(0, p.n())(g);
  const auto& q = p.q_[idx];
  const bool selected = absl::bernoulli_distribution(q.first)(g);
  return selected ? idx : static_cast<result_type>(q.second);
}

/// Writes the distribution to an output stream.
///
/// @param os The output stream to write to.
/// @param x The distribution to write.
/// @return A reference to the output stream.
template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const discrete_distribution<IntType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  const auto& probabilities = x.param().probabilities();
  os << probabilities.size();

  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  for (const auto& p : probabilities) {
    os << os.fill() << p;
  }
  return os;
}

/// Reads the distribution from an input stream.
///
/// @param is The input stream to read from.
/// @param x The distribution to read into.
/// @return A reference to the input stream.
template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    discrete_distribution<IntType>& x) {    // NOLINT(runtime/references)
  using param_type = typename discrete_distribution<IntType>::param_type;
  auto saver = random_internal::make_istream_state_saver(is);

  size_t n;
  std::vector<double> p;

  is >> n;
  if (is.fail()) return is;
  if (n > 0) {
    p.reserve(n);
    for (IntType i = 0; i < n && !is.fail(); ++i) {
      auto tmp = random_internal::read_floating_point<double>(is);
      if (is.fail()) return is;
      p.push_back(tmp);
    }
  }
  x.param(param_type(p.begin(), p.end()));
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_
