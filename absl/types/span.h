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
// span.h
// -----------------------------------------------------------------------------
//
// This header file defines a `Span<T>` type for holding a reference to existing
// array data. The `Span` object, much like the `absl::string_view` object,
// does not own such data itself, and the data being referenced by the span must
// outlive the span itself. Unlike `view` type references, a span can hold a
// reference to mutable data (and can mutate it for underlying types of
// non-const T.) A span provides a lightweight way to pass a reference to such
// data.
//
// Additionally, this header file defines `MakeSpan()` and `MakeConstSpan()`
// factory functions, for clearly creating spans of type `Span<T>` or read-only
// `Span<const T>` when such types may be difficult to identify due to issues
// with implicit conversion.
//
// The C++20 standard includes a `std::span` type. As of January 2026, the
// differences between `absl::Span` and `std::span` are:
//    * `absl::Span` has `operator==` (which is likely a design bug,
//       per https://abseil.io/blog/20180531-regular-types)
//    * `absl::Span` has the factory functions `MakeSpan()` and
//      `MakeConstSpan()`
//    * bounds-checked access to `absl::Span` is accomplished with `at()`
//      however `std::span` now supports the same as of the draft C++26 standard
//    * `absl::Span` has compiler-provided move and copy constructors and
//      assignment.
//    * `absl::Span` has no `bytes()`, `size_bytes()`, `as_bytes()`, or
//      `as_writable_bytes()` methods
//    * `absl::Span` has no static extent template parameter, nor constructors
//      which exist only because of the static extent parameter.
//    * `absl::Span` has an explicit mutable-reference constructor
//    * `absl::Span::subspan(pos, len)` always truncates `len` to
//      `size() - pos`, whereas `std::span::subspan()` only truncates when the
//      `len` parameter is defaulted.
//
// For more information, see the class comments below.
#ifndef ABSL_TYPES_SPAN_H_
#define ABSL_TYPES_SPAN_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"  // TODO(strel): remove this include
#include "absl/base/throw_delegate.h"
#include "absl/hash/internal/weakly_mixed_integer.h"
#include "absl/meta/type_traits.h"
#include "absl/types/internal/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename T>
class Span;

ABSL_NAMESPACE_END
}  // namespace absl

// If std::ranges is available, mark Span as satisfying the `view` and
// `borrowed_range` concepts, just like std::span.
#if __has_include(<version>)
#include <version>  // NOLINT(misc-include-cleaner)
#endif
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
#include <ranges>  // NOLINT(build/c++20)
template <typename T>
// NOLINTNEXTLINE(build/c++20)
inline constexpr bool std::ranges::enable_view<absl::Span<T>> = true;
template <typename T>
// NOLINTNEXTLINE(build/c++20)
inline constexpr bool std::ranges::enable_borrowed_range<absl::Span<T>> = true;
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

//------------------------------------------------------------------------------
// Span
//------------------------------------------------------------------------------
//
// A `Span` is an "array reference" type for holding a reference of contiguous
// array data; the `Span` object does not and cannot own such data itself. A
// span provides an easy way to provide overloads for anything operating on
// contiguous sequences without needing to manage pointers and array lengths
// manually.

