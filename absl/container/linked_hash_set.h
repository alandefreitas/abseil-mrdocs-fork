// Copyright 2025 The Abseil Authors.
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
// File: linked_hash_set.h
// -----------------------------------------------------------------------------
//
// This is a simple insertion-ordered set. It provides O(1) amortized
// insertions and lookups, as well as iteration over the set in the insertion
// order.
//
// This class is thread-compatible.
// This class is NOT exception-safe.
//
// Iterators point into the list and should be stable in the face of
// mutations, except for an iterator pointing to an element that was just
// deleted.
//
// This class supports heterogeneous lookups.

#ifndef ABSL_CONTAINER_LINKED_HASH_SET_H_
#define ABSL_CONTAINER_LINKED_HASH_SET_H_

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/internal/common.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// A simple insertion-ordered set.
///
/// Provides O(1) amortized insertions and lookups, as well as iteration over
/// the set in the insertion order. This class is thread-compatible but NOT
/// exception-safe, and it supports heterogeneous lookups.
template <
    typename Key, typename KeyHash = typename absl::flat_hash_set<Key>::hasher,
    typename KeyEq = typename absl::flat_hash_set<Key, KeyHash>::key_equal,
    typename Alloc = std::allocator<Key>>
class linked_hash_set {
  using KeyArgImpl = absl::container_internal::KeyArg<
      absl::container_internal::IsTransparent<KeyEq>::value &&
      absl::container_internal::IsTransparent<KeyHash>::value>;

 public:
  /// The key type of the set.
  using key_type = Key;
  /// The hash functor type.
  using hasher = KeyHash;
  /// The key equality functor type.
  using key_equal = KeyEq;
  /// The value type stored in the set.
  using value_type = key_type;
  /// The allocator type.
  using allocator_type = Alloc;
  /// The signed integer difference type.
  using difference_type = ptrdiff_t;

 private:
  template <class K>
  using key_arg = typename KeyArgImpl::template type<K, key_type>;

  using ListType = std::list<key_type, Alloc>;

  template <class Fn>
  class Wrapped {
    template <typename K>
    static const K& ToKey(const K& k) {
      return k;
    }
    static const key_type& ToKey(typename ListType::const_iterator it) {
      return *it;
    }
    static const key_type& ToKey(typename ListType::iterator it) { return *it; }

    Fn fn_;

    friend linked_hash_set;

   public:
    using is_transparent = void;

    Wrapped() = default;
    explicit Wrapped(Fn fn) : fn_(std::move(fn)) {}

    template <class... Args>
    auto operator()(Args&&... args) const
        -> decltype(this->fn_(ToKey(args)...)) {
      return fn_(ToKey(args)...);
    }
  };
  using SetType =
      absl::flat_hash_set<typename ListType::iterator, Wrapped<hasher>,
                          Wrapped<key_equal>, Alloc>;

  class NodeHandle {
   public:
    using allocator_type = linked_hash_set::allocator_type;
    using value_type = linked_hash_set::value_type;

    constexpr NodeHandle() noexcept = default;
    NodeHandle(NodeHandle&& nh) noexcept = default;
    ~NodeHandle() = default;
    NodeHandle& operator=(NodeHandle&& node) noexcept = default;
    bool empty() const noexcept { return list_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }
    allocator_type get_allocator() const { return list_.get_allocator(); }
    value_type& value() { return list_.front(); }
    void swap(NodeHandle& nh) noexcept { list_.swap(nh.list_); }

   private:
    friend linked_hash_set;

    explicit NodeHandle(ListType list) : list_(std::move(list)) {}
    ListType list_;
  };

  template <class Iterator, class NodeType>
  struct InsertReturnType {
    Iterator position;
    bool inserted;
    NodeType node;
  };

