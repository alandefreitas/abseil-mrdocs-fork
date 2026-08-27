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
// File: linked_hash_map.h
// -----------------------------------------------------------------------------
//
// This is a simple insertion-ordered map. It provides O(1) amortized
// insertions and lookups, as well as iteration over the map in the insertion
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

#ifndef ABSL_CONTAINER_LINKED_HASH_MAP_H_
#define ABSL_CONTAINER_LINKED_HASH_MAP_H_

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/internal/common.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// An insertion-ordered map.
///
/// Provides O(1) amortized insertions and lookups, as well as iteration over
/// the map in the insertion order. This class is thread-compatible, but not
/// exception-safe. It supports heterogeneous lookups.
template <typename Key, typename Value,
          typename KeyHash = typename absl::flat_hash_set<Key>::hasher,
          typename KeyEq =
              typename absl::flat_hash_set<Key, KeyHash>::key_equal,
          typename Alloc = std::allocator<std::pair<const Key, Value>>>
class linked_hash_map {
  using KeyArgImpl = absl::container_internal::KeyArg<
      absl::container_internal::IsTransparent<KeyEq>::value &&
      absl::container_internal::IsTransparent<KeyHash>::value>;

 public:
  /// The key type.
  using key_type = Key;
  /// The mapped value type.
  using mapped_type = Value;
  /// The hash function type for keys.
  using hasher = KeyHash;
  /// The equality comparison type for keys.
  using key_equal = KeyEq;
  /// The stored element type, a key/value pair.
  using value_type = std::pair<const key_type, mapped_type>;
  /// The allocator type.
  using allocator_type = Alloc;
  /// The signed integer type for iterator differences.
  using difference_type = ptrdiff_t;

 private:
  template <class K>
  using key_arg = typename KeyArgImpl::template type<K, key_type>;

  using ListType = std::list<value_type, Alloc>;

  template <class Fn>
  class Wrapped {
    template <typename K>
    static const K& ToKey(const K& k) {
      return k;
    }
    static const key_type& ToKey(typename ListType::const_iterator it) {
      return it->first;
    }
    static const key_type& ToKey(typename ListType::iterator it) {
      return it->first;
    }

    Fn fn_;

    friend linked_hash_map;

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
    using key_type = linked_hash_map::key_type;
    using mapped_type = linked_hash_map::mapped_type;
    using allocator_type = linked_hash_map::allocator_type;

    constexpr NodeHandle() noexcept = default;
    NodeHandle(NodeHandle&& nh) noexcept = default;
    ~NodeHandle() = default;
    NodeHandle& operator=(NodeHandle&& node) noexcept = default;
    bool empty() const noexcept { return list_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }
    allocator_type get_allocator() const { return list_.get_allocator(); }
    const key_type& key() const { return list_.front().first; }
    mapped_type& mapped() { return list_.front().second; }
    void swap(NodeHandle& nh) noexcept { list_.swap(nh.list_); }

   private:
    friend linked_hash_map;

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
  using iterator = typename ListType::iterator;
  /// The const iterator type, iterating in insertion order.
  using const_iterator = typename ListType::const_iterator;
  /// The reverse iterator type.
  using reverse_iterator = typename ListType::reverse_iterator;
  /// The const reverse iterator type.
  using const_reverse_iterator = typename ListType::const_reverse_iterator;
  /// A reference to an element.
  using reference = typename ListType::reference;
  /// A const reference to an element.
  using const_reference = typename ListType::const_reference;
  /// The unsigned integer type for sizes.
  using size_type = typename ListType::size_type;
  /// A pointer to an element.
  using pointer = typename std::allocator_traits<allocator_type>::pointer;
  /// A const pointer to an element.
  using const_pointer =
      typename std::allocator_traits<allocator_type>::const_pointer;
  /// The node handle type used to extract and reinsert elements.
  using node_type = NodeHandle;
  /// The return type of node-handle insertions.
  using insert_return_type = InsertReturnType<iterator, node_type>;

  /// Constructs an empty map.
  linked_hash_map() {}

  /// Constructs an empty map with the given capacity, hash, equality, and
  /// allocator.
  ///
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param eq The key equality comparison to use.
  /// @param alloc The allocator to use.
  explicit linked_hash_map(size_t reservation_size,
                           const hasher& hash = hasher(),
                           const key_equal& eq = key_equal(),
                           const allocator_type& alloc = allocator_type())
      : set_(reservation_size, Wrapped<hasher>(hash), Wrapped<key_equal>(eq),
             alloc),
        list_(alloc) {}