// A span is conceptually a pointer (ptr) and a length (size) into an already
// existing array of contiguous memory; the array it represents references the
// elements "ptr[0] .. ptr[size-1]". Passing a properly-constructed `Span`
// instead of raw pointers avoids many issues related to index out of bounds
// errors.
//
// Spans may also be constructed from containers holding contiguous sequences.
// Such containers must supply `data()` and `size() const` methods (e.g
// `std::vector<T>`, `absl::InlinedVector<T, N>`). All implicit conversions to
// `absl::Span` from such containers will create spans of type `const T`;
// spans which can mutate their values (of type `T`) must use explicit
// constructors.
//
// A `Span<T>` is somewhat analogous to an `absl::string_view`, but for an array
// of elements of type `T`, and unlike an `absl::string_view`, a span can hold a
// reference to mutable data. A user of `Span` must ensure that the data being
// pointed to outlives the `Span` itself.
//
// You can construct a `Span<T>` in several ways:
//
//   * Explicitly from a reference to a container type
//   * Explicitly from a pointer and size
//   * Implicitly from a container type (but only for spans of type `const T`)
//   * Using the `MakeSpan()` or `MakeConstSpan()` factory functions.
//
// Examples:
//
//   // Construct a Span explicitly from a container:
//   std::vector<int> v = {1, 2, 3, 4, 5};
//   auto span = absl::Span<const int>(v);
//
//   // Construct a Span explicitly from a C-style array:
//   int a[5] =  {1, 2, 3, 4, 5};
//   auto span = absl::Span<const int>(a);
//
//   // Construct a Span implicitly from a container
//   void MyRoutine(absl::Span<const int> a) {
//     ...
//   }
//   std::vector v = {1,2,3,4,5};
//   MyRoutine(v)                     // convert to Span<const T>
//
// Note that `Span` objects, in addition to requiring that the memory they
// point to remains alive, must also ensure that such memory does not get
// reallocated. Therefore, to avoid undefined behavior, containers with
// associated spans should not invoke operations that may reallocate memory
// (such as resizing) or invalidate iterators into the container.
//
// One common use for a `Span` is when passing arguments to a routine that can
// accept a variety of array types (e.g. a `std::vector`, `absl::InlinedVector`,
// a C-style array, etc.). Instead of creating overloads for each case, you
// can simply specify a `Span` as the argument to such a routine.
//
// Example:
//
//   void MyRoutine(absl::Span<const int> a) {
//     ...
//   }
//
//   std::vector v = {1,2,3,4,5};
//   MyRoutine(v);
//
//   absl::InlinedVector<int, 4> my_inline_vector;
//   MyRoutine(my_inline_vector);
//
//   // Explicit constructor from pointer,size
//   int* my_array = new int[10];
//   MyRoutine(absl::Span<const int>(my_array, 10));
/// A non-owning view over a contiguous sequence of objects.
///
/// A `Span` provides a bounds-checked, `std::vector`-like interface over an
/// array whose storage is owned elsewhere. It can be constructed from a
/// pointer and length, a C array, or any contiguous container, and it must not
/// outlive the data it refers to.
template <typename T>
class ABSL_ATTRIBUTE_VIEW Span {
 private:
  // Used to determine whether a Span can be constructed from a container of
  // type C.
  template <typename C>
  using EnableIfConvertibleFrom =
      std::enable_if_t<!std::is_same_v<Span, std::remove_reference_t<C>> &&
                       span_internal::HasData<T, C>::value &&
                       span_internal::HasSize<C>::value>;

  // Used to SFINAE-enable a function when the slice elements are const.
  template <typename U>
  using EnableIfValueIsConst = std::enable_if_t<std::is_const_v<T>, U>;

  // Used to SFINAE-enable a function when the slice elements are mutable.
  template <typename U>
  using EnableIfValueIsMutable = std::enable_if_t<!std::is_const_v<T>, U>;

 public:
  /// The element type, including any cv-qualification.
  using element_type = T;
  /// The element type with cv-qualifiers removed.
  using value_type = std::remove_cv_t<T>;
  // TODO(b/316099902) - pointer should be absl_nullable, but this makes it hard
  // to recognize foreach loops as safe. absl_nullability_unknown is currently
  // used to suppress -Wnullability-completeness warnings.
  /// A pointer to an element.
  using pointer = T* absl_nullability_unknown;
  /// A pointer to a const element.
  using const_pointer = const T* absl_nullability_unknown;
  /// A reference to an element.
  using reference = T&;
  /// A reference to a const element.
  using const_reference = const T&;
  /// A random-access iterator over the elements.
  using iterator = pointer;
  /// A random-access iterator over const elements.
  using const_iterator = const_pointer;
  /// A reverse iterator over the elements.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// A reverse iterator over const elements.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  /// An unsigned type used for sizes and indices.
  using size_type = size_t;
  /// A signed type used for iterator differences.
  using difference_type = ptrdiff_t;
  /// Tag marking `Span` as a view type.
  using absl_internal_is_view = std::true_type;

