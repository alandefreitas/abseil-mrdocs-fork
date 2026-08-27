// Copyright 2026 The Abseil Authors.
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
// File: optional_ref.h
// -----------------------------------------------------------------------------
//
// `optional_ref<T>` provides a `std::optional`-like interface around `T*`.
// It is similar to C++26's `std::optional<T&>`, but with slight enhancements,
// such as the fact that it permits construction from rvalues. That is, it
// relaxes the std::reference_constructs_from_temporary constraint. Its intent
// is to make it easier for functions to accept nullable object addresses,
// regardless of whether or not they point to temporaries.
//
// It can be constructed in the following ways:
//   * optional_ref<T> ref;
//   * optional_ref<T> ref = std::nullopt;
//   * T foo; optional_ref<T> ref = foo;
//   * std::optional<T> foo; optional_ref<T> ref = foo;
//   * T* foo = ...; optional_ref<T> ref = foo;
//   * optional_ref<T> foo; optional_ref<const T> ref = foo;
//
// Since it is trivially copyable and destructible, it should be passed by
// value.
//
// Other properties:
//   * Assignment is not allowed. Example:
//       optional_ref<int> ref;
//       // Compile error.
//       ref = 2;
//
//   * operator bool() is intentionally not defined, as it would be error prone
//     for optional_ref<bool>.
//
// Example usage, assuming some type `T` that is expensive to copy:
//   void ProcessT(optional_ref<const T> input) {
//     if (!input.has_value()) {
//       // Handle empty case.
//       return;
//     }
//     const T& val = *input;
//     // Do something with val.
//   }
//
//   ProcessT(std::nullopt);
//   ProcessT(BuildT());

#ifndef ABSL_TYPES_OPTIONAL_REF_H_
#define ABSL_TYPES_OPTIONAL_REF_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// A `std::optional`-like interface around a `T*`.
///
/// Similar to C++26's `std::optional<T&>`, but it also permits construction
/// from rvalues. It makes it easier for functions to accept nullable object
/// addresses, regardless of whether they point to temporaries. It is trivially
/// copyable and destructible, so it should be passed by value.
template <typename T>
class optional_ref {
  template <typename U>
  using EnableIfConvertibleFrom =
      std::enable_if_t<std::is_convertible_v<U*, T*>>;

 public:
  /// The referenced value type.
  using value_type = T;

