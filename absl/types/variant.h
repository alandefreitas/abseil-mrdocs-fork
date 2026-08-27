// Copyright 2018 The Abseil Authors.
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
// variant.h
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of `absl::variant`
// as a polyfill for `std::variant` prior to C++17. Now that C++17 is required,
// `absl::variant` is an alias for `std::variant`.

#ifndef ABSL_TYPES_VARIANT_H_
#define ABSL_TYPES_VARIANT_H_

#include <stddef.h>

#include <variant>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// Alias for `std::bad_variant_access`.
using bad_variant_access ABSL_REFACTOR_INLINE
    = std::bad_variant_access;

/// Returns the alternative stored at index `I` of a variant (deprecated).
///
/// @param args Arguments forwarded to `std::get`.
/// @return The value returned by `std::get<I>`.
template <size_t I, typename... Args>
[[deprecated]] constexpr auto get(Args&&... args)
    -> decltype(std::get<I>(std::forward<Args>(args)...)) {
  return std::get<I>(std::forward<Args>(args)...);
}

/// Returns the alternative of type `T` stored in a variant (deprecated).
///
/// @param args Arguments forwarded to `std::get`.
/// @return The value returned by `std::get<T>`.
template <typename T, typename... Args>
[[deprecated]] constexpr decltype(std::get<T>(std::declval<Args>()...)) get(
    Args&&... args) {
  return std::get<T>(std::forward<Args>(args)...);
}

/// Returns a pointer to the alternative at index `I`, or null (deprecated).
///
/// @param args Arguments forwarded to `std::get_if`.
/// @return The pointer returned by `std::get_if<I>`.
template <size_t I, typename... Args>
[[deprecated]] constexpr decltype(std::get_if<I>(std::declval<Args>()...))
get_if(Args&&... args) {
  return std::get_if<I>(std::forward<Args>(args)...);
}

/// Returns a pointer to the alternative of type `T`, or null (deprecated).
///
/// @param args Arguments forwarded to `std::get_if`.
/// @return The pointer returned by `std::get_if<T>`.
template <typename T, typename... Args>
[[deprecated]] constexpr decltype(std::get_if<T>(std::declval<Args>()...))
get_if(Args&&... args) {
  return std::get_if<T>(std::forward<Args>(args)...);
}

/// Tests whether a variant currently holds the alternative `T` (deprecated).
///
/// @param args Arguments forwarded to `std::holds_alternative`.
/// @return The result of `std::holds_alternative<T>`.
template <typename T, typename... Args>
[[deprecated]] constexpr decltype(std::holds_alternative<T>(
    std::declval<Args>()...))
holds_alternative(Args&&... args) {
  return std::holds_alternative<T>(std::forward<Args>(args)...);
}

/// Alias for `std::monostate`, an empty variant alternative.
using monostate ABSL_REFACTOR_INLINE
    = std::monostate;

/// Alias for `std::variant`.
template <typename... Types>
using variant ABSL_REFACTOR_INLINE
    = std::variant<Types...>;

/// Alias for `std::variant_alternative`, the type of alternative `I`.
template <size_t I, typename T>
using variant_alternative ABSL_REFACTOR_INLINE
    = std::variant_alternative<I, T>;

/// Alias for `std::variant_alternative_t`, the type of alternative `I`.
template <size_t I, typename T>
using variant_alternative_t ABSL_REFACTOR_INLINE
    = std::variant_alternative_t<I, T>;

/// Alias for `std::variant_npos`, the index of a valueless variant.
inline constexpr size_t variant_npos ABSL_REFACTOR_INLINE
    = std::variant_npos;

/// Alias for `std::variant_size`, the number of alternatives.
template <typename T>
using variant_size ABSL_REFACTOR_INLINE
    = std::variant_size<T>;

/// Alias for `std::variant_size_v`, the number of alternatives.
template <typename T>
inline constexpr size_t variant_size_v ABSL_REFACTOR_INLINE
    = std::variant_size_v<T>;

/// Applies a visitor to the alternatives of one or more variants (deprecated).
///
/// @param args Arguments forwarded to `std::visit`.
/// @return The value returned by `std::visit`.
template <typename... Args>
[[deprecated]] constexpr decltype(std::visit(std::declval<Args>()...)) visit(
    Args&&... args) {
  return std::visit(std::forward<Args>(args)...);
}

namespace variant_internal {
// Helper visitor for converting a variant<Ts...>` into another type (mostly
// variant) that can be constructed from any type.
template <typename To>
struct ConversionVisitor {
  template <typename T>
  To operator()(T&& v) const {
    return To(std::forward<T>(v));
  }
};
}  // namespace variant_internal

/// Converts a variant to a variant of another set of types.
///
/// Each alternative of the target variant must be constructible from any type
/// held by the source variant.
///
/// Example:
///
///     std::variant<name1, name2, float> InternalReq(const Req&);
///
///     // name1 and name2 are convertible to name
///     std::variant<name, float> ExternalReq(const Req& req) {
///       return absl::ConvertVariantTo<std::variant<name, float>>(
///                InternalReq(req));
///     }
///
/// @param variant The source variant to convert.
/// @return A `To` constructed from the active alternative of `variant`.
template <typename To, typename Variant>
To ConvertVariantTo(Variant&& variant) {
  return std::visit(variant_internal::ConversionVisitor<To>{},
                    std::forward<Variant>(variant));
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_VARIANT_H_
