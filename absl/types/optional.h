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
// optional.h
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of `absl::optional`
// as a polyfill for `std::optional` prior to C++17. Now that C++17 is required,
// `absl::optional` is an alias for `std::optional`.

#ifndef ABSL_TYPES_OPTIONAL_H_
#define ABSL_TYPES_OPTIONAL_H_

#include <initializer_list>
#include <optional>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// Alias for `std::bad_optional_access`.
using bad_optional_access ABSL_REFACTOR_INLINE
    = std::bad_optional_access;

/// Creates an `optional` holding a copy of the given value.
///
/// @param value The value to store in the returned optional.
/// @return An optional holding the forwarded value.
template <typename T>
ABSL_REFACTOR_INLINE constexpr decltype(std::make_optional(
    std::declval<T>())) make_optional(T&& value) {
  return std::make_optional(std::forward<T>(value));
}

/// Creates an `optional<T>` by constructing `T` in place.
///
/// @param args Arguments forwarded to the constructor of `T`.
/// @return An optional holding the constructed value.
template <typename T, typename... Args>
constexpr decltype(std::make_optional<T>(
    std::declval<Args>()...)) make_optional(Args&&... args) {
  return std::make_optional<T>(std::forward<Args>(args)...);
}

/// Creates an `optional<T>` from an initializer list and extra arguments.
///
/// @param il Initializer list forwarded to the constructor of `T`.
/// @param args Additional arguments forwarded to the constructor of `T`.
/// @return An optional holding the constructed value.
template <typename T, typename U, typename... Args>
constexpr decltype(std::make_optional<T>(
    std::declval<std::initializer_list<U>>(),
    std::declval<Args>()...)) make_optional(std::initializer_list<U> il,
                                            Args&&... args) {
  return std::make_optional<T>(il, std::forward<Args>(args)...);
}

/// Alias for `std::nullopt`, the disengaged-optional constant.
using std::nullopt ABSL_REFACTOR_INLINE;

/// Alias for `std::nullopt_t`, the type of `nullopt`.
using nullopt_t ABSL_REFACTOR_INLINE
    = std::nullopt_t;

/// Alias for `std::optional`.
using std::optional ABSL_REFACTOR_INLINE;

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_OPTIONAL_H_