  /// Constructs an empty `optional_ref`.
  constexpr optional_ref() : ptr_(nullptr) {}
  /// Constructs an empty `optional_ref` from `std::nullopt`.
  ///
  /// @param input The `std::nullopt` tag.
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      std::nullopt_t input)
      : ptr_(nullptr) {}

  /// Constructs an `optional_ref` referring to a concrete value.
  ///
  /// @param input The value to refer to.
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      T& input ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : ptr_(std::addressof(input)) {}

  /// Constructs an `optional_ref` from a const `std::optional`.
  ///
  /// @param input The optional whose value (if any) is referred to.
  template <typename U, typename = EnableIfConvertibleFrom<const U>>
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      const std::optional<U>& input ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : ptr_(input.has_value() ? std::addressof(*input) : nullptr) {}
  /// Constructs an `optional_ref` from a mutable `std::optional`.
  ///
  /// @param input The optional whose value (if any) is referred to.
  template <typename U, typename = EnableIfConvertibleFrom<U>>
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      std::optional<U>& input ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : ptr_(input.has_value() ? std::addressof(*input) : nullptr) {}

  /// Constructs an `optional_ref` from a pointer, where null means absent.
  ///
  /// @param input The pointer to refer to, or null for an empty reference.
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      T* input ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : ptr_(input) {}

  /// Deleted to forbid a naked `nullptr`; use `std::nullopt` instead.
  ///
  /// @param input The `nullptr` literal.
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      std::nullptr_t input) = delete;

  /// Copies an `optional_ref`.
  ///
  /// @param other The reference to copy.
  optional_ref(const optional_ref<T>& other) = default;
  /// Assignment is deleted; `optional_ref` is not assignable.
  ///
  /// @param other The reference that would be assigned from.
  /// @return This declaration is deleted and cannot be called.
  optional_ref<T>& operator=(const optional_ref<T>& other) = delete;

  /// Converts from an `optional_ref<U>` when `U*` converts to `T*`.
  ///
  /// @param input The source reference to convert from.
  template <typename U, typename = EnableIfConvertibleFrom<U>>
  constexpr optional_ref(  // NOLINT(google-explicit-constructor)
      optional_ref<U> input)
      : ptr_(input.as_pointer()) {}

  /// Determines whether the `optional_ref` contains a value.
  ///
  /// @return `false` if and only if `*this` is empty.
  constexpr bool has_value() const { return ptr_ != nullptr; }

  /// Returns a reference to the underlying value.
  ///
  /// Throws the same error as `std::optional::value()` when empty.
  ///
  /// @return A reference to the referenced value.
  constexpr T& value() const {
    return ABSL_PREDICT_TRUE(ptr_ != nullptr)
               ? *ptr_
               // Replicate the same error logic as in `std::optional`'s
               // `value()`. It either throws an exception or aborts the
               // program. We intentionally ignore the return value of
               // the constructed optional's value as we only need to run
               // the code for error checking.
               : ((void)std::optional<T>().value(), *ptr_);
  }

  /// Returns the value if present, otherwise `default_value`.
  ///
  /// @param default_value The value returned when `*this` is empty.
  /// @return The referenced value, or `default_value` if empty.
  template <typename U>
  constexpr T value_or(U&& default_value) const {
    // Instantiate std::optional<T>::value_or(U) to trigger its static_asserts.
    if (false) {
      // We use `std::add_const_t` here since just using `const` makes MSVC
      // complain about the syntax.
      (void)std::add_const_t<std::optional<T>>{}.value_or(
          std::forward<U>(default_value));
    }
    return ptr_ != nullptr ? *ptr_
                           : static_cast<T>(std::forward<U>(default_value));
  }

  /// Accesses the underlying value; undefined behavior if empty.
  ///
  /// @return A reference to the referenced value.
  constexpr T& operator*() const {
    absl::base_internal::HardeningAssertNonNull(ptr_);
    return *ptr_;
  }
  /// Accesses members of the underlying value; undefined behavior if empty.
  ///
  /// @return A pointer to the referenced value.
  constexpr T* operator->() const {
    absl::base_internal::HardeningAssertNonNull(ptr_);
    return ptr_;
  }

  /// Represents the `optional_ref` as a `T*` pointer.
  ///
  /// @return The underlying pointer, or null if empty.
  constexpr T* as_pointer() const { return ptr_; }
  /// Represents the `optional_ref` as a `std::optional`.
  ///
  /// Incurs a copy when the `optional_ref` is non-empty.
  ///
  /// @return A `std::optional<U>` holding a copy of the value, or `nullopt`.
  template <typename U = std::decay_t<T>>
  constexpr std::optional<U> as_optional() const {
    if (ptr_ == nullptr) return std::nullopt;
    return *ptr_;
  }

 private:
  T* const ptr_;

  // T constraint checks.  You can't have an optional of nullopt_t or
  // in_place_t.
  static_assert(!std::is_same_v<std::nullopt_t, std::remove_cv_t<T>>,
                "optional_ref<nullopt_t> is not allowed.");
  static_assert(!std::is_same_v<std::in_place_t, std::remove_cv_t<T>>,
                "optional_ref<in_place_t> is not allowed.");
};

// Template type deduction guides:

/// Deduces `optional_ref<const T>` from a const lvalue.
template <typename T>
optional_ref(const T&) -> optional_ref<const T>;
/// Deduces `optional_ref<T>` from a mutable lvalue.
template <typename T>
optional_ref(T&) -> optional_ref<T>;

/// Deduces `optional_ref<const T>` from a const `std::optional`.
template <typename T>
optional_ref(const std::optional<T>&) -> optional_ref<const T>;
/// Deduces `optional_ref<T>` from a mutable `std::optional`.
template <typename T>
optional_ref(std::optional<T>&) -> optional_ref<T>;

/// Deduces `optional_ref<T>` from a pointer.
template <typename T>
optional_ref(T*) -> optional_ref<T>;