 public:
  /// The iterator type, iterating in insertion order.
  using iterator = typename ListType::const_iterator;
  /// The const iterator type, iterating in insertion order.
  using const_iterator = typename ListType::const_iterator;
  /// The reverse iterator type.
  using reverse_iterator = typename ListType::const_reverse_iterator;
  /// The const reverse iterator type.
  using const_reverse_iterator = typename ListType::const_reverse_iterator;
  /// A reference to an element.
  using reference = typename ListType::reference;
  /// A const reference to an element.
  using const_reference = typename ListType::const_reference;
  /// A pointer to an element.
  using pointer = typename std::allocator_traits<allocator_type>::pointer;
  /// A const pointer to an element.
  using const_pointer =
      typename std::allocator_traits<allocator_type>::const_pointer;
  /// The unsigned integer size type.
  using size_type = typename ListType::size_type;
  /// The node handle type used to extract and reinsert elements.
  using node_type = NodeHandle;
  /// The return type of node-handle insertions.
  using insert_return_type = InsertReturnType<iterator, node_type>;

  /// Constructs an empty set.
  linked_hash_set() {}

  /// Constructs an empty set with the given reservation size and functors.
  ///
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param eq The key equality functor to use.
  /// @param alloc The allocator to use.
  explicit linked_hash_set(size_t reservation_size,
                           const hasher& hash = hasher(),
                           const key_equal& eq = key_equal(),
                           const allocator_type& alloc = allocator_type())
      : set_(reservation_size, Wrapped<hasher>(hash), Wrapped<key_equal>(eq),
             alloc),
        list_(alloc) {}

  /// Constructs an empty set with the given reservation size, hash, and
  /// allocator.
  ///
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param alloc The allocator to use.
  linked_hash_set(size_t reservation_size, const hasher& hash,
                  const allocator_type& alloc)
      : linked_hash_set(reservation_size, hash, key_equal(), alloc) {}

  /// Constructs an empty set with the given reservation size and allocator.
  ///
  /// @param reservation_size The number of elements to reserve space for.
  /// @param alloc The allocator to use.
  linked_hash_set(size_t reservation_size, const allocator_type& alloc)
      : linked_hash_set(reservation_size, hasher(), key_equal(), alloc) {}

  /// Constructs an empty set with the given allocator.
  ///
  /// @param alloc The allocator to use.
  explicit linked_hash_set(const allocator_type& alloc)
      : linked_hash_set(0, hasher(), key_equal(), alloc) {}

