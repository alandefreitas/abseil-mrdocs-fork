// Copyright 2019 The Abseil Authors.
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
// File: inlined_vector.h
// -----------------------------------------------------------------------------
//
// This header file contains the declaration and definition of an "inlined
// vector" which behaves in an equivalent fashion to a `std::vector`, except
// that storage for small sequences of the vector are provided inline without
// requiring any heap allocation.
//
// An `absl::InlinedVector<T, N>` specifies the default capacity `N` as one of
// its template parameters. Instances where `size() <= N` hold contained
// elements in inline space. Typically `N` is very small so that sequences that
// are expected to be short do not require allocations.
//
// An `absl::InlinedVector` does not usually require a specific allocator. If
// the inlined vector grows beyond its initial constraints, it will need to
// allocate (as any normal `std::vector` would). This is usually performed with
// the default allocator (defined as `std::allocator<T>`). Optionally, a custom
// allocator type may be specified as `A` in `absl::InlinedVector<T, N, A>`.

#ifndef ABSL_CONTAINER_INLINED_VECTOR_H_
#define ABSL_CONTAINER_INLINED_VECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/algorithm/algorithm.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/internal/iterator_traits.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/base/throw_delegate.h"
#include "absl/container/internal/inlined_vector.h"
#include "absl/hash/internal/weakly_mixed_integer.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
/// A drop-in replacement for `std::vector` that stores small sequences inline.
///
/// An `absl::InlinedVector` is designed to be a drop-in replacement for
/// `std::vector` for use cases where the vector's size is sufficiently small
/// that it can be inlined. If the inlined vector does grow beyond its estimated
/// capacity, it will trigger an initial allocation on the heap, and will behave
/// as a `std::vector`. The API of the `absl::InlinedVector` within this file is
/// designed to cover the same API footprint as covered by `std::vector`.
template <typename T, size_t N, typename A = std::allocator<T>>
class ABSL_ATTRIBUTE_WARN_UNUSED InlinedVector {
  static_assert(N > 0, "absl::InlinedVector requires an inlined capacity.");

  using Storage = inlined_vector_internal::Storage<T, N, A>;

  template <typename TheA>
  using AllocatorTraits = inlined_vector_internal::AllocatorTraits<TheA>;
  template <typename TheA>
  using MoveIterator = inlined_vector_internal::MoveIterator<TheA>;
  template <typename TheA>
  using IsMoveAssignOk = inlined_vector_internal::IsMoveAssignOk<TheA>;

  template <typename TheA, typename Iterator>
  using IteratorValueAdapter =
      inlined_vector_internal::IteratorValueAdapter<TheA, Iterator>;
  template <typename TheA>
  using CopyValueAdapter = inlined_vector_internal::CopyValueAdapter<TheA>;
  template <typename TheA>
  using DefaultValueAdapter =
      inlined_vector_internal::DefaultValueAdapter<TheA>;

  template <typename Iterator>
  using EnableIfAtLeastForwardIterator = std::enable_if_t<
      base_internal::IsAtLeastForwardIterator<Iterator>::value, int>;
  template <typename Iterator>
  using DisableIfAtLeastForwardIterator = std::enable_if_t<
      !base_internal::IsAtLeastForwardIterator<Iterator>::value, int>;

  using MemcpyPolicy = typename Storage::MemcpyPolicy;
  using ElementwiseAssignPolicy = typename Storage::ElementwiseAssignPolicy;
  using ElementwiseConstructPolicy =
      typename Storage::ElementwiseConstructPolicy;
  using MoveAssignmentPolicy = typename Storage::MoveAssignmentPolicy;

 public:
  /// The allocator type used by the inlined vector.
  using allocator_type = A;
  /// The type of the elements stored in the inlined vector.
  using value_type = inlined_vector_internal::ValueType<A>;
  /// A pointer to an element.
  using pointer = inlined_vector_internal::Pointer<A>;
  /// A pointer to a const element.
  using const_pointer = inlined_vector_internal::ConstPointer<A>;
  /// An unsigned integral type used for sizes.
  using size_type = inlined_vector_internal::SizeType<A>;
  /// A signed integral type used for differences between iterators.
  using difference_type = inlined_vector_internal::DifferenceType<A>;
  /// A reference to an element.
  using reference = inlined_vector_internal::Reference<A>;
  /// A reference to a const element.
  using const_reference = inlined_vector_internal::ConstReference<A>;
  /// An iterator over the elements.
  using iterator = inlined_vector_internal::Iterator<A>;
  /// A const iterator over the elements.
  using const_iterator = inlined_vector_internal::ConstIterator<A>;
  /// A reverse iterator over the elements.
  using reverse_iterator = inlined_vector_internal::ReverseIterator<A>;
  /// A const reverse iterator over the elements.
  using const_reverse_iterator =
      inlined_vector_internal::ConstReverseIterator<A>;

  // ---------------------------------------------------------------------------
  // InlinedVector Constructors and Destructor
  // ---------------------------------------------------------------------------