  /// Sentinel size value meaning "until the end of the span".
  // NOLINTNEXTLINE
  static constexpr size_type npos = static_cast<size_type>(-1);

  /// Constructs an empty span.
  constexpr Span() noexcept : Span(nullptr, 0) {}
  /// Constructs a span from a pointer and a length.
  ///
  /// @param array Pointer to the first element.
  /// @param length Number of elements in the span.
  constexpr Span(pointer array ABSL_ATTRIBUTE_LIFETIME_BOUND,
                 size_type length) noexcept
      : ptr_(array), len_(length) {}

  /// Constructs a span referring to a C array.
  ///
  /// @param a The array to refer to.
  template <size_t N>
  constexpr Span(T(  // NOLINT(google-explicit-constructor)
      &a ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept
      : Span(a, N) {}

  /// Constructs a mutable span from a container.
  ///
  /// Can be replaced with `MakeSpan()` to infer the type parameter.
  ///
  /// @param v The container whose data the span refers to.
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsMutable<V>,
            typename = span_internal::EnableIfNotIsView<V>>
  explicit Span(
      V& v
          ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/references)
      : Span(span_internal::GetData(v), v.size()) {}

  /// Constructs a read-only `Span<const T>` from a container.
  ///
  /// @param v The container whose data the span refers to.
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsConst<V>,
            typename = span_internal::EnableIfNotIsView<V>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Span(const V& v ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : Span(span_internal::GetData(v), v.size()) {}

  // Overloads of the above two functions that are only enabled for view types.
  // This is so we can drop the ABSL_ATTRIBUTE_LIFETIME_BOUND annotation. These
  // overloads must be made unique by using a different template parameter list
  // (hence the = 0 for the IsView enabler).
  /// Constructs a mutable span from a view type (no lifetime bound).
  ///
  /// @param v The view whose data the span refers to.
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsMutable<V>,
            span_internal::EnableIfIsView<V> = 0>
  explicit Span(V& v) noexcept  // NOLINT(runtime/references)
      : Span(span_internal::GetData(v), v.size()) {}
  /// Constructs a read-only span from a view type (no lifetime bound).
  ///
  /// @param v The view whose data the span refers to.
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsConst<V>,
            span_internal::EnableIfIsView<V> = 0>
  constexpr Span(const V& v) noexcept  // NOLINT(google-explicit-constructor)
      : Span(span_internal::GetData(v), v.size()) {}

  // Implicit constructor from an initializer list, making it possible to pass a
  // brace-enclosed initializer list to a function expecting a `Span`. Such
  // spans constructed from an initializer list must be of type `Span<const T>`.
  //
  //   void Process(absl::Span<const int> x);
  //   Process({1, 2, 3});
  //
  // Note that as always the array referenced by the span must outlive the span.
  // Since an initializer list constructor acts as if it is fed a temporary
  // array (cf. C++ standard [dcl.init.list]/5), it's safe to use this
  // constructor only when the `std::initializer_list` itself outlives the span.
  // In order to meet this requirement it's sufficient to ensure that neither
  // the span nor a copy of it is used outside of the expression in which it's
  // created:
  //
  //   // Assume that this function uses the array directly, not retaining any
  //   // copy of the span or pointer to any of its elements.
  //   void Process(absl::Span<const int> ints);
  //
  //   // Okay: the std::initializer_list<int> will reference a temporary array
  //   // that isn't destroyed until after the call to Process returns.
  //   Process({ 17, 19 });
  //
  //   // Not okay: the storage used by the std::initializer_list<int> is not
  //   // allowed to be referenced after the first line.
  //   absl::Span<const int> ints = { 17, 19 };
  //   Process(ints);
  //
  //   // Not okay for the same reason as above: even when the elements of the
  //   // initializer list expression are not temporaries the underlying array
  //   // is, so the initializer list must still outlive the span.
  //   const int foo = 17;
  //   absl::Span<const int> ints = { foo };
  //   Process(ints);
  //
  /// Constructs a read-only span from an initializer list.
  ///
  /// The referenced array must outlive the span; see the notes above.
  ///
  /// @param v The initializer list whose data the span refers to.
  template <typename LazyT = T,
            typename = EnableIfValueIsConst<LazyT>>
  Span(std::initializer_list<value_type> v
           ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/explicit)
      : Span(v.begin(), v.size()) {}

  // Accessors

  /// Returns a pointer to the span's underlying array of data.
  ///
  /// @return A pointer to the first element (held outside the span).
  constexpr pointer data() const noexcept { return ptr_; }

  /// Returns the number of elements in this span.
  ///
  /// @return The size of the span.
  constexpr size_type size() const noexcept { return len_; }

  /// Returns the length (size) of this span.
  ///
  /// @return The number of elements, equal to `size()`.
  constexpr size_type length() const noexcept { return size(); }

  /// Reports whether this span is empty.
  ///
  /// @return `true` if the span contains no elements.
  constexpr bool empty() const noexcept { return size() == 0; }

  /// Returns a reference to the `i`th element of this span.
  ///
  /// @param i Index of the element to access.
  /// @return A reference to the element at index `i`.
  constexpr reference operator[](size_type i) const noexcept {
    absl::base_internal::HardeningAssertLT(i, size());
    return ptr_[i];
  }

  /// Returns a reference to the `i`th element, with bounds checking.
  ///
  /// @param i Index of the element to access.
  /// @return A reference to the element at index `i`.
  constexpr reference at(size_type i) const {
    return ABSL_PREDICT_TRUE(i < size())  //
               ? *(data() + i)
               : (ThrowStdOutOfRange("Span::at failed bounds check"),
                  *(data() + i));
  }

  /// Returns a reference to the first element; the span must not be empty.
  ///
  /// @return A reference to the first element.
  constexpr reference front() const noexcept {
    absl::base_internal::HardeningAssertGT(size(), static_cast<size_t>(0));
    return *data();
  }

  /// Returns a reference to the last element; the span must not be empty.
  ///
  /// @return A reference to the last element.
  constexpr reference back() const noexcept {
    absl::base_internal::HardeningAssertGT(size(), static_cast<size_t>(0));
    return *(data() + size() - 1);
  }

  /// Returns an iterator to the first element.
  ///
  /// @return An iterator to the first element, or `end()` if empty.
  constexpr iterator begin() const noexcept { return data(); }

  /// Returns a const iterator to the first element.
  ///
  /// @return A const iterator to the first element, or `cend()` if empty.
  constexpr const_iterator cbegin() const noexcept { return begin(); }

  /// Returns an iterator just past the last element.
  ///
  /// @return A past-the-end iterator; dereferencing it is undefined behavior.
  constexpr iterator end() const noexcept { return data() + size(); }

  /// Returns a const iterator just past the last element.
  ///
  /// @return A past-the-end const iterator; dereferencing it is undefined.
  constexpr const_iterator cend() const noexcept { return end(); }

  /// Returns a reverse iterator to the last element.
  ///
  /// @return A reverse iterator to the last element, or `rend()` if empty.
  constexpr reverse_iterator rbegin() const noexcept {
    return reverse_iterator(end());
  }

  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the last element, or `crend()`.
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  /// Returns a reverse iterator just before the first element.
  ///
  /// @return A reverse past-the-end iterator; dereferencing it is undefined.
  constexpr reverse_iterator rend() const noexcept {
    return reverse_iterator(begin());
  }

  /// Returns a const reverse iterator just before the first element.
  ///
  /// @return A const reverse past-the-end iterator; dereferencing is undefined.
  constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // Span mutations

  /// Removes the first `n` elements from the span.
  ///
  /// @param n Number of elements to remove from the front.
  void remove_prefix(size_type n) noexcept {
    absl::base_internal::HardeningAssertGE(size(), n);
    ptr_ += n;
    len_ -= n;
  }

  /// Removes the last `n` elements from the span.
  ///
  /// @param n Number of elements to remove from the back.
  void remove_suffix(size_type n) noexcept {
    absl::base_internal::HardeningAssertGE(size(), n);
    len_ -= n;
  }

  // Span::subspan()
  //
  // Returns a `Span` starting at element `pos` and of length `len`. Both `pos`
  // and `len` are of type `size_type` and thus non-negative. Parameter `pos`
  // must be <= size(). Any `len` value that points past the end of the span
  // will be trimmed to at most size() - `pos`. A default `len` value of `npos`
  // ensures the returned subspan continues until the end of the span.
  //
  // Note that trimming behavior differs from `std::span::subspan()`.
  // `std::span::subspan()` requires `len == npos || pos + len <= size()`.
  // In other words, `std::span::subspan()` only trims `len` when its value is
  // defaulted.
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).subspan(1, 2);  // {11, 12}
  //   absl::MakeSpan(vec).subspan(2, 8);  // {12, 13}
  //   absl::MakeSpan(vec).subspan(1);     // {11, 12, 13}
  //   absl::MakeSpan(vec).subspan(4);     // {}
  //   absl::MakeSpan(vec).subspan(5);     // throws std::out_of_range
  /// Returns a subspan starting at `pos` and spanning `len` elements.
  ///
  /// `pos` must be `<= size()`. Any `len` past the end is trimmed to
  /// `size() - pos`. The default `len` of `npos` runs to the end of the span.
  ///
  /// @param pos Index of the first element of the subspan.
  /// @param len Number of elements in the subspan, or `npos` for the rest.
  /// @return A span over the requested range.
  constexpr Span subspan(size_type pos = 0, size_type len = npos) const {
    return (pos <= size()) ? Span(data() + pos, (std::min)(size() - pos, len))
                           : (ThrowStdOutOfRange("pos > size()"), Span());
  }

  // Span::first()
  //
  // Returns a `Span` containing first `len` elements. Parameter `len` is of
  // type `size_type` and thus non-negative. `len` value must be <= size().
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).first(1);  // {10}
  //   absl::MakeSpan(vec).first(3);  // {10, 11, 12}
  //   absl::MakeSpan(vec).first(5);  // throws std::out_of_range
  /// Returns a span containing the first `len` elements.
  ///
  /// `len` must be `<= size()`.
  ///
  /// @param len Number of elements from the front to include.
  /// @return A span over the first `len` elements.
  constexpr Span first(size_type len) const {
    return (len <= size()) ? Span(data(), len)
                           : (ThrowStdOutOfRange("len > size()"), Span());
  }

  // Span::last()
  //
  // Returns a `Span` containing last `len` elements. Parameter `len` is of
  // type `size_type` and thus non-negative. `len` value must be <= size().
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).last(1);  // {13}
  //   absl::MakeSpan(vec).last(3);  // {11, 12, 13}
  //   absl::MakeSpan(vec).last(5);  // throws std::out_of_range
  /// Returns a span containing the last `len` elements.
  ///
  /// `len` must be `<= size()`.
  ///
  /// @param len Number of elements from the back to include.
  /// @return A span over the last `len` elements.
  constexpr Span last(size_type len) const {
    return (len <= size()) ? Span(size() - len + data(), len)
                           : (ThrowStdOutOfRange("len > size()"), Span());
  }

