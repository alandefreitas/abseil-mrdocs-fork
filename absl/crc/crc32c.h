// Copyright 2022 The Abseil Authors
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
// File: crc32c.h
// -----------------------------------------------------------------------------
//
// This header file defines the API for computing CRC32C values as checksums
// for arbitrary sequences of bytes provided as a string buffer.
//
// The API includes the basic functions for computing such CRC32C values and
// some utility functions for performing more efficient mathematical
// computations using an existing checksum.
#ifndef ABSL_CRC_CRC32C_H_
#define ABSL_CRC_CRC32C_H_

#include <cstddef>
#include <cstdint>
#include <ostream>

#include "absl/crc/internal/crc32c_inline.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

// Abseil's root namespace.
//
// Contains the CRC32C checksum types and functions.
namespace absl {
ABSL_NAMESPACE_BEGIN

//-----------------------------------------------------------------------------
// crc32c_t
//-----------------------------------------------------------------------------

/// A strongly-typed integer for holding a CRC32C value.
///
/// Some operators are intentionally omitted. Only equality operators are defined
/// so that `crc32c_t` can be directly compared. Methods for putting `crc32c_t`
/// directly into a set are omitted because this is bug-prone due to checksum
/// collisions. Use an explicit conversion to the `uint32_t` space for operations
/// that treat `crc32c_t` as an integer.
class crc32c_t final {
 public:
  /// Construct a zero-initialized CRC32C value.
  crc32c_t() = default;

  /// Construct a CRC32C value from a raw 32-bit checksum.
  ///
  /// @param crc The raw checksum value.
  constexpr explicit crc32c_t(uint32_t crc) : crc_(crc) {}

  /// Copy-construct a CRC32C value.
  /// @param other The source object.
  crc32c_t(const crc32c_t& other) = default;

  /// Copy-assign a CRC32C value.
  ///
  /// @param other The source object.
  /// @return A reference to this object.
  crc32c_t& operator=(const crc32c_t& other) = default;

  /// Convert the CRC32C value to its raw 32-bit checksum.
  ///
  /// @return The underlying checksum value.
  explicit operator uint32_t() const { return crc_; }

  /// Compare two CRC32C values for equality.
  ///
  /// @param lhs The left-hand operand.
  /// @param rhs The right-hand operand.
  /// @return `true` if both values are equal.
  friend bool operator==(crc32c_t lhs, crc32c_t rhs) {
    return static_cast<uint32_t>(lhs) == static_cast<uint32_t>(rhs);
  }

  /// Compare two CRC32C values for inequality.
  ///
  /// @param lhs The left-hand operand.
  /// @param rhs The right-hand operand.
  /// @return `true` if the values differ.
  friend bool operator!=(crc32c_t lhs, crc32c_t rhs) { return !(lhs == rhs); }

  /// Format a CRC32C value as an eight-digit hexadecimal string.
  ///
  /// @param sink The output sink to write to.
  /// @param crc The value to format.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, crc32c_t crc) {
    absl::Format(&sink, "%08x", static_cast<uint32_t>(crc));
  }

 private:
  uint32_t crc_;
};


namespace crc_internal {
// Non-inline code path for `absl::ExtendCrc32c()`. Do not call directly.
// Call `absl::ExtendCrc32c()` (defined below) instead.
crc32c_t ExtendCrc32cInternal(crc32c_t initial_crc,
                              absl::string_view buf_to_add);
}  // namespace crc_internal

// -----------------------------------------------------------------------------
// CRC32C Computation Functions
// -----------------------------------------------------------------------------

/// Compute a CRC32C value from an existing value and an additional buffer.
///
/// Computes a CRC32C value from an `initial_crc` CRC32C value including the
/// `buf_to_add` bytes of an additional buffer. Using this function is more
/// efficient than computing a CRC32C value for the combined buffer from
/// scratch.
///
/// Note: `ExtendCrc32c` with an initial_crc of 0 is equivalent to
/// `ComputeCrc32c`.
///
/// This operation has a runtime cost of O(`buf_to_add.size()`)
///
/// @param initial_crc The CRC32C value of the bytes already processed.
/// @param buf_to_add The additional bytes to include in the checksum.
/// @return The CRC32C value of the combined buffer.
inline crc32c_t ExtendCrc32c(crc32c_t initial_crc,
                             absl::string_view buf_to_add) {
  // Approximately 75% of calls have size <= 64.
  if (buf_to_add.size() <= 64) {
    uint32_t crc = static_cast<uint32_t>(initial_crc);
    if (crc_internal::ExtendCrc32cInline(&crc, buf_to_add.data(),
                                         buf_to_add.size())) {
      return crc32c_t{crc};
    }
  }
  return crc_internal::ExtendCrc32cInternal(initial_crc, buf_to_add);
}

