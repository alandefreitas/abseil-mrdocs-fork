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
// File: string_view.h
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of
// `absl::string_view` as a polyfill for `std::string_view` prior to C++17. Now
// that C++17 is required, `absl::string_view` is an alias for
// `std::string_view`

#ifndef ABSL_STRINGS_STRING_VIEW_H_
#define ABSL_STRINGS_STRING_VIEW_H_

#include <string_view>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"

// The Abseil library namespace.
namespace absl {
ABSL_NAMESPACE_BEGIN

/// An alias for `std::string_view`.
using std::string_view;

/// Like `s.substr(pos, n)`, but clips `pos` to an upper bound of `s.size()`.
///
/// Provided because std::string_view::substr throws if `pos > size()`.
///
/// @param s The string view to take a substring of.
/// @param pos The starting position, clipped to `s.size()`.
/// @param n The number of characters to include in the substring.
/// @return A view of the clipped substring.
inline string_view ClippedSubstr(string_view s ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                 size_t pos, size_t n = string_view::npos) {
  pos = (std::min)(pos, static_cast<size_t>(s.size()));
  return s.substr(pos, n);
}

/// Creates an `absl::string_view` from a pointer `p` even if it's null-valued.
///
/// This function should be used where an `absl::string_view` can be created
/// from a possibly-null pointer.
///
/// @param p The possibly-null pointer to create a view from.
/// @return A view of `p`, or an empty view if `p` is null.
constexpr string_view NullSafeStringView(const char* absl_nullable p) {
  return p ? string_view(p) : string_view();
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_STRING_VIEW_H_
