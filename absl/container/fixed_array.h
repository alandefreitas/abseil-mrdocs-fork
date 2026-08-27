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
// File: fixed_array.h
// -----------------------------------------------------------------------------
//
// A `FixedArray<T>` represents a non-resizable array of `T` where the length of
// the array can be determined at run-time. It is a good replacement for
// non-standard and deprecated uses of `alloca()` and variable length arrays
// within the GCC extension. (See
// https://gcc.gnu.org/onlinedocs/gcc/Variable-Length.html).
//
// `FixedArray` allocates small arrays inline, keeping performance fast by
// avoiding heap operations. It also helps reduce the chances of
// accidentally overflowing your stack if large input is passed to
// your function.

#ifndef ABSL_CONTAINER_FIXED_ARRAY_H_
#define ABSL_CONTAINER_FIXED_ARRAY_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include "absl/algorithm/algorithm.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/internal/iterator_traits.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/base/throw_delegate.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/hash/internal/weakly_mixed_integer.h"
#include "absl/memory/memory.h"

// Abseil container library.
namespace absl {
ABSL_NAMESPACE_BEGIN

constexpr static auto kFixedArrayUseDefault = static_cast<size_t>(-1);

/// A run-time fixed-size array that allocates small arrays inline.
///
/// A `FixedArray` provides a run-time fixed-size array, allocating a small array
/// inline for efficiency.
///
/// Most users should not specify the `N` template parameter and let `FixedArray`
/// automatically determine the number of elements to store inline based on
/// `sizeof(T)`. If `N` is specified, the `FixedArray` implementation will use
/// inline storage for arrays with a length <= `N`.
///
/// Note that a `FixedArray` constructed with a `size_type` argument will
/// default-initialize its values by leaving trivially constructible types
/// uninitialized (e.g. int, int[4], double), and others default-constructed.
/// This matches the behavior of c-style arrays and `std::array`, but not
/// `std::vector`.
template <typename T, size_t N = kFixedArrayUseDefault,
          typename A = std::allocator<T>>
class ABSL_ATTRIBUTE_WARN_UNUSED FixedArray {
  static_assert(!std::is_array_v<T> || std::extent_v<T> > 0,
                "Arrays with unknown bounds cannot be used with FixedArray.");

  static constexpr size_t kInlineBytesDefault = 256;

  using AllocatorTraits = std::allocator_traits<A>;
  template <typename Iterator>
  using EnableIfInputIterator =
      std::enable_if_t<base_internal::IsAtLeastInputIterator<Iterator>::value>;
  static constexpr bool NoexceptCopyable() {
    return std::is_nothrow_copy_constructible_v<StorageElement> &&
           absl::allocator_is_nothrow<allocator_type>::value;
  }
  static constexpr bool NoexceptMovable() {
    return std::is_nothrow_move_constructible_v<StorageElement> &&
           absl::allocator_is_nothrow<allocator_type>::value;
  }
  static constexpr bool DefaultConstructorIsNonTrivial() {
    return !std::is_trivially_default_constructible_v<StorageElement>;
  }

 public:
  /// The allocator type used to allocate and construct elements.
  using allocator_type = typename AllocatorTraits::allocator_type;
  /// The type of the elements stored in the fixed array.
  using value_type = typename AllocatorTraits::value_type;
  /// A pointer to an element.
  using pointer = typename AllocatorTraits::pointer;
  /// A pointer to a const element.
  using const_pointer = typename AllocatorTraits::const_pointer;
  /// A reference to an element.
  using reference = value_type&;
  /// A reference to a const element.
  using const_reference = const value_type&;
  /// An unsigned integral type used for sizes and indices.
  using size_type = typename AllocatorTraits::size_type;
  /// A signed integral type used for iterator differences.
  using difference_type = typename AllocatorTraits::difference_type;
  /// A random-access iterator over the elements.
  using iterator = pointer;
  /// A random-access iterator over const elements.
  using const_iterator = const_pointer;
  /// A reverse iterator over the elements.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// A reverse iterator over const elements.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /// The number of elements stored inline before heap allocation is used.
  static constexpr size_type inline_elements =
      (N == kFixedArrayUseDefault ? kInlineBytesDefault / sizeof(value_type)
                                  : static_cast<size_type>(N));

