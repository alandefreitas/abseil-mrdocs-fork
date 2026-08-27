// Copyright 2022 The Abseil Authors.
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
// File: charset.h
// -----------------------------------------------------------------------------
//
// This file contains absl::CharSet, a fast, bit-vector set of 8-bit unsigned
// characters.
//
// Instances can be initialized as constexpr constants. For example:
//
//   constexpr absl::CharSet kJustX = absl::CharSet::Char('x');
//   constexpr absl::CharSet kMySymbols = absl::CharSet("$@!");
//   constexpr absl::CharSet kLetters = absl::CharSet::Range('a', 'z');
//
// Multiple instances can be combined that still forms a constexpr expression.
// For example:
//
//   constexpr absl::CharSet kLettersAndNumbers =
//       absl::CharSet::Range('a', 'z') | absl::CharSet::Range('0', '9');
//
// Several pre-defined character classes are available that mirror the methods
// from <cctype>. For example:
//
//   constexpr absl::CharSet kLettersAndWhitespace =
//       absl::CharSet::AsciiAlphabet() | absl::CharSet::AsciiWhitespace();
//
// To check membership, use the .contains method, e.g.
//
//   absl::CharSet hex_letters("abcdef");
//   hex_letters.contains('a');  // true
//   hex_letters.contains('g');  // false

#ifndef ABSL_STRINGS_CHARSET_H_
#define ABSL_STRINGS_CHARSET_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// A fast, bit-vector set of 8-bit unsigned characters.
class CharSet {
 public:
  /// Constructs an empty character set.
  constexpr CharSet() : m_() {}

  /// Initializes with a given string_view.
  ///
  /// @param str The characters to include in the set.
  constexpr explicit CharSet(absl::string_view str) : m_() {
    for (char c : str) {
      SetChar(static_cast<unsigned char>(c));
    }
  }

  /// Tests whether the set contains a given character.
  ///
  /// @param c The character to test.
  /// @return `true` if `c` is a member of the set.
  constexpr bool contains(char c) const {
    return ((m_[static_cast<unsigned char>(c) / 64] >>
             (static_cast<unsigned char>(c) % 64)) &
            0x1) == 0x1;
  }

  /// Tests whether the set is empty.
  ///
  /// @return `true` if the set contains no characters.
  constexpr bool empty() const {
    for (uint64_t c : m_) {
      if (c != 0) return false;
    }
    return true;
  }

  /// Containing only a single specified char.
  ///
  /// @param x The single character to include.
  /// @return A set containing only `x`.
  static constexpr CharSet Char(char x) {
    return CharSet(CharMaskForWord(x, 0), CharMaskForWord(x, 1),
                   CharMaskForWord(x, 2), CharMaskForWord(x, 3));
  }

  /// Containing all the chars in the closed interval [lo,hi].
  ///
  /// @param lo The lowest character in the range.
  /// @param hi The highest character in the range.
  /// @return A set containing every character in `[lo, hi]`.
  static constexpr CharSet Range(char lo, char hi) {
    return CharSet(RangeForWord(lo, hi, 0), RangeForWord(lo, hi, 1),
                   RangeForWord(lo, hi, 2), RangeForWord(lo, hi, 3));
  }

  /// Computes the intersection of two character sets.
  ///
  /// @param a The left-hand operand.
  /// @param b The right-hand operand.
  /// @return A set with the characters present in both `a` and `b`.
  friend constexpr CharSet operator&(const CharSet& a, const CharSet& b) {
    return CharSet(a.m_[0] & b.m_[0], a.m_[1] & b.m_[1], a.m_[2] & b.m_[2],
                   a.m_[3] & b.m_[3]);
  }

  /// Computes the union of two character sets.
  ///
  /// @param a The left-hand operand.
  /// @param b The right-hand operand.
  /// @return A set with the characters present in either `a` or `b`.
  friend constexpr CharSet operator|(const CharSet& a, const CharSet& b) {
    return CharSet(a.m_[0] | b.m_[0], a.m_[1] | b.m_[1], a.m_[2] | b.m_[2],
                   a.m_[3] | b.m_[3]);
  }