  /// Hashes the span's elements for use with `absl::Hash`.
  ///
  /// @param h The hash state to combine into.
  /// @param v The span whose elements are hashed.
  /// @return The updated hash state.
  template <typename H>
  friend H AbslHashValue(H h, Span v) {
    return H::combine_contiguous(std::move(h), v.data(), v.size());
  }

 private:
  pointer ptr_;
  size_type len_;
};

// Span relationals

// Equality is compared element-by-element, while ordering is lexicographical.
// We provide three overloads for each operator to cover any combination on the
// left or right hand side of mutable Span<T>, read-only Span<const T>, and
// convertible-to-read-only Span<T>.
// TODO(zhangxy): Due to MSVC overload resolution bug with partial ordering
// template functions, 5 overloads per operator is needed as a workaround. We
// should update them to 3 overloads per operator using non-deduced context like
// string_view, i.e.
// - (Span<T>, Span<T>)
// - (Span<T>, non_deduced<Span<const T>>)
// - (non_deduced<Span<const T>>, Span<T>)

/// Compares two spans for element-wise equality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans have equal elements.
template <typename T>
constexpr bool operator==(Span<T> a, Span<T> b) {
  return span_internal::EqualImpl<Span, const T>(a, b);
}
/// Compares two spans for element-wise equality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans have equal elements.
template <typename T>
constexpr bool operator==(Span<const T> a, Span<T> b) {
  return span_internal::EqualImpl<Span, const T>(a, b);
}
/// Compares two spans for element-wise equality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans have equal elements.
template <typename T>
constexpr bool operator==(Span<T> a, Span<const T> b) {
  return span_internal::EqualImpl<Span, const T>(a, b);
}
/// Compares a container to a span for element-wise equality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the operands have equal elements.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator==(const U& a, Span<T> b) {
  return span_internal::EqualImpl<Span, const T>(a, b);
}
/// Compares a span to a container for element-wise equality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the operands have equal elements.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator==(Span<T> a, const U& b) {
  return span_internal::EqualImpl<Span, const T>(a, b);
}