  /// Creates an empty inlined vector with a value-initialized allocator.
  InlinedVector() noexcept(noexcept(allocator_type())) : storage_() {}

  /// Creates an empty inlined vector with a copy of `allocator`.
  ///
  /// @param allocator The allocator to copy.
  explicit InlinedVector(const allocator_type& allocator) noexcept
      : storage_(allocator) {}

  /// Creates an inlined vector with `n` copies of `value_type()`.
  ///
  /// @param n The number of elements to create.
  /// @param allocator The allocator to use.
  explicit InlinedVector(size_type n,
                         const allocator_type& allocator = allocator_type())
      : storage_(allocator) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::InlinedVector failed length check");
    }
    storage_.Initialize(DefaultValueAdapter<A>(), n);
  }

  /// Creates an inlined vector with `n` copies of `v`.
  ///
  /// @param n The number of elements to create.
  /// @param v The value to copy into each element.
  /// @param allocator The allocator to use.
  InlinedVector(size_type n, const_reference v,
                const allocator_type& allocator = allocator_type())
      : storage_(allocator) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::InlinedVector failed length check");
    }
    storage_.Initialize(CopyValueAdapter<A>(std::addressof(v)), n);
  }

  /// Creates an inlined vector with copies of the elements of `list`.
  ///
  /// @param list The initializer list to copy elements from.
  /// @param allocator The allocator to use.
  InlinedVector(std::initializer_list<value_type> list,
                const allocator_type& allocator = allocator_type())
      : InlinedVector(list.begin(), list.end(), allocator) {}

  /// Creates an inlined vector with elements constructed from the provided
  /// forward iterator range [`first`, `last`).
  ///
  /// NOTE: the `enable_if` prevents ambiguous interpretation between a call to
  /// this constructor with two integral arguments and a call to the above
  /// `InlinedVector(size_type, const_reference)` constructor.
  ///
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  /// @param allocator The allocator to use.
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator> = 0>
  InlinedVector(ForwardIterator first, ForwardIterator last,
                const allocator_type& allocator = allocator_type())
      : storage_(allocator) {
    const size_type s = static_cast<size_type>(std::distance(first, last));
    if (ABSL_PREDICT_FALSE(s > max_size())) {
      ThrowStdLengthError("InlinedVector::InlinedVector failed length check");
    }
    storage_.Initialize(IteratorValueAdapter<A, ForwardIterator>(first), s);
  }

  /// Creates an inlined vector with elements constructed from the provided input
  /// iterator range [`first`, `last`).
  ///
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  /// @param allocator The allocator to use.
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator> = 0>
  InlinedVector(InputIterator first, InputIterator last,
                const allocator_type& allocator = allocator_type())
      : storage_(allocator) {
    std::copy(first, last, std::back_inserter(*this));
  }

  /// Creates an inlined vector by copying the contents of `other` using
  /// `other`'s allocator.
  ///
  /// @param other The inlined vector to copy.
  InlinedVector(const InlinedVector& other)
      : InlinedVector(other, other.storage_.GetAllocator()) {}

  /// Creates an inlined vector by copying the contents of `other` using the
  /// provided `allocator`.
  ///
  /// @param other The inlined vector to copy.
  /// @param allocator The allocator to use.
  InlinedVector(const InlinedVector& other, const allocator_type& allocator)
      : storage_(allocator) {
    // Fast path: if the other vector is empty, there's nothing for us to do.
    if (other.empty()) {
      return;
    }

    // Fast path: if the value type is trivially copy constructible, we know the
    // allocator doesn't do anything fancy, and there is nothing on the heap
    // then we know it is legal for us to simply memcpy the other vector's
    // inlined bytes to form our copy of its elements.
    if (std::is_trivially_copy_constructible_v<value_type> &&
        std::is_same_v<A, std::allocator<value_type>> &&
        !other.storage_.GetIsAllocated()) {
      storage_.MemcpyFrom(other.storage_);
      return;
    }

    storage_.InitFrom(other.storage_);
  }

  /// Creates an inlined vector by moving in the contents of `other` without
  /// allocating. If `other` contains allocated memory, the newly-created inlined
  /// vector will take ownership of that memory. However, if `other` does not
  /// contain allocated memory, the newly-created inlined vector will perform
  /// element-wise move construction of the contents of `other`.
  ///
  /// NOTE: since no allocation is performed for the inlined vector in either
  /// case, the `noexcept(...)` specification depends on whether moving the
  /// underlying objects can throw. It is assumed assumed that...
  ///  a) move constructors should only throw due to allocation failure.
  ///  b) if `value_type`'s move constructor allocates, it uses the same
  ///     allocation function as the inlined vector's allocator.
  /// Thus, the move constructor is non-throwing if the allocator is non-throwing
  /// or `value_type`'s move constructor is specified as `noexcept`.
  ///
  /// @param other The inlined vector to move from.
  InlinedVector(InlinedVector&& other) noexcept(
      absl::allocator_is_nothrow<allocator_type>::value ||
      std::is_nothrow_move_constructible_v<value_type>)
      : storage_(other.storage_.GetAllocator()) {
    // Fast path: if the value type can be trivially relocated (i.e. moved from
    // and destroyed), and we know the allocator doesn't do anything fancy, then
    // it's safe for us to simply adopt the contents of the storage for `other`
    // and remove its own reference to them. It's as if we had individually
    // move-constructed each value and then destroyed the original.
    if (absl::is_trivially_relocatable<value_type>::value &&
        std::is_same_v<A, std::allocator<value_type>>) {
      storage_.MemcpyFrom(other.storage_);
      other.storage_.SetInlinedSize(0);
      return;
    }

    // Fast path: if the other vector is on the heap, we can simply take over
    // its allocation.
    if (other.storage_.GetIsAllocated()) {
      storage_.SetAllocation({other.storage_.GetAllocatedData(),
                              other.storage_.GetAllocatedCapacity()});
      storage_.SetAllocatedSize(other.storage_.GetSize());

      other.storage_.SetInlinedSize(0);
      return;
    }

    // Otherwise we must move each element individually.
    IteratorValueAdapter<A, MoveIterator<A>> other_values(
        MoveIterator<A>(other.storage_.GetInlinedData()));

    inlined_vector_internal::ConstructElements<A>(
        storage_.GetAllocator(), storage_.GetInlinedData(), other_values,
        other.storage_.GetSize());

    storage_.SetInlinedSize(other.storage_.GetSize());
  }

  /// Creates an inlined vector by moving in the contents of `other` with a copy
  /// of `allocator`.
  ///
  /// NOTE: if `other`'s allocator is not equal to `allocator`, even if `other`
  /// contains allocated memory, this move constructor will still allocate. Since
  /// allocation is performed, this constructor can only be `noexcept` if the
  /// specified allocator is also `noexcept`.
  ///
  /// @param other The inlined vector to move from.
  /// @param allocator The allocator to use.
  InlinedVector(
      InlinedVector&& other,
      const allocator_type&
          allocator) noexcept(absl::allocator_is_nothrow<allocator_type>::value)
      : storage_(allocator) {
    // Fast path: if the value type can be trivially relocated (i.e. moved from
    // and destroyed), and we know the allocator doesn't do anything fancy, then
    // it's safe for us to simply adopt the contents of the storage for `other`
    // and remove its own reference to them. It's as if we had individually
    // move-constructed each value and then destroyed the original.
    if (absl::is_trivially_relocatable<value_type>::value &&
        std::is_same_v<A, std::allocator<value_type>>) {
      storage_.MemcpyFrom(other.storage_);
      other.storage_.SetInlinedSize(0);
      return;
    }

    // Fast path: if the other vector is on the heap and shared the same
    // allocator, we can simply take over its allocation.
    if ((storage_.GetAllocator() == other.storage_.GetAllocator()) &&
        other.storage_.GetIsAllocated()) {
      storage_.SetAllocation({other.storage_.GetAllocatedData(),
                              other.storage_.GetAllocatedCapacity()});
      storage_.SetAllocatedSize(other.storage_.GetSize());

      other.storage_.SetInlinedSize(0);
      return;
    }

    // Otherwise we must move each element individually.
    storage_.Initialize(
        IteratorValueAdapter<A, MoveIterator<A>>(MoveIterator<A>(other.data())),
        other.size());
  }

  /// Destroys the inlined vector and its elements.
  ~InlinedVector() {}

  // ---------------------------------------------------------------------------
  // InlinedVector Member Accessors
  // ---------------------------------------------------------------------------

  /// Returns whether the inlined vector contains no elements.
  ///
  /// @return `true` if the inlined vector is empty, `false` otherwise.
  bool empty() const noexcept { return !size(); }

  /// Returns the number of elements in the inlined vector.
  ///
  /// @return The number of elements.
  size_type size() const noexcept { return storage_.GetSize(); }

  /// Returns the maximum number of elements the inlined vector can hold.
  ///
  /// @return The maximum number of elements.
  size_type max_size() const noexcept {
    // One bit of the size storage is used to indicate whether the inlined
    // vector contains allocated memory. As a result, the maximum size that the
    // inlined vector can express is the minimum of the limit of how many
    // objects we can allocate and std::numeric_limits<size_type>::max() / 2.
    return (std::min)(AllocatorTraits<A>::max_size(storage_.GetAllocator()),
                      (std::numeric_limits<size_type>::max)() / 2);
  }

  /// Returns the number of elements that could be stored in the inlined vector
  /// without requiring a reallocation.
  ///
  /// NOTE: for most inlined vectors, `capacity()` should be equal to the
  /// template parameter `N`. For inlined vectors which exceed this capacity,
  /// they will no longer be inlined and `capacity()` will equal the capactity of
  /// the allocated memory.
  ///
  /// @return The current capacity.
  size_type capacity() const noexcept {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedCapacity()
                                     : storage_.GetInlinedCapacity();
  }

  /// Returns a `pointer` to the elements of the inlined vector. This pointer
  /// can be used to access and modify the contained elements.
  ///
  /// NOTE: only elements within [`data()`, `data() + size()`) are valid.
  ///
  /// @return A `pointer` to the elements.
  pointer data() noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedData()
                                     : storage_.GetInlinedData();
  }

  /// Overload of `InlinedVector::data()` that returns a `const_pointer` to the
  /// elements of the inlined vector. This pointer can be used to access but not
  /// modify the contained elements.
  ///
  /// NOTE: only elements within [`data()`, `data() + size()`) are valid.
  ///
  /// @return A `const_pointer` to the elements.
  const_pointer data() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedData()
                                     : storage_.GetInlinedData();
  }

  /// Returns a `reference` to the `i`th element of the inlined vector.
  ///
  /// @param i The index of the element to access.
  /// @return A `reference` to the `i`th element.
  reference operator[](size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertLT(i, size());
    return data()[i];
  }

  /// Overload of `InlinedVector::operator[](...)` that returns a
  /// `const_reference` to the `i`th element of the inlined vector.
  ///
  /// @param i The index of the element to access.
  /// @return A `const_reference` to the `i`th element.
  const_reference operator[](size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertLT(i, size());
    return data()[i];
  }

  /// Returns a `reference` to the `i`th element of the inlined vector.
  ///
  /// NOTE: if `i` is not within the required range of `InlinedVector::at(...)`,
  /// in both debug and non-debug builds, `std::out_of_range` will be thrown.
  ///
  /// @param i The index of the element to access.
  /// @return A `reference` to the `i`th element.
  reference at(size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      ThrowStdOutOfRange("InlinedVector::at(size_type) failed bounds check");
    }
    return data()[i];
  }

  /// Overload of `InlinedVector::at(...)` that returns a `const_reference` to
  /// the `i`th element of the inlined vector.
  ///
  /// NOTE: if `i` is not within the required range of `InlinedVector::at(...)`,
  /// in both debug and non-debug builds, `std::out_of_range` will be thrown.
  ///
  /// @param i The index of the element to access.
  /// @return A `const_reference` to the `i`th element.
  const_reference at(size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      ThrowStdOutOfRange("InlinedVector::at(size_type) failed bounds check");
    }
    return data()[i];
  }

  /// Returns a `reference` to the first element of the inlined vector.
  ///
  /// @return A `reference` to the first element.
  reference front() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[0];
  }

  /// Overload of `InlinedVector::front()` that returns a `const_reference` to
  /// the first element of the inlined vector.
  ///
  /// @return A `const_reference` to the first element.
  const_reference front() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[0];
  }

  /// Returns a `reference` to the last element of the inlined vector.
  ///
  /// @return A `reference` to the last element.
  reference back() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[size() - 1];
  }

  /// Overload of `InlinedVector::back()` that returns a `const_reference` to the
  /// last element of the inlined vector.
  ///
  /// @return A `const_reference` to the last element.
  const_reference back() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[size() - 1];
  }

  /// Returns an `iterator` to the beginning of the inlined vector.
  ///
  /// @return An `iterator` to the first element.
  iterator begin() noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND { return data(); }

  /// Overload of `InlinedVector::begin()` that returns a `const_iterator` to
  /// the beginning of the inlined vector.
  ///
  /// @return A `const_iterator` to the first element.
  const_iterator begin() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return data();
  }

  /// Returns an `iterator` to the end of the inlined vector.
  ///
  /// @return An `iterator` past the last element.
  iterator end() noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return data() + size();
  }

  /// Overload of `InlinedVector::end()` that returns a `const_iterator` to the
  /// end of the inlined vector.
  ///
  /// @return A `const_iterator` past the last element.
  const_iterator end() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return data() + size();
  }

  /// Returns a `const_iterator` to the beginning of the inlined vector.
  ///
  /// @return A `const_iterator` to the first element.
  const_iterator cbegin() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return begin();
  }

  /// Returns a `const_iterator` to the end of the inlined vector.
  ///
  /// @return A `const_iterator` past the last element.
  const_iterator cend() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return end();
  }

  /// Returns a `reverse_iterator` from the end of the inlined vector.
  ///
  /// @return A `reverse_iterator` to the last element.
  reverse_iterator rbegin() noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(end());
  }

  /// Overload of `InlinedVector::rbegin()` that returns a
  /// `const_reverse_iterator` from the end of the inlined vector.
  ///
  /// @return A `const_reverse_iterator` to the last element.
  const_reverse_iterator rbegin() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(end());
  }

  /// Returns a `reverse_iterator` from the beginning of the inlined vector.
  ///
  /// @return A `reverse_iterator` past the first element.
  reverse_iterator rend() noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(begin());
  }

  /// Overload of `InlinedVector::rend()` that returns a `const_reverse_iterator`
  /// from the beginning of the inlined vector.
  ///
  /// @return A `const_reverse_iterator` past the first element.
  const_reverse_iterator rend() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(begin());
  }

  /// Returns a `const_reverse_iterator` from the end of the inlined vector.
  ///
  /// @return A `const_reverse_iterator` to the last element.
  const_reverse_iterator crbegin() const noexcept
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rbegin();
  }

  /// Returns a `const_reverse_iterator` from the beginning of the inlined
  /// vector.
  ///
  /// @return A `const_reverse_iterator` past the first element.
  const_reverse_iterator crend() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rend();
  }

  /// Returns a copy of the inlined vector's allocator.
  ///
  /// @return A copy of the allocator.
  allocator_type get_allocator() const { return storage_.GetAllocator(); }

  // ---------------------------------------------------------------------------
  // InlinedVector Member Mutators
  // ---------------------------------------------------------------------------

  /// Replaces the elements of the inlined vector with copies of the elements of
  /// `list`.
  ///
  /// @param list The initializer list to copy elements from.
  /// @return A reference to the inlined vector.
  InlinedVector& operator=(std::initializer_list<value_type> list) {
    assign(list.begin(), list.end());

    return *this;
  }

  /// Overload of `InlinedVector::operator=(...)` that replaces the elements of
  /// the inlined vector with copies of the elements of `other`.
  ///
  /// @param other The inlined vector to copy.
  /// @return A reference to the inlined vector.
  InlinedVector& operator=(const InlinedVector& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      const_pointer other_data = other.data();
      assign(other_data, other_data + other.size());
    }

    return *this;
  }

  /// Overload of `InlinedVector::operator=(...)` that moves the elements of
  /// `other` into the inlined vector.
  ///
  /// NOTE: as a result of calling this overload, `other` is left in a valid but
  /// unspecified state.
  ///
  /// @param other The inlined vector to move from.
  /// @return A reference to the inlined vector.
  InlinedVector& operator=(InlinedVector&& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      MoveAssignment(MoveAssignmentPolicy{}, std::move(other));
    }

    return *this;
  }

  /// Replaces the contents of the inlined vector with `n` copies of `v`.
  ///
  /// @param n The number of copies to assign.
  /// @param v The value to copy into each element.
  void assign(size_type n, const_reference v) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::assign failed length check");
    }
    storage_.Assign(CopyValueAdapter<A>(std::addressof(v)), n);
  }

  /// Overload of `InlinedVector::assign(...)` that replaces the contents of the
  /// inlined vector with copies of the elements of `list`.
  ///
  /// @param list The initializer list to copy elements from.
  void assign(std::initializer_list<value_type> list) {
    assign(list.begin(), list.end());
  }

  /// Overload of `InlinedVector::assign(...)` to replace the contents of the
  /// inlined vector with the range [`first`, `last`).
  ///
  /// NOTE: this overload is for iterators that are "forward" category or better.
  ///
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator> = 0>
  void assign(ForwardIterator first, ForwardIterator last) {
    const size_type s = static_cast<size_type>(std::distance(first, last));
    if (ABSL_PREDICT_FALSE(s > max_size())) {
      ThrowStdLengthError("InlinedVector::assign failed length check");
    }
    storage_.Assign(IteratorValueAdapter<A, ForwardIterator>(first), s);
  }

  /// Overload of `InlinedVector::assign(...)` to replace the contents of the
  /// inlined vector with the range [`first`, `last`).
  ///
  /// NOTE: this overload is for iterators that are "input" category.
  ///
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator> = 0>
  void assign(InputIterator first, InputIterator last) {
    size_type i = 0;
    for (; i < size() && first != last; ++i, static_cast<void>(++first)) {
      data()[i] = *first;
    }

    erase(data() + i, data() + size());
    std::copy(first, last, std::back_inserter(*this));
  }

  /// Resizes the inlined vector to contain `n` elements.
  ///
  /// NOTE: If `n` is smaller than `size()`, extra elements are destroyed. If `n`
  /// is larger than `size()`, new elements are value-initialized.
  ///
  /// @param n The new number of elements.
  void resize(size_type n) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::resize failed length check");
    }
    storage_.Resize(DefaultValueAdapter<A>(), n);
  }

  /// Overload of `InlinedVector::resize(...)` that resizes the inlined vector to
  /// contain `n` elements.
  ///
  /// NOTE: if `n` is smaller than `size()`, extra elements are destroyed. If `n`
  /// is larger than `size()`, new elements are copied-constructed from `v`.
  ///
  /// @param n The new number of elements.
  /// @param v The value to copy into any new elements.
  void resize(size_type n, const_reference v) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::resize failed length check");
    }
    storage_.Resize(CopyValueAdapter<A>(std::addressof(v)), n);
  }

  /// Inserts a copy of `v` at `pos`, returning an `iterator` to the newly
  /// inserted element.
  ///
  /// @param pos The position at which to insert.
  /// @param v The value to copy.
  /// @return An `iterator` to the newly inserted element.
  iterator insert(const_iterator pos,
                  const_reference v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(pos, v);
  }

  /// Overload of `InlinedVector::insert(...)` that inserts `v` at `pos` using
  /// move semantics, returning an `iterator` to the newly inserted element.
  ///
  /// @param pos The position at which to insert.
  /// @param v The value to move.
  /// @return An `iterator` to the newly inserted element.
  iterator insert(const_iterator pos,
                  value_type&& v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(pos, std::move(v));
  }

  /// Overload of `InlinedVector::insert(...)` that inserts `n` contiguous copies
  /// of `v` starting at `pos`, returning an `iterator` pointing to the first of
  /// the newly inserted elements.
  ///
  /// @param pos The position at which to insert.
  /// @param n The number of copies to insert.
  /// @param v The value to copy.
  /// @return An `iterator` to the first newly inserted element.
  iterator insert(const_iterator pos, size_type n,
                  const_reference v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(pos, cbegin());
    absl::base_internal::HardeningAssertLE(pos, cend());
    if (ABSL_PREDICT_FALSE(n > max_size() - size())) {
      ThrowStdLengthError("InlinedVector::insert failed length check");
    }

    if (ABSL_PREDICT_TRUE(n != 0)) {
      value_type dealias = v;
      // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102329#c2
      // It appears that GCC thinks that since `pos` is a const pointer and may
      // point to uninitialized memory at this point, a warning should be
      // issued. But `pos` is actually only used to compute an array index to
      // write to.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
      return storage_.Insert(pos, CopyValueAdapter<A>(std::addressof(dealias)),
                             n);
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    } else {
      return const_cast<iterator>(pos);
    }
  }

  /// Overload of `InlinedVector::insert(...)` that inserts copies of the
  /// elements of `list` starting at `pos`, returning an `iterator` pointing to
  /// the first of the newly inserted elements.
  ///
  /// @param pos The position at which to insert.
  /// @param list The initializer list to copy elements from.
  /// @return An `iterator` to the first newly inserted element.
  iterator insert(const_iterator pos, std::initializer_list<value_type> list)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert(pos, list.begin(), list.end());
  }

  /// Overload of `InlinedVector::insert(...)` that inserts the range [`first`,
  /// `last`) starting at `pos`, returning an `iterator` pointing to the first
  /// of the newly inserted elements.
  ///
  /// NOTE: this overload is for iterators that are "forward" category or better.
  ///
  /// @param pos The position at which to insert.
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  /// @return An `iterator` to the first newly inserted element.
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator> = 0>
  iterator insert(const_iterator pos, ForwardIterator first,
                  ForwardIterator last) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(pos, cbegin());
    absl::base_internal::HardeningAssertLE(pos, cend());
    const size_type s = static_cast<size_type>(std::distance(first, last));
    if (ABSL_PREDICT_FALSE(s > max_size() - size())) {
      ThrowStdLengthError("InlinedVector::insert failed length check");
    }

    if (ABSL_PREDICT_TRUE(first != last)) {
      return storage_.Insert(
          pos, IteratorValueAdapter<A, ForwardIterator>(first), s);
    } else {
      return const_cast<iterator>(pos);
    }
  }

  /// Overload of `InlinedVector::insert(...)` that inserts the range [`first`,
  /// `last`) starting at `pos`, returning an `iterator` pointing to the first
  /// of the newly inserted elements.
  ///
  /// NOTE: this overload is for iterators that are "input" category.
  ///
  /// @param pos The position at which to insert.
  /// @param first An iterator to the beginning of the range.
  /// @param last An iterator past the end of the range.
  /// @return An `iterator` to the first newly inserted element.
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator> = 0>
  iterator insert(const_iterator pos, InputIterator first,
                  InputIterator last) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(pos, cbegin());
    absl::base_internal::HardeningAssertLE(pos, cend());

    size_type index = static_cast<size_type>(std::distance(cbegin(), pos));
    for (size_type i = index; first != last; ++i, static_cast<void>(++first)) {
      insert(data() + i, *first);
    }

    return iterator(data() + index);
  }

  /// Constructs and inserts an element using `args...` in the inlined vector at
  /// `pos`, returning an `iterator` pointing to the newly emplaced element.
  ///
  /// @param pos The position at which to emplace.
  /// @param args The arguments used to construct the element.
  /// @return An `iterator` to the newly emplaced element.
  template <typename... Args>
  iterator emplace(const_iterator pos,
                   Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(pos, cbegin());
    absl::base_internal::HardeningAssertLE(pos, cend());
    if (ABSL_PREDICT_FALSE(size() == max_size())) {
      ThrowStdLengthError("InlinedVector::emplace failed length check");
    }

    value_type dealias(std::forward<Args>(args)...);
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102329#c2
    // It appears that GCC thinks that since `pos` is a const pointer and may
    // point to uninitialized memory at this point, a warning should be
    // issued. But `pos` is actually only used to compute an array index to
    // write to.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return storage_.Insert(pos,
                           IteratorValueAdapter<A, MoveIterator<A>>(
                               MoveIterator<A>(std::addressof(dealias))),
                           1);
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  }

  /// Constructs and inserts an element using `args...` in the inlined vector at
  /// `end()`, returning a `reference` to the newly emplaced element.
  ///
  /// @param args The arguments used to construct the element.
  /// @return A `reference` to the newly emplaced element.
  template <typename... Args>
  reference emplace_back(Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(size() == max_size())) {
      ThrowStdLengthError("InlinedVector::emplace_back failed length check");
    }
    return storage_.EmplaceBack(std::forward<Args>(args)...);
  }

  /// Inserts a copy of `v` in the inlined vector at `end()`.
  ///
  /// @param v The value to copy.
  void push_back(const_reference v) { static_cast<void>(emplace_back(v)); }

  /// Overload of `InlinedVector::push_back(...)` for inserting `v` at `end()`
  /// using move semantics.
  ///
  /// @param v The value to move.
  void push_back(value_type&& v) {
    static_cast<void>(emplace_back(std::move(v)));
  }

  /// Destroys the element at `back()`, reducing the size by `1`.
  void pop_back() noexcept {
    absl::base_internal::HardeningAssertNonEmpty(*this);

    AllocatorTraits<A>::destroy(storage_.GetAllocator(), data() + (size() - 1));
    storage_.SubtractSize(1);
  }

  /// Erases the element at `pos`, returning an `iterator` pointing to where the
  /// erased element was located.
  ///
  /// NOTE: may return `end()`, which is not dereferenceable.
  ///
  /// @param pos The position of the element to erase.
  /// @return An `iterator` to the element that followed the erased element.
  iterator erase(const_iterator pos) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(pos, cbegin());
    absl::base_internal::HardeningAssertLT(pos, cend());

    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102329#c2
    // It appears that GCC thinks that since `pos` is a const pointer and may
    // point to uninitialized memory at this point, a warning should be
    // issued. But `pos` is actually only used to compute an array index to
    // write to.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
    return storage_.Erase(pos, pos + 1);
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  }

  /// Overload of `InlinedVector::erase(...)` that erases every element in the
  /// range [`from`, `to`), returning an `iterator` pointing to where the first
  /// erased element was located.
  ///
  /// NOTE: may return `end()`, which is not dereferenceable.
  ///
  /// @param from An iterator to the beginning of the range to erase.
  /// @param to An iterator past the end of the range to erase.
  /// @return An `iterator` to the element that followed the erased range.
  iterator erase(const_iterator from,
                 const_iterator to) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertGE(from, cbegin());
    absl::base_internal::HardeningAssertLE(from, to);
    absl::base_internal::HardeningAssertLE(to, cend());

    if (ABSL_PREDICT_TRUE(from != to)) {
      return storage_.Erase(from, to);
    } else {
      return const_cast<iterator>(from);
    }
  }

  /// Destroys all elements in the inlined vector, setting the size to `0` and
  /// preserving capacity.
  void clear() noexcept {
    inlined_vector_internal::DestroyAdapter<A>::DestroyElements(
        storage_.GetAllocator(), data(), size());
    storage_.SetSize(0);
  }

  /// Ensures that there is enough room for at least `n` elements.
  ///
  /// @param n The minimum capacity to reserve.
  void reserve(size_type n) {
    if (ABSL_PREDICT_FALSE(n > max_size())) {
      ThrowStdLengthError("InlinedVector::reserve failed length check");
    }
    storage_.Reserve(n);
  }

  /// Attempts to reduce memory usage by moving elements to (or keeping elements
  /// in) the smallest available buffer sufficient for containing `size()`
  /// elements.
  ///
  /// If `size()` is sufficiently small, the elements will be moved into (or kept
  /// in) the inlined space.
  void shrink_to_fit() {
    if (storage_.GetIsAllocated()) {
      storage_.ShrinkToFit();
    }
  }

  /// Swaps the contents of the inlined vector with `other`.
  ///
  /// @param other The inlined vector to swap with.
  void swap(InlinedVector& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      storage_.Swap(std::addressof(other.storage_));
    }
  }

 private:
  /// Provides `absl::Hash` support for `absl::InlinedVector`.
  ///
  /// @param h The hash state to combine with.
  /// @param a The inlined vector to hash.
  /// @return The combined hash state.
  template <typename H, typename TheT, size_t TheN, typename TheA>
  friend H AbslHashValue(H h, const absl::InlinedVector<TheT, TheN, TheA>& a);

  void MoveAssignment(MemcpyPolicy, InlinedVector&& other) {
    // Assumption check: we shouldn't be told to use memcpy to implement move
    // assignment unless we have trivially destructible elements and an
    // allocator that does nothing fancy.
    static_assert(std::is_trivially_destructible_v<value_type>);
    static_assert(std::is_same_v<A, std::allocator<value_type>>);

    // Throw away our existing heap allocation, if any. There is no need to
    // destroy the existing elements one by one because we know they are
    // trivially destructible.
    storage_.DeallocateIfAllocated();

    // Adopt the other vector's inline elements or heap allocation.
    storage_.MemcpyFrom(other.storage_);
    other.storage_.SetInlinedSize(0);
  }

  // Destroy our existing elements, if any, and adopt the heap-allocated
  // elements of the other vector.
  //
  // REQUIRES: other.storage_.GetIsAllocated()
  void DestroyExistingAndAdopt(InlinedVector&& other) {
    absl::base_internal::HardeningAssert(other.storage_.GetIsAllocated());

    inlined_vector_internal::DestroyAdapter<A>::DestroyElements(
        storage_.GetAllocator(), data(), size());
    storage_.DeallocateIfAllocated();

    storage_.MemcpyFrom(other.storage_);
    other.storage_.SetInlinedSize(0);
  }

  void MoveAssignment(ElementwiseAssignPolicy, InlinedVector&& other) {
    // Fast path: if the other vector is on the heap then we don't worry about
    // actually move-assigning each element. Instead we only throw away our own
    // existing elements and adopt the heap allocation of the other vector.
    if (other.storage_.GetIsAllocated()) {
      DestroyExistingAndAdopt(std::move(other));
      return;
    }

    storage_.Assign(IteratorValueAdapter<A, MoveIterator<A>>(
                        MoveIterator<A>(other.storage_.GetInlinedData())),
                    other.size());
  }

  void MoveAssignment(ElementwiseConstructPolicy, InlinedVector&& other) {
    // Fast path: if the other vector is on the heap then we don't worry about
    // actually move-assigning each element. Instead we only throw away our own
    // existing elements and adopt the heap allocation of the other vector.
    if (other.storage_.GetIsAllocated()) {
      DestroyExistingAndAdopt(std::move(other));
      return;
    }

    inlined_vector_internal::DestroyAdapter<A>::DestroyElements(
        storage_.GetAllocator(), data(), size());
    storage_.DeallocateIfAllocated();

    if constexpr (!std::is_nothrow_move_constructible_v<value_type>) {
      // Reset the size to zero before moving to avoid leaking freed memory if
      // an exception is thrown.
      storage_.SetInlinedSize(0);
    }

    IteratorValueAdapter<A, MoveIterator<A>> other_values(
        MoveIterator<A>(other.storage_.GetInlinedData()));
    inlined_vector_internal::ConstructElements<A>(
        storage_.GetAllocator(), storage_.GetInlinedData(), other_values,
        other.storage_.GetSize());
    storage_.SetInlinedSize(other.storage_.GetSize());
  }

  Storage storage_;
};