  /// Computes the complement of a character set.
  ///
  /// @param a The operand to complement.
  /// @return A set containing every character not present in `a`.
  friend constexpr CharSet operator~(const CharSet& a) {
    return CharSet(~a.m_[0], ~a.m_[1], ~a.m_[2], ~a.m_[3]);
  }

  /// The set of ASCII uppercase letters.
  ///
  /// @return A set containing `A` through `Z`.
  static constexpr CharSet AsciiUppercase() { return CharSet::Range('A', 'Z'); }
  /// The set of ASCII lowercase letters.
  ///
  /// @return A set containing `a` through `z`.
  static constexpr CharSet AsciiLowercase() { return CharSet::Range('a', 'z'); }
  /// The set of ASCII decimal digits.
  ///
  /// @return A set containing `0` through `9`.
  static constexpr CharSet AsciiDigits() { return CharSet::Range('0', '9'); }
  /// The set of ASCII letters.
  ///
  /// @return A set containing the ASCII lowercase and uppercase letters.
  static constexpr CharSet AsciiAlphabet() {
    return AsciiLowercase() | AsciiUppercase();
  }
  /// The set of ASCII letters and digits.
  ///
  /// @return A set containing the ASCII letters and decimal digits.
  static constexpr CharSet AsciiAlphanumerics() {
    return AsciiDigits() | AsciiAlphabet();
  }
  /// The set of ASCII hexadecimal digits.
  ///
  /// @return A set containing `0`-`9`, `A`-`F`, and `a`-`f`.
  static constexpr CharSet AsciiHexDigits() {
    return AsciiDigits() | CharSet::Range('A', 'F') | CharSet::Range('a', 'f');
  }
  /// The set of printable ASCII characters.
  ///
  /// @return A set containing the printable ASCII characters.
  static constexpr CharSet AsciiPrintable() {
    return CharSet::Range(0x20, 0x7e);
  }
  /// The set of ASCII whitespace characters.
  ///
  /// @return A set containing the ASCII whitespace characters.
  static constexpr CharSet AsciiWhitespace() { return CharSet("\t\n\v\f\r "); }
  /// The set of ASCII punctuation characters.
  ///
  /// @return A set containing the printable, non-alphanumeric,
  ///         non-whitespace ASCII characters.
  static constexpr CharSet AsciiPunctuation() {
    return AsciiPrintable() & ~AsciiWhitespace() & ~AsciiAlphanumerics();
  }

 private:
  constexpr CharSet(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3)
      : m_{b0, b1, b2, b3} {}

  static constexpr uint64_t RangeForWord(char lo, char hi, uint64_t word) {
    return OpenRangeFromZeroForWord(static_cast<unsigned char>(hi) + 1, word) &
           ~OpenRangeFromZeroForWord(static_cast<unsigned char>(lo), word);
  }

  // All the chars in the specified word of the range [0, upper).
  static constexpr uint64_t OpenRangeFromZeroForWord(uint64_t upper,
                                                     uint64_t word) {
    return (upper <= 64 * word) ? 0
           : (upper >= 64 * (word + 1))
               ? ~static_cast<uint64_t>(0)
               : (~static_cast<uint64_t>(0) >> (64 - upper % 64));
  }

  static constexpr uint64_t CharMaskForWord(char x, uint64_t word) {
    return (static_cast<unsigned char>(x) / 64 == word)
               ? (static_cast<uint64_t>(1)
                  << (static_cast<unsigned char>(x) % 64))
               : 0;
  }

  constexpr void SetChar(unsigned char c) {
    m_[c / 64] |= static_cast<uint64_t>(1) << (c % 64);
  }

  uint64_t m_[4];
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CHARSET_H_