/// Compares two spans for inequality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans differ.
template <typename T>
constexpr bool operator!=(Span<T> a, Span<T> b) {
  return !(a == b);
}
/// Compares two spans for inequality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans differ.
template <typename T>
constexpr bool operator!=(Span<const T> a, Span<T> b) {
  return !(a == b);
}
/// Compares two spans for inequality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the spans differ.
template <typename T>
constexpr bool operator!=(Span<T> a, Span<const T> b) {
  return !(a == b);
}
/// Compares a container to a span for inequality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the operands differ.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator!=(const U& a, Span<T> b) {
  return !(a == b);
}
/// Compares a span to a container for inequality.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if the operands differ.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator!=(Span<T> a, const U& b) {
  return !(a == b);
}

/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically less than `b`.
template <typename T>
constexpr bool operator<(Span<T> a, Span<T> b) {
  return span_internal::LessThanImpl<Span, const T>(a, b);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically less than `b`.
template <typename T>
constexpr bool operator<(Span<const T> a, Span<T> b) {
  return span_internal::LessThanImpl<Span, const T>(a, b);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically less than `b`.
template <typename T>
constexpr bool operator<(Span<T> a, Span<const T> b) {
  return span_internal::LessThanImpl<Span, const T>(a, b);
}
/// Compares a container and a span lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically less than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<(const U& a, Span<T> b) {
  return span_internal::LessThanImpl<Span, const T>(a, b);
}
/// Compares a span and a container lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically less than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<(Span<T> a, const U& b) {
  return span_internal::LessThanImpl<Span, const T>(a, b);
}