  /// Constructs an empty map with the given capacity, hash, and allocator.
  ///
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param alloc The allocator to use.
  linked_hash_map(size_t reservation_size, const hasher& hash,
                  const allocator_type& alloc)
      : linked_hash_map(reservation_size, hash, key_equal(), alloc) {}

  /// Constructs an empty map with the given capacity and allocator.
  ///
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param alloc The allocator to use.
  linked_hash_map(size_t reservation_size, const allocator_type& alloc)
      : linked_hash_map(reservation_size, hasher(), key_equal(), alloc) {}

  /// Constructs an empty map with the given allocator.
  ///
  /// @param alloc The allocator to use.
  explicit linked_hash_map(const allocator_type& alloc)
      : linked_hash_map(0, hasher(), key_equal(), alloc) {}

  /// Constructs a map from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element of the range.
  /// @param last Iterator past the last element of the range.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param eq The key equality comparison to use.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_map(InputIt first, InputIt last, size_t reservation_size = 0,
                  const hasher& hash = hasher(),
                  const key_equal& eq = key_equal(),
                  const allocator_type& alloc = allocator_type())
      : linked_hash_map(reservation_size, hash, eq, alloc) {
    insert(first, last);
  }

  /// Constructs a map from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element of the range.
  /// @param last Iterator past the last element of the range.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_map(InputIt first, InputIt last, size_t reservation_size,
                  const hasher& hash, const allocator_type& alloc)
      : linked_hash_map(first, last, reservation_size, hash, key_equal(),
                        alloc) {}

  /// Constructs a map from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element of the range.
  /// @param last Iterator past the last element of the range.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_map(InputIt first, InputIt last, size_t reservation_size,
                  const allocator_type& alloc)
      : linked_hash_map(first, last, reservation_size, hasher(), key_equal(),
                        alloc) {}

  /// Constructs a map from the range `[first, last)`.
  ///
  /// @param first Iterator to the first element of the range.
  /// @param last Iterator past the last element of the range.
  /// @param alloc The allocator to use.
  template <class InputIt>
  linked_hash_map(InputIt first, InputIt last, const allocator_type& alloc)
      : linked_hash_map(first, last, /*reservation_size=*/0, hasher(),
                        key_equal(), alloc) {}

  /// Constructs a map from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param eq The key equality comparison to use.
  /// @param alloc The allocator to use.
  linked_hash_map(std::initializer_list<value_type> init,
                  size_t reservation_size = 0, const hasher& hash = hasher(),
                  const key_equal& eq = key_equal(),
                  const allocator_type& alloc = allocator_type())
      : linked_hash_map(init.begin(), init.end(), reservation_size, hash, eq,
                        alloc) {}

  /// Constructs a map from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param hash The hash function to use.
  /// @param alloc The allocator to use.
  linked_hash_map(std::initializer_list<value_type> init,
                  size_t reservation_size, const hasher& hash,
                  const allocator_type& alloc)
      : linked_hash_map(init, reservation_size, hash, key_equal(), alloc) {}

  /// Constructs a map from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param reservation_size The number of elements to reserve capacity for.
  /// @param alloc The allocator to use.
  linked_hash_map(std::initializer_list<value_type> init,
                  size_t reservation_size, const allocator_type& alloc)
      : linked_hash_map(init, reservation_size, hasher(), key_equal(), alloc) {}

  /// Constructs a map from an initializer list.
  ///
  /// @param init The initializer list of elements.
  /// @param alloc The allocator to use.
  linked_hash_map(std::initializer_list<value_type> init,
                  const allocator_type& alloc)
      : linked_hash_map(init, /*reservation_size=*/0, hasher(), key_equal(),
                        alloc) {}

  /// Constructs a map by copying another map.
  ///
  /// @param other The map to copy from.
  linked_hash_map(const linked_hash_map& other)
      : linked_hash_map(0, other.hash_function(), other.key_eq(),
                        other.get_allocator()) {
    reserve(other.size());
    CopyFrom(other);
  }

  /// Constructs a map by copying another map, using the given allocator.
  ///
  /// @param other The map to copy from.
  /// @param alloc The allocator to use.
  linked_hash_map(const linked_hash_map& other, const allocator_type& alloc)
      : linked_hash_map(0, other.hash_function(), other.key_eq(), alloc) {
    reserve(other.size());
    CopyFrom(other);
  }

  /// Constructs a map by moving from another map.
  ///
  /// @param other The map to move from.
  linked_hash_map(linked_hash_map&& other) noexcept
      : set_(std::move(other.set_)), list_(std::move(other.list_)) {
    // Since the list and set must agree for other to end up "valid",
    // explicitly clear them.
    other.set_.clear();
    other.list_.clear();
  }

  /// Constructs a map by moving from another map, using the given allocator.
  ///
  /// @param other The map to move from.
  /// @param alloc The allocator to use.
  linked_hash_map(linked_hash_map&& other, const allocator_type& alloc)
      : linked_hash_map(0, other.hash_function(), other.key_eq(), alloc) {
    if (get_allocator() == other.get_allocator()) {
      *this = std::move(other);
    } else {
      CopyFrom(std::move(other));
    }
  }

  /// Copy assignment operator.
  ///
  /// @param other The map to copy from.
  /// @return A reference to this map.
  linked_hash_map& operator=(const linked_hash_map& other) {
    if (this != &other) {
      // Make a new set, with other's hash/eq/alloc.
      set_ = SetType(0, other.set_.hash_function(), other.set_.key_eq(),
                     other.get_allocator());
      set_.reserve(other.size());
      // Copy the list, with other's allocator.
      list_ = ListType(other.get_allocator());
      CopyFrom(other);
    }
    return *this;
  }

  /// Move assignment operator.
  ///
  /// @param other The map to move from.
  /// @return A reference to this map.
  linked_hash_map& operator=(linked_hash_map&& other) noexcept {
    if (this != &other) {
      // underlying containers will handle progagate_on_container_move details
      set_ = std::move(other.set_);
      list_ = std::move(other.list_);
      other.set_.clear();
      other.list_.clear();
    }
    return *this;
  }

  /// Assigns the contents of an initializer list to the map.
  ///
  /// @param values The initializer list of elements.
  /// @return A reference to this map.
  linked_hash_map& operator=(std::initializer_list<value_type> values) {
    clear();
    reserve(values.size());
    insert(values.begin(), values.end());
    return *this;
  }

  // Derive size_ from set_, as list::size might be O(N).
  /// Returns the number of elements in the map.
  ///
  /// @return The number of elements.
  size_type size() const { return set_.size(); }
  /// Returns the maximum number of elements the map can hold.
  ///
  /// @return The maximum number of elements.
  size_type max_size() const noexcept { return ~size_type{}; }
  /// Returns whether the map is empty.
  ///
  /// @return `true` if the map contains no elements.
  bool empty() const { return set_.empty(); }

  // Iteration is list-like, in insertion order.
  // These are all forwarded.
  /// Returns an iterator to the first element.
  ///
  /// @return An iterator to the beginning of the map.
  iterator begin() { return list_.begin(); }
  /// Returns an iterator past the last element.
  ///
  /// @return An iterator to the end of the map.
  iterator end() { return list_.end(); }
  /// Returns a const iterator to the first element.
  ///
  /// @return A const iterator to the beginning of the map.
  const_iterator begin() const { return list_.begin(); }
  /// Returns a const iterator past the last element.
  ///
  /// @return A const iterator to the end of the map.
  const_iterator end() const { return list_.end(); }
  /// Returns a const iterator to the first element.
  ///
  /// @return A const iterator to the beginning of the map.
  const_iterator cbegin() const { return list_.cbegin(); }
  /// Returns a const iterator past the last element.
  ///
  /// @return A const iterator to the end of the map.
  const_iterator cend() const { return list_.cend(); }
  /// Returns a reverse iterator to the last element.
  ///
  /// @return A reverse iterator to the beginning of the reversed map.
  reverse_iterator rbegin() { return list_.rbegin(); }
  /// Returns a reverse iterator before the first element.
  ///
  /// @return A reverse iterator to the end of the reversed map.
  reverse_iterator rend() { return list_.rend(); }
  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the beginning of the reversed map.
  const_reverse_iterator rbegin() const { return list_.rbegin(); }
  /// Returns a const reverse iterator before the first element.
  ///
  /// @return A const reverse iterator to the end of the reversed map.
  const_reverse_iterator rend() const { return list_.rend(); }
  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the beginning of the reversed map.
  const_reverse_iterator crbegin() const { return list_.crbegin(); }
  /// Returns a const reverse iterator before the first element.
  ///
  /// @return A const reverse iterator to the end of the reversed map.
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

  /// Removes the first element from the map.
  void pop_front() { erase(begin()); }
  /// Removes the last element from the map.
  void pop_back() { erase(std::prev(end())); }

  /// Removes all elements from the map.
  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    set_.clear();
    list_.clear();
  }

  /// Reserves capacity for at least `n` elements.
  ///
  /// @param n The number of elements to reserve capacity for.
  void reserve(size_t n) { set_.reserve(n); }
  /// Returns the number of elements the map can hold without rehashing.
  ///
  /// @return The current capacity.
  size_t capacity() const { return set_.capacity(); }
  /// Returns the number of buckets in the underlying hash set.
  ///
  /// @return The bucket count.
  size_t bucket_count() const { return set_.bucket_count(); }
  /// Returns the current load factor of the underlying hash set.
  ///
  /// @return The load factor.
  float load_factor() const { return set_.load_factor(); }

  /// Returns the hash function used by the map.
  ///
  /// @return The hash function.
  hasher hash_function() const { return set_.hash_function().fn_; }
  /// Returns the key equality comparison used by the map.
  ///
  /// @return The key equality comparison.
  key_equal key_eq() const { return set_.key_eq().fn_; }
  /// Returns the allocator used by the map.
  ///
  /// @return The allocator.
  allocator_type get_allocator() const { return list_.get_allocator(); }

  /// Removes the element with the given key.
  ///
  /// @param key The key to erase.
  /// @return The number of elements removed (0 or 1).
  template <class K = key_type>
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
  /// @return An iterator to the element following the erased element.
  iterator erase(const_iterator position) {
    auto found = set_.find(position);
    assert(*found == position);
    set_.erase(found);
    return list_.erase(position);
  }

  /// Removes the element at the given position.
  ///
  /// @param position An iterator to the element to erase.
  /// @return An iterator to the element following the erased element.
  iterator erase(iterator position) {
    return erase(static_cast<const_iterator>(position));
  }

  /// Removes the elements in the range `[first, last)`.
  ///
  /// @param first An iterator to the first element to erase.
  /// @param last An iterator past the last element to erase.
  /// @return An iterator to the element following the last erased element.
  iterator erase(iterator first, iterator last) {
    while (first != last) first = erase(first);
    return first;
  }

  /// Removes the elements in the range `[first, last)`.
  ///
  /// @param first An iterator to the first element to erase.
  /// @param last An iterator past the last element to erase.
  /// @return An iterator to the element following the last erased element.
  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) first = erase(first);
    if (first == end()) return end();
    return *set_.find(first);
  }

  /// Finds the element with the given key.
  ///
  /// @param key The key to search for.
  /// @return An iterator to the element, or `end()` if not found.
  template <class K = key_type>
  iterator find(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  /// Finds the element with the given key.
  ///
  /// @param key The key to search for.
  /// @return A const iterator to the element, or `end()` if not found.
  template <class K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  /// Returns the number of elements with the given key.
  ///
  /// @param key The key to count.
  /// @return The number of matching elements (0 or 1).
  template <class K = key_type>
  size_type count(const key_arg<K>& key) const {
    return contains(key) ? 1 : 0;
  }
  /// Returns whether the map contains an element with the given key.
  ///
  /// @param key The key to search for.
  /// @return `true` if the key is present.
  template <class K = key_type>
  bool contains(const key_arg<K>& key) const {
    return set_.contains(key);
  }

  /// Returns a reference to the value mapped to the given key.
  ///
  /// @param key The key to look up.
  /// @return A reference to the mapped value.
  template <class K = key_type>
  mapped_type& at(const key_arg<K>& key) {
    auto it = find(key);
    if (ABSL_PREDICT_FALSE(it == end())) {
      ThrowStdOutOfRange("absl::linked_hash_map::at");
    }
    return it->second;
  }

  /// Returns a const reference to the value mapped to the given key.
  ///
  /// @param key The key to look up.
  /// @return A const reference to the mapped value.
  template <class K = key_type>
  const mapped_type& at(const key_arg<K>& key) const {
    return const_cast<linked_hash_map*>(this)->at(key);
  }

  /// Returns the range of elements matching the given key.
  ///
  /// @param key The key to look up.
  /// @return A pair of iterators bounding the matching elements.
  template <class K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K>& key) {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  /// Returns the range of elements matching the given key.
  ///
  /// @param key The key to look up.
  /// @return A pair of const iterators bounding the matching elements.
  template <class K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  /// Returns a reference to the value mapped to the given key, inserting a
  /// default-constructed value if the key is not present.
  ///
  /// @param key The key to look up.
  /// @return A reference to the mapped value.
  template <class K = key_type>
  mapped_type& operator[](const key_arg<K>& key) {
    return LazyEmplaceInternal(key).first->second;
  }

  /// Returns a reference to the value mapped to the given key, inserting a
  /// default-constructed value if the key is not present.
  ///
  /// @param key The key to look up.
  /// @return A reference to the mapped value.
  template <class K = key_type, K* = nullptr>
  mapped_type& operator[](key_arg<K>&& key) {
    return LazyEmplaceInternal(std::forward<key_arg<K>>(key)).first->second;
  }

  /// Inserts an element into the map.
  ///
  /// @param v The value to insert.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  std::pair<iterator, bool> insert(const value_type& v) {
    return InsertInternal(v);
  }
  /// Inserts an element into the map.
  ///
  /// @param v The value to insert.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  std::pair<iterator, bool> insert(value_type&& v) {
    return InsertInternal(std::move(v));
  }

  /// Inserts an element into the map, ignoring the position hint.
  ///
  /// @param v The value to insert.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, const value_type& v) {
    return insert(v).first;
  }
  /// Inserts an element into the map, ignoring the position hint.
  ///
  /// @param v The value to insert.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, value_type&& v) {
    return insert(std::move(v)).first;
  }

  /// Inserts the elements of an initializer list into the map.
  ///
  /// @param ilist The initializer list of elements to insert.
  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  /// Inserts the elements of the range `[first, last)` into the map.
  ///
  /// @param first An iterator to the first element to insert.
  /// @param last An iterator past the last element to insert.
  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) insert(*first);
  }

  /// Inserts an element via a node handle.
  ///
  /// @param node The node handle holding the element to insert.
  /// @return The result of the insertion, including the node handle if
  /// insertion did not take place.
  insert_return_type insert(node_type&& node) {
    if (node.empty()) return {end(), false, node_type()};
    if (auto [set_itr, inserted] = set_.emplace(node.list_.begin()); inserted) {
      list_.splice(list_.end(), node.list_);
      return {*set_itr, true, node_type()};
    } else {
      return {*set_itr, false, std::move(node)};
    }
  }

  /// Inserts an element via a node handle, ignoring the position hint.
  ///
  /// @param node The node handle holding the element to insert.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, node_type&& node) {
    return insert(std::move(node)).first;
  }

  // The last two template parameters ensure that both arguments are rvalues
  // (lvalue arguments are handled by the overloads below). This is necessary
  // for supporting bitfield arguments.
  //
  //   union { int n : 1; };
  //   linked_hash_map<int, int> m;
  //   m.insert_or_assign(n, n);
  /// Inserts the given key/value, or assigns the value if the key exists.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, class V = mapped_type, K* = nullptr,
            V* = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, V&& v) {
    return InsertOrAssignInternal(std::forward<key_arg<K>>(k),
                                  std::forward<V>(v));
  }

  /// Inserts the given key/value, or assigns the value if the key exists.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, class V = mapped_type, K* = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, const V& v) {
    return InsertOrAssignInternal(std::forward<key_arg<K>>(k), v);
  }

  /// Inserts the given key/value, or assigns the value if the key exists.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, class V = mapped_type, V* = nullptr>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, V&& v) {
    return InsertOrAssignInternal(k, std::forward<V>(v));
  }

  /// Inserts the given key/value, or assigns the value if the key exists.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, class V = mapped_type>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, const V& v) {
    return InsertOrAssignInternal(k, v);
  }

  /// Inserts the given key/value, or assigns the value if the key exists,
  /// ignoring the position hint.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or updated element.
  template <class K = key_type, class V = mapped_type, K* = nullptr,
            V* = nullptr>
  iterator insert_or_assign(const_iterator hint, key_arg<K>&& k, V&& v) {
    return insert_or_assign(std::forward<key_arg<K>>(k), std::forward<V>(v))
        .first;
  }

  /// Inserts the given key/value, or assigns the value if the key exists,
  /// ignoring the position hint.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or updated element.
  template <class K = key_type, class V = mapped_type, K* = nullptr>
  iterator insert_or_assign(const_iterator hint, key_arg<K>&& k, const V& v) {
    return insert_or_assign(std::forward<key_arg<K>>(k), v).first;
  }

  /// Inserts the given key/value, or assigns the value if the key exists,
  /// ignoring the position hint.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or updated element.
  template <class K = key_type, class V = mapped_type, V* = nullptr>
  iterator insert_or_assign(const_iterator hint, const key_arg<K>& k, V&& v) {
    return insert_or_assign(k, std::forward<V>(v)).first;
  }

  /// Inserts the given key/value, or assigns the value if the key exists,
  /// ignoring the position hint.
  ///
  /// @param k The key to insert or look up.
  /// @param v The value to assign.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or updated element.
  template <class K = key_type, class V = mapped_type>
  iterator insert_or_assign(const_iterator hint, const key_arg<K>& k,
                            const V& v) {
    return insert_or_assign(k, v).first;
  }

  /// Constructs an element in place if the key is not already present.
  ///
  /// @param args The arguments used to construct the element.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    ListType node_donor(get_allocator());
    auto list_iter =
        node_donor.emplace(node_donor.end(), std::forward<Args>(args)...);
    auto ins = set_.insert(list_iter);
    if (!ins.second) return {*ins.first, false};
    list_.splice(list_.end(), node_donor, list_iter);
    return {list_iter, true};
  }

  /// Constructs an element in place if the key is not present, ignoring the
  /// position hint.
  ///
  /// @param k The key to insert or look up.
  /// @param args The arguments used to construct the mapped value.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  template <class K = key_type, class... Args, K* = nullptr>
  iterator try_emplace(const_iterator hint, key_arg<K>&& k, Args&&... args) {
    return try_emplace(std::forward<key_arg<K>>(k), std::forward<Args>(args)...)
        .first;
  }

  /// Constructs an element in place if the key is not present, ignoring the
  /// position hint.
  ///
  /// @param args The arguments used to construct the element.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
  }

  /// Constructs an element in place if the key is not already present.
  ///
  /// @param key The key to insert or look up.
  /// @param args The arguments used to construct the mapped value.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, typename... Args, K* = nullptr>
  std::pair<iterator, bool> try_emplace(key_arg<K>&& key, Args&&... args) {
    return LazyEmplaceInternal(std::forward<key_arg<K>>(key),
                               std::forward<Args>(args)...);
  }

  /// Merges the elements of another map into this one.
  ///
  /// @param src The source map whose elements are moved in.
  template <typename H, typename E>
  void merge(linked_hash_map<Key, Value, H, E, Alloc>& src) {
    auto itr = src.list_.begin();
    while (itr != src.list_.end()) {
      if (contains(itr->first)) {
        ++itr;
      } else {
        insert(src.extract(itr++));
      }
    }
  }

  /// Merges the elements of another map into this one.
  ///
  /// @param src The source map whose elements are moved in.
  template <typename H, typename E>
  void merge(linked_hash_map<Key, Value, H, E, Alloc>&& src) {
    merge(src);
  }

  /// Extracts the element at the given position as a node handle.
  ///
  /// @param position An iterator to the element to extract.
  /// @return A node handle owning the extracted element.
  node_type extract(const_iterator position) {
    set_.erase(position->first);
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, position);
    return node_type(std::move(extracted_node_list));
  }

  /// Extracts the element with the given key as a node handle.
  ///
  /// @param key The key of the element to extract.
  /// @return A node handle owning the extracted element, or an empty handle if
  /// the key is not present.
  template <class K = key_type,
            std::enable_if_t<!std::is_same_v<K, iterator>, int> = 0>
  node_type extract(const key_arg<K>& key) {
    auto node = set_.extract(key);
    if (node.empty()) return node_type();
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, node.value());
    return node_type(std::move(extracted_node_list));
  }

  /// Moves an element from another map into this one at the given position.
  ///
  /// @param list The source map to splice the element from.
  /// @param it An iterator to the element in `list` to move.
  /// @param pos A position hint, which is ignored.
  template <typename H, typename E>
  void splice(const_iterator pos, linked_hash_map<Key, Value, H, E, Alloc>& list,
              const_iterator it) {
    if (&list == this) {
      list_.splice(list_.end(), list.list_, it);
    } else {
      insert(list.extract(it));
    }
  }

  /// Constructs an element in place if the key is not already present.
  ///
  /// @param key The key to insert or look up.
  /// @param args The arguments used to construct the mapped value.
  /// @return A pair of an iterator to the element and a bool that is `true` if
  /// insertion took place.
  template <class K = key_type, typename... Args>
  std::pair<iterator, bool> try_emplace(const key_arg<K>& key, Args&&... args) {
    return LazyEmplaceInternal(key, std::forward<Args>(args)...);
  }

  /// Constructs an element in place if the key is not present, ignoring the
  /// position hint.
  ///
  /// @param key The key to insert or look up.
  /// @param args The arguments used to construct the mapped value.
  /// @param hint A position hint, which is ignored.
  /// @return An iterator to the inserted or existing element.
  template <class K = key_type, typename... Args>
  iterator try_emplace(const_iterator hint, const key_arg<K>& key,
                       Args&&... args) {
    return LazyEmplaceInternal(key, std::forward<Args>(args)...).first;
  }

  /// Swaps the contents of this map with another.
  ///
  /// @param other The map to swap contents with.
  void swap(linked_hash_map& other) noexcept {
    using std::swap;
    swap(set_, other.set_);
    swap(list_, other.list_);
  }

  /// Returns whether two maps contain the same key/value pairs.
  ///
  /// @param a The first map to compare.
  /// @param b The second map to compare.
  /// @return `true` if the maps are equal.
  friend bool operator==(const linked_hash_map& a, const linked_hash_map& b) {
    if (a.size() != b.size()) return false;
    const linked_hash_map* outer = &a;
    const linked_hash_map* inner = &b;
    if (outer->capacity() > inner->capacity()) std::swap(outer, inner);
    for (const value_type& elem : *outer) {
      auto it = inner->find(elem.first);
      if (it == inner->end()) return false;
      if (it->second != elem.second) return false;
    }

    return true;
  }

  /// Returns whether two maps differ in their key/value pairs.
  ///
  /// @param a The first map to compare.
  /// @param b The second map to compare.
  /// @return `true` if the maps are not equal.
  friend bool operator!=(const linked_hash_map& a, const linked_hash_map& b) {
    return !(a == b);
  }

  /// Rehashes the map so it can hold at least `n` elements without rehashing.
  ///
  /// @param n The minimum number of buckets to allocate.
  void rehash(size_t n) { set_.rehash(n); }

 private:
  template <typename Other>
  void CopyFrom(Other&& other) {
    for (auto& elem : other.list_) {
      set_.insert(list_.insert(list_.end(), std::move(elem)));
    }
    assert(set_.size() == list_.size());
  }

  template <typename U>
  std::pair<iterator, bool> InsertInternal(U&& pair) {  // NOLINT(build/c++11)
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(pair.first, [&](const auto& ctor) {
      constructed = true;
      ctor(list_.emplace(list_.end(), std::forward<U>(pair)));
    });
    return {*set_iter, constructed};
  }

  template <class K, class V>
  std::pair<iterator, bool> InsertOrAssignInternal(K&& k, V&& v) {
    auto [it, inserted] =
        LazyEmplaceInternal(std::forward<K>(k), std::forward<V>(v));
    if (!inserted) {
      // NOLINTNEXTLINE(bugprone-use-after-move)
      it->second = std::forward<V>(v);
    }
    return {it, inserted};
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> LazyEmplaceInternal(K&& key, Args&&... args) {
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(
        key, [this, &constructed, &key, &args...](const auto& ctor) {
          auto list_iter =
              list_.emplace(list_.end(), std::piecewise_construct,
                            std::forward_as_tuple(std::forward<K>(key)),
                            std::forward_as_tuple(std::forward<Args>(args)...));
          constructed = true;
          ctor(list_iter);
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

#endif  // ABSL_CONTAINER_LINKED_HASH_MAP_H_