  /// Creates a copy of `other`, copying the copy-constructed allocator.
  ///
  /// @param other The fixed array to copy from.
  FixedArray(const FixedArray& other) noexcept(NoexceptCopyable())
      : FixedArray(other,
                   AllocatorTraits::select_on_container_copy_construction(
                       other.storage_.alloc())) {}

  /// Creates a copy of `other` using the given allocator.
  ///
  /// @param other The fixed array to copy from.
  /// @param a The allocator to use.
  FixedArray(const FixedArray& other,
             const allocator_type& a) noexcept(NoexceptCopyable())
      : FixedArray(other.begin(), other.end(), a) {}

  /// Moves the contents of `other` into a new fixed array.
  ///
  /// @param other The fixed array to move from.
  FixedArray(FixedArray&& other) noexcept(NoexceptMovable())
      : FixedArray(std::move(other), other.storage_.alloc()) {}

  /// Moves the contents of `other` into a new fixed array using the given
  /// allocator.
  ///
  /// @param other The fixed array to move from.
  /// @param a The allocator to use.
  FixedArray(FixedArray&& other,
             const allocator_type& a) noexcept(NoexceptMovable())
      : FixedArray(std::make_move_iterator(other.begin()),
                   std::make_move_iterator(other.end()), a) {}

  /// Creates an array object that can store `n` elements.
  ///
  /// Note that trivially constructible elements will be uninitialized.
  ///
  /// @param n The number of elements to store.
  /// @param a The allocator to use.
  explicit FixedArray(size_type n, const allocator_type& a = allocator_type())
      : storage_(n, a) {
    if (DefaultConstructorIsNonTrivial()) {
      memory_internal::ConstructRange(storage_.alloc(), storage_.begin(),
                                      storage_.end());
    }
  }

  /// Creates an array initialized with `n` copies of `val`.
  ///
  /// @param n The number of elements to store.
  /// @param val The value to copy into every element.
  /// @param a The allocator to use.
  FixedArray(size_type n, const value_type& val,
             const allocator_type& a = allocator_type())
      : storage_(n, a) {
    memory_internal::ConstructRange(storage_.alloc(), storage_.begin(),
                                    storage_.end(), val);
  }

  /// Creates an array initialized with the size and contents of `init_list`.
  ///
  /// @param init_list The initializer list of elements to copy.
  /// @param a The allocator to use.
  FixedArray(std::initializer_list<value_type> init_list,
             const allocator_type& a = allocator_type())
      : FixedArray(init_list.begin(), init_list.end(), a) {}

  /// Creates an array initialized with the elements from the input range.
  ///
  /// The array's size will always be `std::distance(first, last)`.
  /// REQUIRES: Iterator must be an input_iterator or better.
  ///
  /// @param first Iterator to the first element to copy.
  /// @param last Iterator past the last element to copy.
  /// @param a The allocator to use.
  template <typename Iterator, EnableIfInputIterator<Iterator>* = nullptr>
  FixedArray(Iterator first, Iterator last,
             const allocator_type& a = allocator_type())
      : storage_(std::distance(first, last), a) {
    memory_internal::CopyRange(storage_.alloc(), storage_.begin(), first, last);
  }

  /// Destroys the fixed array and its elements.
  ~FixedArray() noexcept {
    for (auto* cur = storage_.begin(); cur != storage_.end(); ++cur) {
      AllocatorTraits::destroy(storage_.alloc(), cur);
    }
  }

  /// Deleted move assignment, since a `FixedArray`'s size never changes.
  void operator=(FixedArray&& other) = delete;
  /// Deleted copy assignment, since a `FixedArray`'s size never changes.
  void operator=(const FixedArray& other) = delete;

  /// Returns the length of the fixed array.
  ///
  /// @return The number of elements in the fixed array.
  size_type size() const { return storage_.size(); }