/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically greater than `b`.
template <typename T>
constexpr bool operator>(Span<T> a, Span<T> b) {
  return b < a;
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically greater than `b`.
template <typename T>
constexpr bool operator>(Span<const T> a, Span<T> b) {
  return b < a;
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically greater than `b`.
template <typename T>
constexpr bool operator>(Span<T> a, Span<const T> b) {
  return b < a;
}
/// Compares a container and a span lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically greater than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>(const U& a, Span<T> b) {
  return b < a;
}
/// Compares a span and a container lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is lexicographically greater than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>(Span<T> a, const U& b) {
  return b < a;
}

/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically greater than `b`.
template <typename T>
constexpr bool operator<=(Span<T> a, Span<T> b) {
  return !(b < a);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically greater than `b`.
template <typename T>
constexpr bool operator<=(Span<const T> a, Span<T> b) {
  return !(b < a);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically greater than `b`.
template <typename T>
constexpr bool operator<=(Span<T> a, Span<const T> b) {
  return !(b < a);
}
/// Compares a container and a span lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically greater than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<=(const U& a, Span<T> b) {
  return !(b < a);
}
/// Compares a span and a container lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically greater than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<=(Span<T> a, const U& b) {
  return !(b < a);
}

/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically less than `b`.
template <typename T>
constexpr bool operator>=(Span<T> a, Span<T> b) {
  return !(a < b);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically less than `b`.
template <typename T>
constexpr bool operator>=(Span<const T> a, Span<T> b) {
  return !(a < b);
}
/// Compares two spans lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically less than `b`.
template <typename T>
constexpr bool operator>=(Span<T> a, Span<const T> b) {
  return !(a < b);
}
/// Compares a container and a span lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically less than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>=(const U& a, Span<T> b) {
  return !(a < b);
}
/// Compares a span and a container lexicographically.
/// @param a The left operand.
/// @param b The right operand.
/// @return `true` if `a` is not lexicographically less than `b`.
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>=(Span<T> a, const U& b) {
  return !(a < b);
}

