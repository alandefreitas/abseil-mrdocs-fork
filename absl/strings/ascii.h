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
// File: ascii.h
// -----------------------------------------------------------------------------
//
// This package contains functions operating on characters and strings
// restricted to standard ASCII. These include character classification
// functions analogous to those found in the ANSI C Standard Library <ctype.h>
// header file.
//
// C++ implementations provide <ctype.h> functionality based on their
// C environment locale. In general, reliance on such a locale is not ideal, as
// the locale standard is problematic (and may not return invariant information
// for the same character set, for example). These `ascii_*()` functions are
// hard-wired for standard ASCII, much faster, and guaranteed to behave
// consistently.  They will never be overloaded, nor will their function
// signature change.
//
// `ascii_isalnum()`, `ascii_isalpha()`, `ascii_isascii()`, `ascii_isblank()`,
// `ascii_iscntrl()`, `ascii_isdigit()`, `ascii_isgraph()`, `ascii_islower()`,
// `ascii_isprint()`, `ascii_ispunct()`, `ascii_isspace()`, `ascii_isupper()`,
// `ascii_isxdigit()`
//   Analogous to the <ctype.h> functions with similar names, these
//   functions take an unsigned char and return a bool, based on whether the
//   character matches the condition specified.
//
//   If the input character has a numerical value greater than 127, these
//   functions return `false`.
//
// `ascii_tolower()`, `ascii_toupper()`
//   Analogous to the <ctype.h> functions with similar names, these functions
//   take an unsigned char and return a char.
//
//   If the input character is not an ASCII {lower,upper}-case letter (including
//   numerical values greater than 127) then the functions return the same value
//   as the input character.

#ifndef ABSL_STRINGS_ASCII_H_
#define ABSL_STRINGS_ASCII_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace ascii_internal {

// Declaration for an array of bitfields holding character information.
ABSL_DLL extern const unsigned char kPropertyBits[256];

// Declaration for the array of characters to upper-case characters.
ABSL_DLL extern const char kToUpper[256];

// Declaration for the array of characters to lower-case characters.
ABSL_DLL extern const char kToLower[256];

void AsciiStrToLower(char* absl_nonnull dst, const char* absl_nullable src,
                     size_t n);

void AsciiStrToUpper(char* absl_nonnull dst, const char* absl_nullable src,
                     size_t n);

}  // namespace ascii_internal

/// Determines whether the given character is an alphabetic character.
///
/// @param c The character to test.
/// @return `true` if `c` is alphabetic, `false` otherwise.
inline bool ascii_isalpha(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x01) != 0;
}

/// Determines whether the given character is an alphanumeric character.
///
/// @param c The character to test.
/// @return `true` if `c` is alphanumeric, `false` otherwise.
inline bool ascii_isalnum(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x04) != 0;
}

/// Determines whether the given character is a whitespace character (space,
/// tab, vertical tab, formfeed, linefeed, or carriage return).
///
/// @param c The character to test.
/// @return `true` if `c` is whitespace, `false` otherwise.
inline bool ascii_isspace(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x08) != 0;
}

/// Determines whether the given character is a punctuation character.
///
/// @param c The character to test.
/// @return `true` if `c` is a punctuation character, `false` otherwise.
inline bool ascii_ispunct(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x10) != 0;
}

/// Determines whether the given character is a blank character (tab or space).
///
/// @param c The character to test.
/// @return `true` if `c` is a blank character, `false` otherwise.
inline bool ascii_isblank(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x20) != 0;
}

/// Determines whether the given character is a control character.
///
/// @param c The character to test.
/// @return `true` if `c` is a control character, `false` otherwise.
inline bool ascii_iscntrl(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x40) != 0;
}

/// Determines whether the given character can be represented as a hexadecimal
/// digit character (i.e. {0-9} or {A-F} or {a-f}).
///
/// @param c The character to test.
/// @return `true` if `c` is a hexadecimal digit, `false` otherwise.
inline bool ascii_isxdigit(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x80) != 0;
}

/// Determines whether the given character can be represented as a decimal
/// digit character (i.e. {0-9}).
///
/// @param c The character to test.
/// @return `true` if `c` is a decimal digit, `false` otherwise.
inline constexpr bool ascii_isdigit(unsigned char c) {
  return c >= '0' && c <= '9';
}

/// Determines whether the given character is printable, including spaces.
///
/// @param c The character to test.
/// @return `true` if `c` is printable, `false` otherwise.
inline constexpr bool ascii_isprint(unsigned char c) {
  return c >= 32 && c < 127;
}

/// Determines whether the given character has a graphical representation.
///
/// @param c The character to test.
/// @return `true` if `c` has a graphical representation, `false` otherwise.
inline constexpr bool ascii_isgraph(unsigned char c) {
  return c > 32 && c < 127;
}

/// Determines whether the given character is uppercase.
///
/// @param c The character to test.
/// @return `true` if `c` is uppercase, `false` otherwise.
inline constexpr bool ascii_isupper(unsigned char c) {
  return c >= 'A' && c <= 'Z';
}

/// Determines whether the given character is lowercase.
///
/// @param c The character to test.
/// @return `true` if `c` is lowercase, `false` otherwise.
inline constexpr bool ascii_islower(unsigned char c) {
  return c >= 'a' && c <= 'z';
}

