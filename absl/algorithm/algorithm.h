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
// File: algorithm.h
// -----------------------------------------------------------------------------
//
// This header file contains Google extensions to the standard <algorithm> C++
// header.

#ifndef ABSL_ALGORITHM_ALGORITHM_H_
#define ABSL_ALGORITHM_ALGORITHM_H_

#include <algorithm>
#include <iterator>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/base/macros.h"

// Google extensions to the standard `<algorithm>` C++ header.
//
// Historical note: Abseil once provided implementations of `equal` and
// `rotate` prior to their adoption in C++14. New code should prefer the
// `std` variants. See the documentation for the STL `<algorithm>` header
// for more information: https://en.cppreference.com/w/cpp/header/algorithm
namespace absl {
ABSL_NAMESPACE_BEGIN

/// Determines whether two ranges are equal.
///
/// Compares the elements in the range `[first1, last1)` with the range
/// beginning at `first2`, using `operator==`.
///
/// @param first1 Iterator to the first element of the first range.
/// @param last1 Iterator one past the last element of the first range.
/// @param first2 Iterator to the first element of the second range.
/// @return `true` if the two ranges compare element-wise equal.
template <class InputIt1, class InputIt2>
ABSL_DEPRECATE_AND_INLINE()
constexpr bool equal(InputIt1 first1, InputIt1 last1, InputIt2 first2) {
  return std::equal(first1, last1, first2);
}

/// Determines whether two ranges are equal using a custom predicate.
///
/// Compares the elements in the range `[first1, last1)` with the range
/// beginning at `first2`, using the binary predicate `p`.
///
/// @param first1 Iterator to the first element of the first range.
/// @param last1 Iterator one past the last element of the first range.
/// @param first2 Iterator to the first element of the second range.
/// @param p Binary predicate used to compare corresponding elements.
/// @return `true` if every compared pair of elements satisfies `p`.
template <class InputIt1, class InputIt2, class BinaryPredicate>
ABSL_DEPRECATE_AND_INLINE()
constexpr bool equal(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                     BinaryPredicate p) {
  return std::equal(first1, last1, first2, p);
}

/// Determines whether two bounded ranges are equal.
///
/// Compares the elements in the range `[first1, last1)` with the range
/// `[first2, last2)`, using `operator==`.
///
/// @param first1 Iterator to the first element of the first range.
/// @param last1 Iterator one past the last element of the first range.
/// @param first2 Iterator to the first element of the second range.
/// @param last2 Iterator one past the last element of the second range.
/// @return `true` if the two ranges have equal length and compare
///   element-wise equal.
template <class InputIt1, class InputIt2>
ABSL_DEPRECATE_AND_INLINE()
constexpr bool equal(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                     InputIt2 last2) {
  return std::equal(first1, last1, first2, last2);
}

/// Determines whether two bounded ranges are equal using a custom predicate.
///
/// Compares the elements in the range `[first1, last1)` with the range
/// `[first2, last2)`, using the binary predicate `p`.
///
/// @param first1 Iterator to the first element of the first range.
/// @param last1 Iterator one past the last element of the first range.
/// @param first2 Iterator to the first element of the second range.
/// @param last2 Iterator one past the last element of the second range.
/// @param p Binary predicate used to compare corresponding elements.
/// @return `true` if the two ranges have equal length and every compared
///   pair of elements satisfies `p`.
template <class InputIt1, class InputIt2, class BinaryPredicate>
ABSL_DEPRECATE_AND_INLINE()
constexpr bool equal(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                     InputIt2 last2, BinaryPredicate p) {
  return std::equal(first1, last1, first2, last2, p);
}

/// Rotates the elements in a range so that `n_first` becomes the new first.
///
/// Performs a left rotation on the range `[first, last)` such that the
/// element pointed to by `n_first` becomes the first element of the range.
///
/// @param first Iterator to the first element of the range.
/// @param n_first Iterator to the element that should become the new first.
/// @param last Iterator one past the last element of the range.
/// @return An iterator to the new location of the element previously at
///   `first`.
template <class ForwardIt>
ABSL_DEPRECATE_AND_INLINE()
constexpr ForwardIt rotate(ForwardIt first, ForwardIt n_first, ForwardIt last) {
  return std::rotate(first, n_first, last);
}

/// Performs a linear search for a value in a range.
///
/// Searches the range `[first, last)` and returns `true` if it contains an
/// element equal to `value`.
///
/// A linear search is of O(n) complexity and makes at most
/// n = (`last` - `first`) comparisons. A linear search over short containers
/// may be faster than a binary search, even when the container is sorted.
///
/// @param first Iterator to the first element of the range to search.
/// @param last Iterator one past the last element of the range to search.
/// @param value The value to search for.
/// @return `true` if `[first, last)` contains an element equal to `value`.
template <typename InputIterator, typename EqualityComparable>
constexpr bool linear_search(InputIterator first, InputIterator last,
                             const EqualityComparable& value) {
  return std::find(first, last, value) != last;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_ALGORITHM_ALGORITHM_H_
