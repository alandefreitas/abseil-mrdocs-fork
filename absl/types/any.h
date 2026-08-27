//
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
// any.h
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of `absl::any` as a
// polyfill for `std::any` prior to C++17. Now that C++17 is required,
// `absl::any` is an alias for `std::any`.

#ifndef ABSL_TYPES_ANY_H_
#define ABSL_TYPES_ANY_H_

#include <any>  // IWYU pragma: export
#include <initializer_list>

#include "absl/base/config.h"

// Include-what-you-use cleanup required for these headers.
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// Alias for `std::any` (deprecated; use `std::any` directly).
using any ABSL_DEPRECATE_AND_INLINE() = std::any;

/// Casts the value stored in an `any` object to type `T` (deprecated).
///
/// @param args Arguments forwarded to `std::any_cast`.
/// @return The result of `std::any_cast<T>`.
template <typename T, typename... Args>
[[deprecated]] constexpr decltype(std::any_cast<T>(std::declval<Args>()...))
any_cast(Args&&... args) {
  return std::any_cast<T>(std::forward<Args>(args)...);
}

/// Alias for `std::bad_any_cast` (deprecated; use `std::bad_any_cast`).
using bad_any_cast ABSL_DEPRECATE_AND_INLINE() = std::bad_any_cast;

/// Constructs an `any` object holding a value of type `T` (deprecated).
///
/// @param args Arguments forwarded to `std::make_any`.
/// @return An `any` holding the constructed value.
template <typename T, typename... Args>
[[deprecated]] constexpr decltype(std::make_any<T>(std::declval<Args>()...))
make_any(Args&&... args) {
  return std::make_any<T>(std::forward<Args>(args)...);
}

/// Constructs an `any` object holding a `T` from an initializer list
/// (deprecated).
///
/// @param il Initializer list forwarded to `std::make_any`.
/// @param args Additional arguments forwarded to `std::make_any`.
/// @return An `any` holding the constructed value.
template <typename T, typename U, typename... Args>
[[deprecated]] constexpr decltype(std::make_any<T>(
    std::declval<std::initializer_list<U>>(), std::declval<Args>()...))
make_any(std::initializer_list<U> il, Args&&... args) {
  return std::make_any<T>(il, std::forward<Args>(args)...);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_ANY_H_