namespace optional_ref_internal {

// This is a C++-11 compatible version of std::equality_comparable_with that
// only requires `t == u` is a valid boolean expression.
//
// We still need this for a couple reasons:
// -  As of 2026-02-13, Abseil supports C++17.
//  - Even for targets that are built with the default toolchain, using
//    std::equality_comparable_with gives us an error due to mutual recursion
//    between its definition and our definition of operator==.
//
template <typename T, typename U>
using enable_if_equality_comparable_t = std::enable_if_t<std::is_convertible_v<
    decltype(std::declval<T>() == std::declval<U>()), bool>>;

}  // namespace optional_ref_internal

// Compare an optional referenced value to std::nullopt.

/// Tests whether an `optional_ref` is empty.
///
/// @param a The reference to test.
/// @param b The `std::nullopt` tag.
/// @return `true` if `a` is empty.
template <typename T>
constexpr bool operator==(optional_ref<T> a, std::nullopt_t b) {
  return !a.has_value();
}
/// Tests whether an `optional_ref` is empty.
///
/// @param a The `std::nullopt` tag.
/// @param b The reference to test.
/// @return `true` if `b` is empty.
template <typename T>
constexpr bool operator==(std::nullopt_t a, optional_ref<T> b) {
  return !b.has_value();
}
/// Tests whether an `optional_ref` holds a value.
///
/// @param a The reference to test.
/// @param b The `std::nullopt` tag.
/// @return `true` if `a` holds a value.
template <typename T>
constexpr bool operator!=(optional_ref<T> a, std::nullopt_t b) {
  return a.has_value();
}
/// Tests whether an `optional_ref` holds a value.
///
/// @param a The `std::nullopt` tag.
/// @param b The reference to test.
/// @return `true` if `b` holds a value.
template <typename T>
constexpr bool operator!=(std::nullopt_t a, optional_ref<T> b) {
  return b.has_value();
}

// Compare two optional referenced values. Note, this does not test that the
// contained `ptr_`s are equal. If the caller wants "shallow" reference equality
// semantics, they should use `as_pointer()` explicitly.

/// Compares the referenced values of two `optional_ref`s.
///
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if both are empty or both hold equal values.
template <typename T, typename U>
constexpr bool operator==(optional_ref<T> a, optional_ref<U> b) {
  return a.has_value() ? *a == b : !b.has_value();
}

// Compare an optional referenced value to a non-optional value.

/// Compares a value to the referenced value of an `optional_ref`.
///
/// @param a The value to compare.
/// @param b The reference to compare against.
/// @return `true` if `b` holds a value equal to `a`.
template <
    typename T, typename U,
    typename = optional_ref_internal::enable_if_equality_comparable_t<T, U>>
constexpr bool operator==(const T& a, optional_ref<U> b) {
  return b.has_value() && a == *b;
}
/// Compares the referenced value of an `optional_ref` to a value.
///
/// @param a The reference to compare.
/// @param b The value to compare against.
/// @return `true` if `a` holds a value equal to `b`.
template <
    typename T, typename U,
    typename = optional_ref_internal::enable_if_equality_comparable_t<T, U>>
constexpr bool operator==(optional_ref<T> a, const U& b) {
  return b == a;
}

// Inequality operators, as above.

/// Tests whether two `optional_ref`s differ.
///
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the operands are not equal.
template <typename T, typename U>
constexpr bool operator!=(optional_ref<T> a, optional_ref<U> b) {
  return !(a == b);
}
/// Tests whether an `optional_ref` differs from a value.
///
/// @param a The reference to compare.
/// @param b The value to compare against.
/// @return `true` if `a` does not hold a value equal to `b`.
template <
    typename T, typename U,
    typename = optional_ref_internal::enable_if_equality_comparable_t<T, U>>
constexpr bool operator!=(optional_ref<T> a, const U& b) {
  return !(a == b);
}
/// Tests whether a value differs from an `optional_ref`.
///
/// @param a The value to compare.
/// @param b The reference to compare against.
/// @return `true` if `b` does not hold a value equal to `a`.
template <
    typename T, typename U,
    typename = optional_ref_internal::enable_if_equality_comparable_t<T, U>>
constexpr bool operator!=(const T& a, optional_ref<U> b) {
  return !(a == b);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_OPTIONAL_REF_H_