  /// Returns the largest possible value of `std::distance(begin(), end())` for a
  /// `FixedArray<T>`. This is equivalent to the most possible addressable bytes
  /// over the number of bytes taken by T.
  ///
  /// @return The maximum number of elements the fixed array can address.
  constexpr size_type max_size() const {
    return (std::numeric_limits<difference_type>::max)() / sizeof(value_type);
  }

  /// Returns whether or not the fixed array is empty.
  ///
  /// @return `true` if the fixed array contains no elements.
  bool empty() const { return size() == 0; }

  /// Returns the memory size of the fixed array in bytes.
  ///
  /// @return The number of bytes occupied by the elements.
  size_t memsize() const { return size() * sizeof(value_type); }

  /// Returns a const T* pointer to elements of the `FixedArray`. This pointer
  /// can be used to access (but not modify) the contained elements.
  ///
  /// @return A const pointer to the first element.
  const_pointer data() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return AsValueType(storage_.begin());
  }

  /// Returns a T* pointer to elements of the fixed array. This pointer can be
  /// used to access and modify the contained elements.
  ///
  /// @return A pointer to the first element.
  pointer data() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return AsValueType(storage_.begin());
  }

  /// Returns a reference to the ith element of the fixed array.
  ///
  /// REQUIRES: 0 <= i < size()
  ///
  /// @param i The index of the element to access.
  /// @return A reference to the ith element.
  reference operator[](size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertLT(i, size());
    return data()[i];
  }

  /// Returns a const reference to the ith element of the fixed array.
  ///
  /// REQUIRES: 0 <= i < size()
  ///
  /// @param i The index of the element to access.
  /// @return A const reference to the ith element.
  const_reference operator[](size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertLT(i, size());
    return data()[i];
  }

  /// Bounds-checked access. Returns a reference to the ith element of the fixed
  /// array, or throws std::out_of_range.
  ///
  /// @param i The index of the element to access.
  /// @return A reference to the ith element.
  reference at(size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  /// Bounds-checked access. Returns a const reference to the ith element of the
  /// fixed array, or throws std::out_of_range.
  ///
  /// @param i The index of the element to access.
  /// @return A const reference to the ith element.
  const_reference at(size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  /// Returns a reference to the first element of the fixed array.
  ///
  /// @return A reference to the first element.
  reference front() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[0];
  }

  /// Returns a const reference to the first element of the fixed array.
  ///
  /// @return A const reference to the first element.
  const_reference front() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[0];
  }

  /// Returns a reference to the last element of the fixed array.
  ///
  /// @return A reference to the last element.
  reference back() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[size() - 1];
  }

  /// Returns a const reference to the last element of the fixed array.
  ///
  /// @return A const reference to the last element.
  const_reference back() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    absl::base_internal::HardeningAssertNonEmpty(*this);
    return data()[size() - 1];
  }

  /// Returns an iterator to the beginning of the fixed array.
  ///
  /// @return An iterator to the first element.
  iterator begin() ABSL_ATTRIBUTE_LIFETIME_BOUND { return data(); }

  /// Returns a const iterator to the beginning of the fixed array.
  ///
  /// @return A const iterator to the first element.
  const_iterator begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return data(); }

  /// Returns a const iterator to the beginning of the fixed array.
  ///
  /// @return A const iterator to the first element.
  const_iterator cbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return begin();
  }

  /// Returns an iterator to the end of the fixed array.
  ///
  /// @return An iterator past the last element.
  iterator end() ABSL_ATTRIBUTE_LIFETIME_BOUND { return data() + size(); }

  /// Returns a const iterator to the end of the fixed array.
  ///
  /// @return A const iterator past the last element.
  const_iterator end() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return data() + size();
  }

  /// Returns a const iterator to the end of the fixed array.
  ///
  /// @return A const iterator past the last element.
  const_iterator cend() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return end(); }

  /// Returns a reverse iterator from the end of the fixed array.
  ///
  /// @return A reverse iterator to the last element.
  reverse_iterator rbegin() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(end());
  }

  /// Returns a const reverse iterator from the end of the fixed array.
  ///
  /// @return A const reverse iterator to the last element.
  const_reverse_iterator rbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(end());
  }

  /// Returns a const reverse iterator from the end of the fixed array.
  ///
  /// @return A const reverse iterator to the last element.
  const_reverse_iterator crbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rbegin();
  }

  /// Returns a reverse iterator from the beginning of the fixed array.
  ///
  /// @return A reverse iterator past the first element.
  reverse_iterator rend() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(begin());
  }

  /// Returns a const reverse iterator from the beginning of the fixed array.
  ///
  /// @return A const reverse iterator past the first element.
  const_reverse_iterator rend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(begin());
  }

  /// Returns a const reverse iterator from the beginning of the fixed array.
  ///
  /// @return A const reverse iterator past the first element.
  const_reverse_iterator crend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rend();
  }

  /// Assigns the given `value` to all elements in the fixed array.
  ///
  /// @param val The value to assign to every element.
  void fill(const value_type& val) { std::fill(begin(), end(), val); }

  /// Returns whether two fixed arrays are elementwise equal.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if the arrays have equal size and equal elements.
  friend bool operator==(const FixedArray& lhs, const FixedArray& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  /// Returns whether two fixed arrays are not elementwise equal.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if the arrays differ in size or elements.
  friend bool operator!=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs == rhs);
  }

  /// Returns whether `lhs` orders lexicographically before `rhs`.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if `lhs` is lexicographically less than `rhs`.
  friend bool operator<(const FixedArray& lhs, const FixedArray& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                        rhs.end());
  }

  /// Returns whether `lhs` orders lexicographically after `rhs`.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if `lhs` is lexicographically greater than `rhs`.
  friend bool operator>(const FixedArray& lhs, const FixedArray& rhs) {
    return rhs < lhs;
  }

  /// Returns whether `lhs` orders lexicographically before or equal to `rhs`.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if `lhs` is not lexicographically greater than `rhs`.
  friend bool operator<=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(rhs < lhs);
  }

  /// Returns whether `lhs` orders lexicographically after or equal to `rhs`.
  ///
  /// @param lhs The left-hand fixed array.
  /// @param rhs The right-hand fixed array.
  /// @return `true` if `lhs` is not lexicographically less than `rhs`.
  friend bool operator>=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs < rhs);
  }

  /// Combines the contents of the fixed array into a hash state.
  ///
  /// @param h The hash state to combine into.
  /// @param v The fixed array to hash.
  /// @return The updated hash state.
  template <typename H>
  friend H AbslHashValue(H h, const FixedArray& v) {
    return H::combine_contiguous(std::move(h), v.data(), v.size());
  }

 private:
  // StorageElement
  //
  // For FixedArrays with a C-style-array value_type, StorageElement is a POD
  // wrapper struct called StorageElementWrapper that holds the value_type
  // instance inside. This is needed for construction and destruction of the
  // entire array regardless of how many dimensions it has. For all other cases,
  // StorageElement is just an alias of value_type.
  //
  // Maintainer's Note: The simpler solution would be to simply wrap value_type
  // in a struct whether it's an array or not. That causes some paranoid
  // diagnostics to misfire, believing that 'data()' returns a pointer to a
  // single element, rather than the packed array that it really is.
  // e.g.:
  //
  //     FixedArray<char> buf(1);
  //     sprintf(buf.data(), "foo");
  //
  //     error: call to int __builtin___sprintf_chk(etc...)
  //     will always overflow destination buffer [-Werror]
  //
  template <typename OuterT, typename InnerT = std::remove_extent_t<OuterT>,
            size_t InnerN = std::extent_v<OuterT>>
  struct StorageElementWrapper {
    InnerT array[InnerN];
  };

  using StorageElement =
      std::conditional_t<std::is_array_v<value_type>,
                         StorageElementWrapper<value_type>, value_type>;

  static pointer AsValueType(pointer ptr) { return ptr; }
  static pointer AsValueType(StorageElementWrapper<value_type>* ptr) {
    return std::addressof(ptr->array);
  }

  static_assert(sizeof(StorageElement) == sizeof(value_type));
  static_assert(alignof(StorageElement) == alignof(value_type));

  class NonEmptyInlinedStorage {
   public:
    StorageElement* data() { return reinterpret_cast<StorageElement*>(buff_); }
    void AnnotateConstruct(size_type n);
    void AnnotateDestruct(size_type n);

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
    void* RedzoneBegin() { return &redzone_begin_; }
    void* RedzoneEnd() { return &redzone_end_ + 1; }
#endif  // ABSL_HAVE_ADDRESS_SANITIZER

   private:
    ABSL_ADDRESS_SANITIZER_REDZONE(redzone_begin_);
    alignas(StorageElement) unsigned char buff_[sizeof(
        StorageElement[inline_elements])];
    ABSL_ADDRESS_SANITIZER_REDZONE(redzone_end_);
  };

  class EmptyInlinedStorage {
   public:
    StorageElement* data() { return nullptr; }
    void AnnotateConstruct(size_type) {}
    void AnnotateDestruct(size_type) {}
  };

  using InlinedStorage =
      std::conditional_t<inline_elements == 0, EmptyInlinedStorage,
                          NonEmptyInlinedStorage>;

  // Storage
  //
  // An instance of Storage manages the inline and out-of-line memory for
  // instances of FixedArray. This guarantees that even when construction of
  // individual elements fails in the FixedArray constructor body, the
  // destructor for Storage will still be called and out-of-line memory will be
  // properly deallocated.
  //
  class Storage : public InlinedStorage {
   public:
    Storage(size_type n, const allocator_type& a)
        : size_alloc_(n, a), data_(InitializeData()) {}

    ~Storage() noexcept {
      if (UsingInlinedStorage(size())) {
        InlinedStorage::AnnotateDestruct(size());
      } else {
        AllocatorTraits::deallocate(alloc(), AsValueType(begin()), size());
      }
    }

    size_type size() const { return size_alloc_.template get<0>(); }
    StorageElement* begin() const { return data_; }
    StorageElement* end() const { return begin() + size(); }
    allocator_type& alloc() { return size_alloc_.template get<1>(); }
    const allocator_type& alloc() const {
      return size_alloc_.template get<1>();
    }

   private:
    static bool UsingInlinedStorage(size_type n) {
      return n <= inline_elements;
    }

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
    ABSL_ATTRIBUTE_NOINLINE
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
    StorageElement* InitializeData() {
      if (UsingInlinedStorage(size())) {
        InlinedStorage::AnnotateConstruct(size());
        return InlinedStorage::data();
      } else {
        return reinterpret_cast<StorageElement*>(
            AllocatorTraits::allocate(alloc(), size()));
      }
    }

    // `CompressedTuple` takes advantage of EBCO for stateless `allocator_type`s
    container_internal::CompressedTuple<size_type, allocator_type> size_alloc_;
    StorageElement* data_;
  };

  Storage storage_;
};

/// Annotates the inline storage redzones after constructing `n` elements.
///
/// @param n The number of constructed elements.
template <typename T, size_t N, typename A>
void FixedArray<T, N, A>::NonEmptyInlinedStorage::AnnotateConstruct(
    typename FixedArray<T, N, A>::size_type n) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  if (!n) return;
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), RedzoneEnd(),
                                     data() + n);
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), data(),
                                     RedzoneBegin());
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}

/// Annotates the inline storage redzones before destructing `n` elements.
///
/// @param n The number of elements about to be destructed.
template <typename T, size_t N, typename A>
void FixedArray<T, N, A>::NonEmptyInlinedStorage::AnnotateDestruct(
    typename FixedArray<T, N, A>::size_type n) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  if (!n) return;
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), data() + n,
                                     RedzoneEnd());
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), RedzoneBegin(),
                                     data());
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_FIXED_ARRAY_H_