  /// Constructs a set from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element in the range.
  /// @param last Iterator past the last element in the range.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param eq The key equality functor to use.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_set(InputIt first, InputIt last, size_t reservation_size = 0,
                  const hasher& hash = hasher(),
                  const key_equal& eq = key_equal(),
                  const allocator_type& alloc = allocator_type())
      : linked_hash_set(reservation_size, hash, eq, alloc) {
    insert(first, last);
  }

  /// Constructs a set from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element in the range.
  /// @param last Iterator past the last element in the range.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param alloc The allocator to use.
  template <class InputIter>
  linked_hash_set(InputIter first, InputIter last, size_t reservation_size,
                  const hasher& hash, const allocator_type& alloc)
      : linked_hash_set(first, last, reservation_size, hash, key_equal(),
                        alloc) {}

  /// Constructs a set from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element in the range.
  /// @param last Iterator past the last element in the range.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param alloc The allocator to use.
  template <class InputIter>
  linked_hash_set(InputIter first, InputIter last, size_t reservation_size,
                  const allocator_type& alloc)
      : linked_hash_set(first, last, reservation_size, hasher(), key_equal(),
                        alloc) {}

  /// Constructs a set from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element in the range.
  /// @param last Iterator past the last element in the range.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_set(InputIt first, InputIt last, const allocator_type& alloc)
      : linked_hash_set(first, last, /*reservation_size=*/0, hasher(),
                        key_equal(), alloc) {}

  /// Constructs a set from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param eq The key equality functor to use.
  /// @param alloc The allocator to use.
  linked_hash_set(std::initializer_list<key_type> init,
                  size_t reservation_size = 0, const hasher& hash = hasher(),
                  const key_equal& eq = key_equal(),
                  const allocator_type& alloc = allocator_type())
      : linked_hash_set(init.begin(), init.end(), reservation_size, hash, eq,
                        alloc) {}

  /// Constructs a set from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param alloc The allocator to use.
  linked_hash_set(std::initializer_list<key_type> init, size_t reservation_size,
                  const allocator_type& alloc)
      : linked_hash_set(init, reservation_size, hasher(), key_equal(), alloc) {}

  /// Constructs a set from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve space for.
  /// @param hash The hash functor to use.
  /// @param alloc The allocator to use.
  linked_hash_set(std::initializer_list<key_type> init, size_t reservation_size,
                  const hasher& hash, const allocator_type& alloc)
      : linked_hash_set(init, reservation_size, hash, key_equal(), alloc) {}

  /// Constructs a set from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param alloc The allocator to use.
  linked_hash_set(std::initializer_list<key_type> init,
                  const allocator_type& alloc)
      : linked_hash_set(init, /*reservation_size=*/0, hasher(), key_equal(),
                        alloc) {}

  /// Copy constructor.
  ///
  /// @param other The set to copy from.
  linked_hash_set(const linked_hash_set& other)
      : linked_hash_set(0, other.hash_function(), other.key_eq(),
                        other.get_allocator()) {
    reserve(other.size());
    CopyFrom(other);
  }

  /// Copy constructor using the given allocator.
  ///
  /// @param other The set to copy from.
  /// @param alloc The allocator to use.
  linked_hash_set(const linked_hash_set& other, const allocator_type& alloc)
      : linked_hash_set(0, other.hash_function(), other.key_eq(), alloc) {
    reserve(other.size());
    CopyFrom(other);
  }

  /// Move constructor.
  ///
  /// @param other The set to move from.
  linked_hash_set(linked_hash_set&& other) noexcept
      : set_(std::move(other.set_)), list_(std::move(other.list_)) {
    // Since the list and set must agree for other to end up "valid",
    // explicitly clear them.
    other.set_.clear();
    other.list_.clear();
  }

  /// Move constructor using the given allocator.
  ///
  /// @param other The set to move from.
  /// @param alloc The allocator to use.
  linked_hash_set(linked_hash_set&& other, const allocator_type& alloc)
      : linked_hash_set(0, other.hash_function(), other.key_eq(), alloc) {
    if (get_allocator() == other.get_allocator()) {
      *this = std::move(other);
    } else {
      CopyFrom(std::move(other));
    }
  }

  /// Copy assignment operator.
  ///
  /// @param other The set to copy from.
  /// @return A reference to this set.
  linked_hash_set& operator=(const linked_hash_set& other) {
    if (this != &other) {
      // Make a new set, with other's hash/eq/alloc.
      set_ = SetType(0, other.set_.hash_function(),
                     other.set_.key_eq(), other.get_allocator());
      set_.reserve(other.size());
      // Copy the list, with other's allocator.
      list_ = ListType(other.get_allocator());
      CopyFrom(other);
    }
    return *this;
  }

  /// Move assignment operator.
  ///
  /// @param other The set to move from.
  /// @return A reference to this set.
  linked_hash_set& operator=(linked_hash_set&& other) noexcept {
    if (this != &other) {
      set_ = std::move(other.set_);
      list_ = std::move(other.list_);
      other.set_.clear();
      other.list_.clear();
    }
    return *this;
  }

  /// Replaces the contents with the elements from an initializer list.
  ///
  /// @param values The initializer list of elements.
  /// @return A reference to this set.
  linked_hash_set& operator=(std::initializer_list<key_type> values) {
    clear();
    reserve(values.size());
    insert(values.begin(), values.end());
    return *this;
  }

  /// Returns the number of elements in the set.
  ///
  /// @return The number of elements.
  // Derive size from set_, as list::size might be O(N).
  size_type size() const { return set_.size(); }
  /// Returns the maximum number of elements the set can hold.
  ///
  /// @return The maximum number of elements.
  size_type max_size() const noexcept { return ~size_type{}; }
  /// Returns whether the set is empty.
  ///
  /// @return `true` if the set contains no elements.
  bool empty() const { return set_.empty(); }

  /// Returns an iterator to the first element, in insertion order.
  ///
  /// @return An iterator to the beginning of the set.
  iterator begin() { return list_.begin(); }
  /// Returns an iterator past the last element.
  ///
  /// @return An iterator to the end of the set.
  iterator end() { return list_.end(); }
  /// Returns a const iterator to the first element, in insertion order.
  ///
  /// @return A const iterator to the beginning of the set.
  const_iterator begin() const { return list_.begin(); }
  /// Returns a const iterator past the last element.
  ///
  /// @return A const iterator to the end of the set.
  const_iterator end() const { return list_.end(); }
  /// Returns a const iterator to the first element, in insertion order.
  ///
  /// @return A const iterator to the beginning of the set.
  const_iterator cbegin() const { return list_.cbegin(); }
  /// Returns a const iterator past the last element.
  ///
  /// @return A const iterator to the end of the set.
  const_iterator cend() const { return list_.cend(); }
  /// Returns a reverse iterator to the last element.
  ///
  /// @return A reverse iterator to the beginning of the reversed set.
  reverse_iterator rbegin() { return list_.rbegin(); }
  /// Returns a reverse iterator past the first element.
  ///
  /// @return A reverse iterator to the end of the reversed set.
  reverse_iterator rend() { return list_.rend(); }
  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the beginning of the reversed set.
  const_reverse_iterator rbegin() const { return list_.rbegin(); }
  /// Returns a const reverse iterator past the first element.
  ///
  /// @return A const reverse iterator to the end of the reversed set.
  const_reverse_iterator rend() const { return list_.rend(); }
  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the beginning of the reversed set.
  const_reverse_iterator crbegin() const { return list_.crbegin(); }
  /// Returns a const reverse iterator past the first element.
  ///
  /// @return A const reverse iterator to the end of the reversed set.
  const_reverse_iterator crend() const { return list_.crend(); }
  /// Returns a reference to the first element.
  ///
  /// @return A reference to the first element.
  reference front() { return list_.front(); }
  /// Returns a reference to the last element.
  ///
  /// @return A reference to the last element.
  reference back() { return list_.back(); }
  /// Returns a const reference to the first element.
  ///
  /// @return A const reference to the first element.
  const_reference front() const { return list_.front(); }
  /// Returns a const reference to the last element.
  ///
  /// @return A const reference to the last element.
  const_reference back() const { return list_.back(); }

  /// Removes the first element from the set.
  void pop_front() { erase(begin()); }
  /// Removes the last element from the set.
  void pop_back() { erase(std::prev(end())); }

  /// Removes all elements from the set.
  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    set_.clear();
    list_.clear();
  }

  /// Reserves space for at least `n` elements.
  ///
  /// @param n The number of elements to reserve space for.
  void reserve(size_t n) { set_.reserve(n); }
  /// Returns the number of buckets in the underlying hash set.
  ///
  /// @return The number of buckets.
  size_t bucket_count() const { return set_.bucket_count(); }
  /// Returns the number of elements the set can hold without rehashing.
  ///
  /// @return The current capacity.
  size_t capacity() const { return set_.capacity(); }
  /// Returns the current load factor of the underlying hash set.
  ///
  /// @return The load factor.
  float load_factor() const { return set_.load_factor(); }

  /// Returns the hash functor.
  ///
  /// @return The hash functor.
  hasher hash_function() const { return set_.hash_function().fn_; }
  /// Returns the key equality functor.
  ///
  /// @return The key equality functor.
  key_equal key_eq() const { return set_.key_eq().fn_; }
  /// Returns the allocator.
  ///
  /// @return The allocator.
  allocator_type get_allocator() const { return list_.get_allocator(); }

  /// Removes the element with the given key.
  ///
  /// @param key The key to erase.
  /// @return The number of elements removed (0 or 1).
  template <typename K = key_type>
  size_type erase(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return 0;
    auto list_it = *found;
    // Erase set entry first since it refers to the list element.
    set_.erase(found);
    list_.erase(list_it);
    return 1;
  }

  /// Removes the element at the given position.
  ///
  /// @param position An iterator to the element to erase.
  /// @return An iterator to the element following the erased one.
  iterator erase(const_iterator position) {
    auto found = set_.find(position);
    assert(*found == position);
    set_.erase(found);
    return list_.erase(position);
  }

  /// Removes the elements in the range `[first, last)`.
  ///
  /// @param first An iterator to the first element to erase.
  /// @param last An iterator past the last element to erase.
  /// @return An iterator to the element following the last erased one.
  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) first = erase(first);
    return first;
  }

  /// Finds the element with the given key.
  ///
  /// @param key The key to search for.
  /// @return An iterator to the element, or `end()` if not found.
  template <typename K = key_type>
  iterator find(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  /// Finds the element with the given key.
  ///
  /// @param key The key to search for.
  /// @return A const iterator to the element, or `end()` if not found.
  template <typename K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  /// Returns the number of elements matching the given key.
  ///
  /// @param key The key to count.
  /// @return The number of matching elements (0 or 1).
  template <typename K = key_type>
  size_t count(const key_arg<K>& key) const {
    return contains(key) ? 1 : 0;
  }
  /// Returns whether the set contains an element with the given key.
  ///
  /// @param key The key to search for.
  /// @return `true` if the set contains a matching element.
  template <typename K = key_type>
  bool contains(const key_arg<K>& key) const {
    return set_.contains(key);
  }

  /// Returns the range of elements matching the given key.
  ///
  /// @param key The key to search for.
  /// @return A pair of iterators bounding the matching range.
  template <typename K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K>& key) {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  /// Returns the range of elements matching the given key.
  ///
  /// @param key The key to search for.
  /// @return A pair of const iterators bounding the matching range.
  template <typename K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  /// Inserts an element into the set.
  ///
  /// @param k The element to insert.
  /// @return A pair of an iterator to the element and a bool that is `true`
  /// if insertion took place.
  template <typename K = key_type>
  std::pair<iterator, bool> insert(const key_arg<K>& k) {
    return InsertInternal(list_.end(), k);
  }
  /// Inserts an element into the set.
  ///
  /// @param k The element to insert.
  /// @return A pair of an iterator to the element and a bool that is `true`
  /// if insertion took place.
  template <typename K = key_type, K* = nullptr>
  std::pair<iterator, bool> insert(key_arg<K>&& k) {
    return InsertInternal(list_.end(), std::move(k));
  }

  /// Inserts an element into the set using a position hint.
  ///
  /// @param hint An iterator hint for where to insert.
  /// @param k The element to insert.
  /// @return An iterator to the inserted or existing element.
  template <typename K = key_type,
            std::enable_if_t<
                !std::is_convertible_v<const key_arg<K>&, const_iterator> &&
                    !std::is_convertible_v<const key_arg<K>&, iterator>,
                int> = 0>
  iterator insert(const_iterator hint, const key_arg<K>& k) {
    return InsertInternal(hint, k).first;
  }
  /// Inserts an element into the set using a position hint.
  ///
  /// @param hint An iterator hint for where to insert.
  /// @param k The element to insert.
  /// @return An iterator to the inserted or existing element.
  template <
      typename K = key_type, K* = nullptr,
      std::enable_if_t<!std::is_convertible_v<key_arg<K>&&, const_iterator> &&
                           !std::is_convertible_v<key_arg<K>&&, iterator>,
                       int> = 0>
  iterator insert(const_iterator hint, key_arg<K>&& k) {
    return InsertInternal(hint, std::move(k)).first;
  }

  /// Inserts the elements from an initializer list.
  ///
  /// @param ilist The initializer list of elements to insert.
  void insert(std::initializer_list<key_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  /// Inserts the elements from the range `[first, last)`.
  ///
  /// @param first An iterator to the first element to insert.
  /// @param last An iterator past the last element to insert.
  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) insert(*first);
  }

  /// Inserts an element extracted as a node handle.
  ///
  /// @param node The node handle to insert.
  /// @return An `insert_return_type` describing the result.
  insert_return_type insert(node_type&& node) {
    if (node.empty()) return {end(), false, node_type()};
    if (auto [set_itr, inserted] = set_.emplace(node.list_.begin()); inserted) {
      list_.splice(list_.end(), node.list_);
      return {*set_itr, true, node_type()};
    } else {
      return {*set_itr, false, std::move(node)};
    }
  }

  /// Inserts an element extracted as a node handle, using a position hint.
  ///
  /// @param node The node handle to insert.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, node_type&& node) {
    return insert(std::move(node)).first;
  }

  /// Constructs an element in place in the set.
  ///
  /// @param args The arguments used to construct the element.
  /// @return A pair of an iterator to the element and a bool that is `true`
  /// if insertion took place.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return EmplaceInternal(list_.end(), std::forward<Args>(args)...);
  }

  /// Constructs an element in place in the set using a position hint.
  ///
  /// @param hint An iterator hint for where to insert.
  /// @param args The arguments used to construct the element.
  /// @return An iterator to the inserted or existing element.
  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    return EmplaceInternal(hint, std::forward<Args>(args)...).first;
  }

  /// Merges the elements of another set into this one.
  ///
  /// @param src The source set to merge from.
  template <typename H, typename E>
  void merge(linked_hash_set<Key, H, E, Alloc>& src) {
    auto itr = src.list_.begin();
    while (itr != src.list_.end()) {
      if (contains(*itr)) {
        ++itr;
      } else {
        insert(src.extract(itr++));
      }
    }
  }

  /// Merges the elements of another set into this one.
  ///
  /// @param src The source set to merge from.
  template <typename H, typename E>
  void merge(linked_hash_set<Key, H, E, Alloc>&& src) {
    merge(src);
  }

  /// Extracts the element at the given position as a node handle.
  ///
  /// @param position An iterator to the element to extract.
  /// @return A node handle owning the extracted element.
  node_type extract(const_iterator position) {
    set_.erase(position);
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, position);
    return node_type(std::move(extracted_node_list));
  }

  /// Extracts the element with the given key as a node handle.
  ///
  /// @param key The key of the element to extract.
  /// @return A node handle owning the extracted element, or an empty handle.
  template <class K = key_type,
            typename std::enable_if_t<!std::is_same_v<K, iterator>, int> = 0>
  node_type extract(const key_arg<K>& key) {
    auto node = set_.extract(key);
    if (node.empty()) return node_type();
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, node.value());
    return node_type(std::move(extracted_node_list));
  }

  /// Swaps the contents of this set with another.
  ///
  /// @param other The set to swap with.
  void swap(linked_hash_set& other) noexcept {
    using std::swap;
    swap(set_, other.set_);
    swap(list_, other.list_);
  }

  /// Returns whether two sets contain the same elements.
  ///
  /// @param a The first set to compare.
  /// @param b The second set to compare.
  /// @return `true` if the sets are equal.
  friend bool operator==(const linked_hash_set& a, const linked_hash_set& b) {
    if (a.size() != b.size()) return false;
    const linked_hash_set* outer = &a;
    const linked_hash_set* inner = &b;
    if (outer->capacity() > inner->capacity()) std::swap(outer, inner);
    for (const value_type& elem : *outer)
      if (!inner->contains(elem)) return false;
    return true;
  }

  /// Returns whether two sets contain different elements.
  ///
  /// @param a The first set to compare.
  /// @param b The second set to compare.
  /// @return `true` if the sets are not equal.
  friend bool operator!=(const linked_hash_set& a, const linked_hash_set& b) {
    return !(a == b);
  }

  /// Rehashes the underlying hash set to hold at least `n` buckets.
  ///
  /// @param n The minimum number of buckets.
  void rehash(size_t n) { set_.rehash(n); }

 private:
  template <typename Other>
  void CopyFrom(Other&& other) {
    for (auto& elem : other.list_) {
      set_.insert(list_.insert(list_.end(), std::move(elem)));
    }
    assert(set_.size() == list_.size());
  }

  template <typename... Args>
  std::pair<iterator, bool> EmplaceInternal(const_iterator hint,
                                            Args&&... args) {
    ListType node_donor(get_allocator());
    auto list_iter =
        node_donor.emplace(node_donor.end(), std::forward<Args>(args)...);
    auto ins = set_.insert(list_iter);
    if (!ins.second) return {*ins.first, false};
    list_.splice(hint, node_donor, list_iter);
    return {list_iter, true};
  }

  template <typename U>
  std::pair<iterator, bool> InsertInternal(const_iterator hint,
                                           U&& key) {  // NOLINT(build/c++11)
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(key, [&](const auto& ctor) {
      constructed = true;
      ctor(list_.emplace(hint, std::forward<U>(key)));
    });
    return {*set_iter, constructed};
  }

  // The set component, used for speedy lookups.
  SetType set_;

  // The list component, used for maintaining insertion order.
  ListType list_;
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_LINKED_HASH_SET_H_