/// Determines whether the given character is ASCII.
///
/// @param c The character to test.
/// @return `true` if `c` is an ASCII character, `false` otherwise.
inline constexpr bool ascii_isascii(unsigned char c) { return c < 128; }

/// Returns an ASCII character, converting to lowercase if uppercase is
/// passed. Note that character values > 127 are simply returned.
///
/// @param c The character to convert.
/// @return The lowercase equivalent of `c`, or `c` if it has no lowercase form.
inline char ascii_tolower(unsigned char c) {
  return ascii_internal::kToLower[c];
}

/// Converts the characters in `s` to lowercase, changing the contents of `s`.
///
/// @param s The string to convert in place.
void AsciiStrToLower(std::string* absl_nonnull s);

/// Creates a lowercase string from a given absl::string_view.
///
/// @param s The source characters to convert.
/// @return A new lowercase string.
[[nodiscard]] inline std::string AsciiStrToLower(absl::string_view s) {
  std::string result;
  StringResizeAndOverwrite(result, s.size(), [s](char* buf, size_t buf_size) {
    ascii_internal::AsciiStrToLower(buf, s.data(), s.size());
    return buf_size;
  });
  return result;
}

/// Creates a lowercase string from a given std::string&&.
///
/// (Template is used to lower priority of this overload.)
///
/// @param s The source string to convert.
/// @return A new lowercase string.
template <int&... DoNotSpecify>
[[nodiscard]] inline std::string AsciiStrToLower(std::string&& s) {
  std::string result = std::move(s);
  absl::AsciiStrToLower(&result);
  return result;
}

/// Returns the ASCII character, converting to upper-case if lower-case is
/// passed. Note that characters values > 127 are simply returned.
///
/// @param c The character to convert.
/// @return The uppercase equivalent of `c`, or `c` if it has no uppercase form.
inline char ascii_toupper(unsigned char c) {
  return ascii_internal::kToUpper[c];
}

/// Converts the characters in `s` to uppercase, changing the contents of `s`.
///
/// @param s The string to convert in place.
void AsciiStrToUpper(std::string* absl_nonnull s);

/// Creates an uppercase string from a given absl::string_view.
///
/// @param s The source characters to convert.
/// @return A new uppercase string.
[[nodiscard]] inline std::string AsciiStrToUpper(absl::string_view s) {
  std::string result;
  StringResizeAndOverwrite(result, s.size(), [s](char* buf, size_t buf_size) {
    ascii_internal::AsciiStrToUpper(buf, s.data(), s.size());
    return buf_size;
  });
  return result;
}

/// Creates an uppercase string from a given std::string&&.
///
/// (Template is used to lower priority of this overload.)
///
/// @param s The source string to convert.
/// @return A new uppercase string.
template <int&... DoNotSpecify>
[[nodiscard]] inline std::string AsciiStrToUpper(std::string&& s) {
  std::string result = std::move(s);
  absl::AsciiStrToUpper(&result);
  return result;
}

/// Returns absl::string_view with whitespace stripped from the beginning of the
/// given string_view.
///
/// @param str The characters to strip leading whitespace from.
/// @return A view of `str` without leading whitespace.
[[nodiscard]] inline absl::string_view StripLeadingAsciiWhitespace(
    absl::string_view str ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  auto it = std::find_if_not(str.begin(), str.end(), absl::ascii_isspace);
  return str.substr(static_cast<size_t>(it - str.begin()));
}

/// Strips in place whitespace from the beginning of the given string.
///
/// @param str The string to strip leading whitespace from in place.
inline void StripLeadingAsciiWhitespace(std::string* absl_nonnull str) {
  auto it = std::find_if_not(str->begin(), str->end(), absl::ascii_isspace);
  str->erase(str->begin(), it);
}

/// Returns absl::string_view with whitespace stripped from the end of the given
/// string_view.
///
/// @param str The characters to strip trailing whitespace from.
/// @return A view of `str` without trailing whitespace.
[[nodiscard]] inline absl::string_view StripTrailingAsciiWhitespace(
    absl::string_view str ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  auto it = std::find_if_not(str.rbegin(), str.rend(), absl::ascii_isspace);
  return str.substr(0, static_cast<size_t>(str.rend() - it));
}

/// Strips in place whitespace from the end of the given string
///
/// @param str The string to strip trailing whitespace from in place.
inline void StripTrailingAsciiWhitespace(std::string* absl_nonnull str) {
  auto it = std::find_if_not(str->rbegin(), str->rend(), absl::ascii_isspace);
  str->erase(static_cast<size_t>(str->rend() - it));
}

/// Returns absl::string_view with whitespace stripped from both ends of the
/// given string_view.
///
/// @param str The characters to strip whitespace from.
/// @return A view of `str` without leading or trailing whitespace.
[[nodiscard]] inline absl::string_view StripAsciiWhitespace(
    absl::string_view str ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return StripTrailingAsciiWhitespace(StripLeadingAsciiWhitespace(str));
}

/// Strips in place whitespace from both ends of the given string
///
/// @param str The string to strip whitespace from in place.
inline void StripAsciiWhitespace(std::string* absl_nonnull str) {
  StripTrailingAsciiWhitespace(str);
  StripLeadingAsciiWhitespace(str);
}

/// Removes leading, trailing, and consecutive internal whitespace.
///
/// @param str The string to normalize whitespace in place.
void RemoveExtraAsciiWhitespace(std::string* absl_nonnull str);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_ASCII_H_