// MakeSpan()
//
// Constructs a mutable `Span<T>`, deducing `T` automatically from either a
// container or pointer+size.
//
// Because a read-only `Span<const T>` is implicitly constructed from container
// types regardless of whether the container itself is a const container,
// constructing mutable spans of type `Span<T>` from containers requires
// explicit constructors. The container-accepting version of `MakeSpan()`
// deduces the type of `T` by the constness of the pointer received from the
// container's `data()` member. Similarly, the pointer-accepting version returns
// a `Span<const T>` if `T` is `const`, and a `Span<T>` otherwise.
//
// Examples:
//
//   void MyRoutine(absl::Span<MyComplicatedType> a) {
//     ...
//   };
//   // my_vector is a container of non-const types
//   std::vector<MyComplicatedType> my_vector;
//
//   // Constructing a Span implicitly attempts to create a Span of type
//   // `Span<const T>`
//   MyRoutine(my_vector);                // error, type mismatch
//
//   // Explicitly constructing the Span is verbose
//   MyRoutine(absl::Span<MyComplicatedType>(my_vector));
//
//   // Use MakeSpan() to make an absl::Span<T>
//   MyRoutine(absl::MakeSpan(my_vector));
//
//   // Construct a span from an array ptr+size
//   absl::Span<T> my_span() {
//     return absl::MakeSpan(&array[0], num_elements_);
//   }
//
// NOTE: To avoid undefined behavior if the container is empty, use `.data()`
// or pass the container directly instead of using `&v[0]` or `&v[v.size()]`.
//
/// Constructs a mutable `Span<T>` from a pointer and a size.
///
/// @param ptr Pointer to the first element.
/// @param size Number of elements.
/// @return A span over `size` elements starting at `ptr`.
template <int&... ExplicitArgumentBarrier, typename T>
constexpr Span<T> MakeSpan(T* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND,
                           size_t size) noexcept {
  return Span<T>(ptr, size);
}

/// Constructs a mutable `Span<T>` from a `[begin, end)` pointer pair.
///
/// @param begin Pointer to the first element.
/// @param end Pointer just past the last element.
/// @return A span over the range `[begin, end)`.
template <int&... ExplicitArgumentBarrier, typename T>
Span<T> MakeSpan(T* absl_nullable begin ABSL_ATTRIBUTE_LIFETIME_BOUND,
                 T* absl_nullable end) noexcept {
  absl::base_internal::HardeningAssertLE(begin, end);
  return Span<T>(begin, static_cast<size_t>(end - begin));
}

