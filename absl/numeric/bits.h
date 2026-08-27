// Copyright 2020 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: bits.h
// -----------------------------------------------------------------------------
//
// This file contains implementations of C++20's bitwise math functions, as
// defined by:
//
// P0553R4:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0553r4.html
// P0556R3:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0556r3.html
// P1355R2:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1355r2.html
// P1956R1:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1956r1.pdf
// P0463R1
//  https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0463r1.html
// P1272R4
//  https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p1272r4.html
//
// When using a standard library that implements these functions, we use the
// standard library's implementation.

#ifndef ABSL_NUMERIC_BITS_H_
#define ABSL_NUMERIC_BITS_H_

#include <cstdint>
#include <limits>
#include <type_traits>
#include <version>

#include "absl/base/config.h"

#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
#include <bit>
#endif

#include "absl/base/attributes.h"
#include "absl/base/internal/endian.h"
#include "absl/numeric/internal/bits.h"

// The primary namespace for the Abseil library.
//
// Groups the common utilities and types that make up Abseil, including the
// bitwise math functions and the 128-bit integer types declared in this
// module.
namespace absl {
ABSL_NAMESPACE_BEGIN

// https://github.com/llvm/llvm-project/issues/64544
// libc++ had the wrong signature for std::rotl and std::rotr
// prior to libc++ 18.0.
//
#if (defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L) &&     \
    (!defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 180000)
/// Rotates the bits of `x` to the left by `s` positions.
using std::rotl;
/// Rotates the bits of `x` to the right by `s` positions.
using std::rotr;

#else

/// Rotates the bits of `x` to the left by `s` positions.
///
/// @param x The unsigned value whose bits are rotated.
/// @param s The number of bit positions to rotate to the left.
/// @return The value of `x` with its bits rotated left by `s` positions.
template <class T>
[[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<T>, T> rotl(
    T x, int s) noexcept {
  return numeric_internal::RotateLeft(x, s);
}

/// Rotates the bits of `x` to the right by `s` positions.
///
/// @param x The unsigned value whose bits are rotated.
/// @param s The number of bit positions to rotate to the right.
/// @return The value of `x` with its bits rotated right by `s` positions.
template <class T>
[[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<T>, T> rotr(
    T x, int s) noexcept {
  return numeric_internal::RotateRight(x, s);
}

#endif

// https://github.com/llvm/llvm-project/issues/64544
// libc++ had the wrong signature for std::rotl and std::rotr
// prior to libc++ 18.0.
//
#if (defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L)

/// Counts the number of consecutive one bits starting from the most significant bit of `x`.
using std::countl_one;
/// Counts the number of consecutive zero bits starting from the most significant bit of `x`.
using std::countl_zero;
/// Counts the number of consecutive one bits starting from the least significant bit of `x`.
using std::countr_one;
/// Counts the number of consecutive zero bits starting from the least significant bit of `x`.
using std::countr_zero;
/// Counts the number of one bits in the value of `x`.
using std::popcount;

#else

/// Counts the number of consecutive zero bits starting from the most significant bit of `x`.
///
/// While this function is typically `constexpr`, on some platforms it may not
/// be marked as `constexpr` due to constraints of the compiler or available
/// intrinsics.
///
/// @param x The unsigned value whose leading zero bits are counted.
/// @return The number of consecutive zero bits, starting from the most significant bit.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline std::enable_if_t<std::is_unsigned_v<T>, int>
countl_zero(T x) noexcept {
  return numeric_internal::CountLeadingZeroes(x);
}

/// Counts the number of consecutive one bits starting from the most significant bit of `x`.
///
/// @param x The unsigned value whose leading one bits are counted.
/// @return The number of consecutive one bits, starting from the most significant bit.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline std::enable_if_t<std::is_unsigned_v<T>, int>
countl_one(T x) noexcept {
  // Avoid integer promotion to a wider type
  return countl_zero(static_cast<T>(~x));
}

/// Counts the number of consecutive zero bits starting from the least significant bit of `x`.
///
/// @param x The unsigned value whose trailing zero bits are counted.
/// @return The number of consecutive zero bits, starting from the least significant bit.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CTZ inline std::enable_if_t<std::is_unsigned_v<T>, int>
countr_zero(T x) noexcept {
  return numeric_internal::CountTrailingZeroes(x);
}

/// Counts the number of consecutive one bits starting from the least significant bit of `x`.
///
/// @param x The unsigned value whose trailing one bits are counted.
/// @return The number of consecutive one bits, starting from the least significant bit.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CTZ inline std::enable_if_t<std::is_unsigned_v<T>, int>
countr_one(T x) noexcept {
  // Avoid integer promotion to a wider type
  return countr_zero(static_cast<T>(~x));
}

/// Counts the number of one bits in the value of `x`.
///
/// @param x The unsigned value whose set bits are counted.
/// @return The number of one bits in `x`.
template <class T>
ABSL_INTERNAL_CONSTEXPR_POPCOUNT inline std::enable_if_t<std::is_unsigned_v<T>,
                                                         int>
popcount(T x) noexcept {
  return numeric_internal::Popcount(x);
}

#endif

#if (defined(__cpp_lib_int_pow2) && __cpp_lib_int_pow2 >= 202002L)

/// Returns the smallest power of two not less than `x`.
using std::bit_ceil;
/// Returns the largest power of two not greater than `x`.
using std::bit_floor;
/// Returns the number of bits needed to represent the value of `x`.
using std::bit_width;
/// Determines whether `x` is an integral power of two.
using std::has_single_bit;

#else

/// Determines whether `x` is an integral power of two.
///
/// @param x The unsigned value to test.
/// @return `true` if `x` is an integral power of two; `false` otherwise.
template <class T>
constexpr inline std::enable_if_t<std::is_unsigned_v<T>, bool> has_single_bit(
    T x) noexcept {
  return x != 0 && (x & (x - 1)) == 0;
}

/// Returns the number of bits needed to represent the value of `x`.
///
/// @param x The unsigned value to measure.
/// @return If `x == 0`, `0`; otherwise one plus the base-2 logarithm of `x`, with any fractional part discarded.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline std::enable_if_t<std::is_unsigned_v<T>, int>
bit_width(T x) noexcept {
  return std::numeric_limits<T>::digits - countl_zero(x);
}

/// Returns the largest power of two not greater than `x`.
///
/// @param x The unsigned value to round down.
/// @return If `x == 0`, `0`; otherwise the maximal value `y` such that `has_single_bit(y)` is true and `y <= x`.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline std::enable_if_t<std::is_unsigned_v<T>, T>
bit_floor(T x) noexcept {
  return x == 0 ? 0 : T{1} << (bit_width(x) - 1);
}

/// Returns the smallest power of two not less than `x`.
///
/// The result must be representable as a value of type `T`.
///
/// @param x The unsigned value to round up.
/// @return `N`, where `N` is the smallest power of two greater than or equal to `x`.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline std::enable_if_t<std::is_unsigned_v<T>, T>
bit_ceil(T x) {
  // If T is narrower than unsigned, T{1} << bit_width will be promoted.  We
  // want to force it to wraparound so that bit_ceil of an invalid value are not
  // core constant expressions.
  //
  // BitCeilNonPowerOf2 triggers an overflow in constexpr contexts if we would
  // undergo promotion to unsigned but not fit the result into T without
  // truncation.
  return has_single_bit(x) ? T{1} << (bit_width(x) - 1)
                           : numeric_internal::BitCeilNonPowerOf2(x);
}

#endif

#if defined(__cpp_lib_endian) && __cpp_lib_endian >= 201907L

/// Indicates the endianness of all scalar types.
///
/// If all scalar types are little-endian, `absl::endian::native` equals
/// `absl::endian::little`. If all scalar types are big-endian,
/// `absl::endian::native` equals `absl::endian::big`. Platforms that use
/// anything else are unsupported.
///
/// See https://en.cppreference.com/w/cpp/types/endian.
using std::endian;

#else

/// Indicates the endianness of all scalar types.
///
/// If all scalar types are little-endian, `native` equals `little`. If all
/// scalar types are big-endian, `native` equals `big`. Platforms that use
/// anything else are unsupported.
enum class endian {
  little,
  big,
#if defined(ABSL_IS_LITTLE_ENDIAN)
  native = little
#elif defined(ABSL_IS_BIG_ENDIAN)
  native = big
#else
#error "Endian detection needs to be set up for this platform"
#endif
};

#endif  // defined(__cpp_lib_endian) && __cpp_lib_endian >= 201907L

#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L

/// Reverses the bytes in the given integer value `x`.
///
/// `absl::byteswap` participates in overload resolution only if `T` satisfies
/// integral, i.e., `T` is an integer type. The program is ill-formed if `T`
/// has padding bits.
///
/// See https://en.cppreference.com/w/cpp/numeric/byteswap.
using std::byteswap;

#else

/// Reverses the bytes in the given integer value `x`.
///
/// @param x The integer value whose bytes are reversed.
/// @return The value of `x` with the order of its bytes reversed.
template <class T>
[[nodiscard]] constexpr T byteswap(T x) noexcept {
  static_assert(std::is_integral_v<T>,
                "byteswap requires an integral argument");
  static_assert(
      sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
      "byteswap works only with 8, 16, 32, or 64-bit integers");
  if constexpr (sizeof(T) == 1) {
    return x;
  } else if constexpr (sizeof(T) == 2) {
    return static_cast<T>(gbswap_16(static_cast<uint16_t>(x)));
  } else if constexpr (sizeof(T) == 4) {
    return static_cast<T>(gbswap_32(static_cast<uint32_t>(x)));
  } else if constexpr (sizeof(T) == 8) {
    return static_cast<T>(gbswap_64(static_cast<uint64_t>(x)));
  }
}

#endif  // defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_NUMERIC_BITS_H_