// -----------------------------------------------------------------------------
// InlinedVector Non-Member Functions
// -----------------------------------------------------------------------------

/// Swaps the contents of two inlined vectors.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
template <typename T, size_t N, typename A>
void swap(absl::InlinedVector<T, N, A>& a,
          absl::InlinedVector<T, N, A>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

/// Tests for value-equality of two inlined vectors.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if the vectors are equal, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator==(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  auto a_data = a.data();
  auto b_data = b.data();
  return std::equal(a_data, a_data + a.size(), b_data, b_data + b.size());
}

/// Tests for value-inequality of two inlined vectors.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if the vectors are not equal, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator!=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(a == b);
}

/// Tests whether the value of an inlined vector is less than the value of
/// another inlined vector using a lexicographical comparison algorithm.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if `a` is less than `b`, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator<(const absl::InlinedVector<T, N, A>& a,
               const absl::InlinedVector<T, N, A>& b) {
  auto a_data = a.data();
  auto b_data = b.data();
  return std::lexicographical_compare(a_data, a_data + a.size(), b_data,
                                      b_data + b.size());
}

/// Tests whether the value of an inlined vector is greater than the value of
/// another inlined vector using a lexicographical comparison algorithm.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if `a` is greater than `b`, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator>(const absl::InlinedVector<T, N, A>& a,
               const absl::InlinedVector<T, N, A>& b) {
  return b < a;
}