/// Compute the CRC32C value of the provided string.
///
/// @param buf The bytes to compute the checksum over.
/// @return The CRC32C value of `buf`.
inline crc32c_t ComputeCrc32c(absl::string_view buf) {
  return ExtendCrc32c(crc32c_t{0}, buf);
}

/// Extend a CRC32C value as if trailing zero bytes were appended.
///
/// Computes a CRC32C value for a buffer with an `initial_crc` CRC32C value,
/// where `length` bytes with a value of 0 are appended to the buffer. Using this
/// function is more efficient than computing a CRC32C value for the combined
/// buffer from scratch.
///
/// This operation has a runtime cost of O(log(`length`))
///
/// @param initial_crc The CRC32C value of the existing buffer.
/// @param length The number of zero bytes appended.
/// @return The CRC32C value of the extended buffer.
crc32c_t ExtendCrc32cByZeroes(crc32c_t initial_crc, size_t length);

/// Copy bytes while computing the CRC32C value of the copied data.
///
/// Copies `src` to `dest` using `memcpy()` semantics, returning the CRC32C
/// value of the copied buffer.
///
/// Using `MemcpyCrc32c()` is potentially faster than performing the `memcpy()`
/// and `ComputeCrc32c()` operations separately.
///
/// @param dest The destination buffer.
/// @param src The source buffer.
/// @param count The number of bytes to copy.
/// @param initial_crc The CRC32C value to extend.
/// @return The CRC32C value of the copied bytes.
crc32c_t MemcpyCrc32c(void* dest, const void* src, size_t count,
                      crc32c_t initial_crc = crc32c_t{0});

// -----------------------------------------------------------------------------
// CRC32C Arithmetic Functions
// -----------------------------------------------------------------------------

// The following functions perform arithmetic on CRC32C values, which are
// generally more efficient than recalculating any given result's CRC32C value.

/// Compute the CRC32C value of two concatenated buffers.
///
/// Calculates the CRC32C value of two buffers with known CRC32C values
/// concatenated together.
///
/// Given a buffer with CRC32C value `crc1` and a buffer with
/// CRC32C value `crc2` and length, `crc2_length`, returns the CRC32C value of
/// the concatenation of these two buffers.
///
/// This operation has a runtime cost of O(log(`crc2_length`)).
///
/// @param crc1 The CRC32C value of the first buffer.
/// @param crc2 The CRC32C value of the second buffer.
/// @param crc2_length The length in bytes of the second buffer.
/// @return The CRC32C value of the concatenated buffers.
crc32c_t ConcatCrc32c(crc32c_t crc1, crc32c_t crc2, size_t crc2_length);

/// Compute the CRC32C value of a buffer with a prefix removed.
///
/// Calculates the CRC32C value of an existing buffer with a series of bytes
/// (the prefix) removed from the beginning of that buffer.
///
/// Given the CRC32C value of an existing buffer, `full_string_crc`; The CRC32C
/// value of a prefix of that buffer, `prefix_crc`; and the length of the buffer
/// with the prefix removed, `remaining_string_length` , return the CRC32C
/// value of the buffer with the prefix removed.
///
/// This operation has a runtime cost of O(log(`remaining_string_length`)).
///
/// @param prefix_crc The CRC32C value of the prefix being removed.
/// @param full_string_crc The CRC32C value of the complete buffer.
/// @param remaining_string_length The length of the buffer after removal.
/// @return The CRC32C value of the buffer with the prefix removed.
crc32c_t RemoveCrc32cPrefix(crc32c_t prefix_crc, crc32c_t full_string_crc,
                            size_t remaining_string_length);
/// Compute the CRC32C value of a buffer with a suffix removed.
///
/// Calculates the CRC32C value of an existing buffer with a series of bytes
/// (the suffix) removed from the end of that buffer.
///
/// Given a CRC32C value of an existing buffer `full_string_crc`, the CRC32C
/// value of the suffix to remove `suffix_crc`, and the length of that suffix
/// `suffix_len`, returns the CRC32C value of the buffer with suffix removed.
///
/// This operation has a runtime cost of O(log(`suffix_len`))
///
/// @param full_string_crc The CRC32C value of the complete buffer.
/// @param suffix_crc The CRC32C value of the suffix being removed.
/// @param suffix_length The length of the suffix being removed.
/// @return The CRC32C value of the buffer with the suffix removed.
crc32c_t RemoveCrc32cSuffix(crc32c_t full_string_crc, crc32c_t suffix_crc,
                            size_t suffix_length);

/// Stream a CRC32C value as an eight-digit hexadecimal string.
///
/// @param os The output stream to write to.
/// @param crc The value to stream.
/// @return A reference to `os`.
inline std::ostream& operator<<(std::ostream& os, crc32c_t crc) {
  return os << absl::StreamFormat("%08x", static_cast<uint32_t>(crc));
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_CRC32C_H_
