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
// File: match.h
// -----------------------------------------------------------------------------
//
// This file contains simple utilities for performing string matching checks.
// All of these function parameters are specified as `absl::string_view`,
// meaning that these functions can accept `std::string`, `absl::string_view` or
// NUL-terminated C-style strings.
//
// Examples:
//   std::string s = "foo";
//   absl::string_view sv = "f";
//   assert(absl::StrContains(s, sv));
//
// Note: The order of parameters in these functions is designed to mimic the
// order an equivalent member function would exhibit;
// e.g. `s.Contains(x)` ==> `absl::StrContains(s, x).
#ifndef ABSL_STRINGS_MATCH_H_
#define ABSL_STRINGS_MATCH_H_

#include <cstring>

#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// Returns whether a given string `haystack` contains the substring `needle`.
///
/// @param haystack The string to search within.
/// @param needle The substring to search for.
/// @return `true` if `needle` is found in `haystack`, `false` otherwise.
inline bool StrContains(absl::string_view haystack,
                        absl::string_view needle) noexcept {
  return haystack.find(needle, 0) != haystack.npos;
}

/// Returns whether a given string `haystack` contains the character `needle`.
///
/// @param haystack The string to search within.
/// @param needle The character to search for.
/// @return `true` if `needle` is found in `haystack`, `false` otherwise.
inline bool StrContains(absl::string_view haystack, char needle) noexcept {
  return haystack.find(needle) != haystack.npos;
}

/// Returns whether a given string `text` begins with `prefix`.
///
/// @param text The string to test.
/// @param prefix The prefix to look for.
/// @return `true` if `text` begins with `prefix`, `false` otherwise.
inline constexpr bool StartsWith(absl::string_view text,
                                 absl::string_view prefix) noexcept {
  if (prefix.empty()) {
    return true;
  }
  if (text.size() < prefix.size()) {
    return false;
  }
  absl::string_view possible_match = text.substr(0, prefix.size());

  return possible_match == prefix;
}

/// Returns whether a given string `text` ends with `suffix`.
///
/// @param text The string to test.
/// @param suffix The suffix to look for.
/// @return `true` if `text` ends with `suffix`, `false` otherwise.
inline constexpr bool EndsWith(absl::string_view text,
                               absl::string_view suffix) noexcept {
  if (suffix.empty()) {
    return true;
  }
  if (text.size() < suffix.size()) {
    return false;
  }
  absl::string_view possible_match = text.substr(text.size() - suffix.size());
  return possible_match == suffix;
}
/// Returns whether a given ASCII string `haystack` contains the ASCII substring
/// `needle`, ignoring case in the comparison.
///
/// @param haystack The string to search within.
/// @param needle The substring to search for.
/// @return `true` if `needle` is found in `haystack`, `false` otherwise.
bool StrContainsIgnoreCase(absl::string_view haystack,
                           absl::string_view needle) noexcept;

/// Returns whether a given ASCII string `haystack` contains the ASCII character
/// `needle`, ignoring case in the comparison.
///
/// @param haystack The string to search within.
/// @param needle The character to search for.
/// @return `true` if `needle` is found in `haystack`, `false` otherwise.
bool StrContainsIgnoreCase(absl::string_view haystack,
                           char needle) noexcept;

/// Returns whether given ASCII strings `piece1` and `piece2` are equal, ignoring
/// case in the comparison.
///
/// @param piece1 The first string to compare.
/// @param piece2 The second string to compare.
/// @return `true` if the strings are equal ignoring case, `false` otherwise.
bool EqualsIgnoreCase(absl::string_view piece1,
                      absl::string_view piece2) noexcept;

/// Returns whether a given ASCII string `text` starts with `prefix`,
/// ignoring case in the comparison.
///
/// @param text The string to test.
/// @param prefix The prefix to look for.
/// @return `true` if `text` starts with `prefix` ignoring case, `false` otherwise.
bool StartsWithIgnoreCase(absl::string_view text,
                          absl::string_view prefix) noexcept;

/// Returns whether a given ASCII string `text` ends with `suffix`, ignoring
/// case in the comparison.
///
/// @param text The string to test.
/// @param suffix The suffix to look for.
/// @return `true` if `text` ends with `suffix` ignoring case, `false` otherwise.
bool EndsWithIgnoreCase(absl::string_view text,
                        absl::string_view suffix) noexcept;

/// Yields the longest prefix in common between both input strings.
/// Pointer-wise, the returned result is a subset of input "a".
///
/// @param a The first string to compare.
/// @param b The second string to compare.
/// @return The longest common prefix, as a view into `a`.
absl::string_view FindLongestCommonPrefix(absl::string_view a,
                                          absl::string_view b);

/// Yields the longest suffix in common between both input strings.
/// Pointer-wise, the returned result is a subset of input "a".
///
/// @param a The first string to compare.
/// @param b The second string to compare.
/// @return The longest common suffix, as a view into `a`.
absl::string_view FindLongestCommonSuffix(absl::string_view a,
                                          absl::string_view b);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_MATCH_H_