/// Constructs a mutable `Span` from a view container.
///
/// @param c The container whose data the span refers to.
/// @return A span over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeSpan(C& c) noexcept  // NOLINT(runtime/references)
    -> std::enable_if_t<span_internal::IsView<C>::value,
                        decltype(absl::MakeSpan(span_internal::GetData(c),
                                                c.size()))> {
  return MakeSpan(span_internal::GetData(c), c.size());
}

/// Constructs a mutable `Span` from a non-view container.
///
/// @param c The container whose data the span refers to.
/// @return A span over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeSpan(
    C& c ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/references)
    -> std::enable_if_t<!span_internal::IsView<C>::value,
                        decltype(absl::MakeSpan(span_internal::GetData(c),
                                                c.size()))> {
  return MakeSpan(span_internal::GetData(c), c.size());
}

/// Constructs a mutable `Span<T>` from a C array.
///
/// @param array The array whose elements the span refers to.
/// @return A span over all elements of `array`.
template <int&... ExplicitArgumentBarrier, typename T, size_t N>
constexpr Span<T> MakeSpan(
    T (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept {
  return Span<T>(array, N);
}

// MakeConstSpan()
//
// Constructs a `Span<const T>` as with `MakeSpan`, deducing `T` automatically,
// but always returning a `Span<const T>`.
//
// Examples:
//
//   void ProcessInts(absl::Span<const int> some_ints);
//
//   // Call with a pointer and size.
//   int array[3] = { 0, 0, 0 };
//   ProcessInts(absl::MakeConstSpan(&array[0], 3));
//
//   // Call with a [begin, end) pair.
//   ProcessInts(absl::MakeConstSpan(&array[0], &array[3]));
//
//   // Call directly with an array.
//   ProcessInts(absl::MakeConstSpan(array));
//
//   // Call with a contiguous container.
//   std::vector<int> some_ints = ...;
//   ProcessInts(absl::MakeConstSpan(some_ints));
//   ProcessInts(absl::MakeConstSpan(std::vector<int>{ 0, 0, 0 }));
//
/// Constructs a `Span<const T>` from a pointer and a size.
///
/// @param ptr Pointer to the first element.
/// @param size Number of elements.
/// @return A read-only span over `size` elements starting at `ptr`.
template <int&... ExplicitArgumentBarrier, typename T>
constexpr Span<const T> MakeConstSpan(
    T* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND, size_t size) noexcept {
  return Span<const T>(ptr, size);
}

/// Constructs a `Span<const T>` from a `[begin, end)` pointer pair.
///
/// @param begin Pointer to the first element.
/// @param end Pointer just past the last element.
/// @return A read-only span over the range `[begin, end)`.
template <int&... ExplicitArgumentBarrier, typename T>
Span<const T> MakeConstSpan(T* absl_nullable begin
                                ABSL_ATTRIBUTE_LIFETIME_BOUND,
                            T* absl_nullable end) noexcept {
  absl::base_internal::HardeningAssertLE(begin, end);
  return Span<const T>(begin, end - begin);
}

/// Constructs a `Span<const T>` from a view container.
///
/// @param c The container whose data the span refers to.
/// @return A read-only span over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeConstSpan(const C& c) noexcept
    -> std::enable_if_t<span_internal::IsView<C>::value,
                        decltype(MakeSpan(c))> {
  return MakeSpan(c);
}

/// Constructs a `Span<const T>` from a non-view container.
///
/// @param c The container whose data the span refers to.
/// @return A read-only span over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeConstSpan(const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
    -> std::enable_if_t<!span_internal::IsView<C>::value,
                        decltype(MakeSpan(c))> {
  return MakeSpan(c);
}

/// Constructs a `Span<const T>` from a C array.
///
/// @param array The array whose elements the span refers to.
/// @return A read-only span over all elements of `array`.
template <int&... ExplicitArgumentBarrier, typename T, size_t N>
constexpr Span<const T> MakeConstSpan(
    const T (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept {
  return Span<const T>(array, N);
}
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_TYPES_SPAN_H_