/// Tests whether the value of an inlined vector is less than or equal to the
/// value of another inlined vector using a lexicographical comparison algorithm.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if `a` is less than or equal to `b`, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator<=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(b < a);
}

/// Tests whether the value of an inlined vector is greater than or equal to the
/// value of another inlined vector using a lexicographical comparison algorithm.
///
/// @param a The first inlined vector.
/// @param b The second inlined vector.
/// @return `true` if `a` is greater than or equal to `b`, `false` otherwise.
template <typename T, size_t N, typename A>
bool operator>=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(a < b);
}

/// Provides `absl::Hash` support for `absl::InlinedVector`. It is uncommon to
/// call this directly.
///
/// @param h The hash state to combine with.
/// @param a The inlined vector to hash.
/// @return The combined hash state.
template <typename H, typename T, size_t N, typename A>
H AbslHashValue(H h, const absl::InlinedVector<T, N, A>& a) {
  return H::combine_contiguous(std::move(h), a.data(), a.size());
}

/// Erases all elements that satisfy the predicate `pred` from the inlined
/// vector `v`.
///
/// @param v The inlined vector to erase elements from.
/// @param pred The predicate that selects elements to erase.
/// @return The number of erased elements.
template <typename T, size_t N, typename A, typename Predicate>
constexpr typename InlinedVector<T, N, A>::size_type erase_if(
    InlinedVector<T, N, A>& v, Predicate pred) {
  const auto it = std::remove_if(v.begin(), v.end(), std::move(pred));
  const auto removed = static_cast<typename InlinedVector<T, N, A>::size_type>(
      std::distance(it, v.end()));
  v.erase(it, v.end());
  return removed;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INLINED_VECTOR_H_
