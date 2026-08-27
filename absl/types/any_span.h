// Copyright 2026 The Abseil Authors.
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
// File: any_span.h
// -----------------------------------------------------------------------------
//
// AnySpan provides a view of a random access container, much like absl::Span
// (go/totw/93). See also go/totw/145#gtlanyspan for an introduction of AnySpan.
//
// The primary differences from absl::Span are:
//  * AnySpan works with any random access container, whereas Span only works if
//    elements are contiguous in memory -- both will work with std::vector, but
//    only AnySpan will work with std::deque.
//  * AnySpan performs a variety of transformations, such as dereferencing
//    containers of pointers, or accessing specific members from a collection of
//    structs, whereas Span does not offer such capability. For example,
//    AnySpan<std::string> can handle both std::vector<std::string> and
//    std::vector<std::string*>. Safe implicit conversions for a container's
//    value type (such as up-casting from child classes, or converting
//    reference_wrapper<T> to const T&) will happen implicitly.
//  * AnySpan's generality has some small runtime cost, usually only a
//    conditional branch per element access, or a function-pointer call in the
//    worst case. Span may be preferable when the inputs are likely to be
//    contiguous and performance is critical.
//
// AnySpan<T> is a mutable view to the elements and AnySpan<const T> is a
// read-only view to the elements, similar to absl::Span.
//
// AnySpan only requires containers to provide a size() and an operator[] that
// returns a reference. It will use data() if it returns a pointer to the type
// returned by operator[], which allows it to perform some internal
// optimizations (this should apply to many well behaved random access
// containers that use arrays internally, but notably
// RepeatedPtrField<T>::data() returns T** instead of T*).
//
// Using AnySpan as an input parameter:
//
// To write a function that can accept vector<MyMessage>,
// vector<unique_ptr<MyMessage>>, or RepeatedPtrField<MyMessage> as inputs, you
// can use AnySpan as the input to the function. AnySpan should be passed by
// value and it is trivially copyable so it does not need to be moved:
//
//   void MyFunction(AnySpan<const MyMessage> messages);
//
// You can invoke MyFunction with a vector<MyMessage> or deque<MyMessage>:
//
//  std::vector<MyMessage> messages = ...;
//  MyFunction(messages);
//
// Or a container of smart pointers:
//
//  std::deque<std::unique_ptr<MyMessage>> message_ptrs = ...;
//  MyFunction(AnySpan<const MyMessage>(
//      message_ptrs, any_span_transform::Deref()));
//
// Or, you can call the same function with a repeated proto field of type
// MyMessage:
//
//  OtherMessage proto_message = ...;
//  MyFunction(proto_message.repeated_field());
//
//
// Using AnySpan as an output parameter:
//
// To write a function that allows mutation of a fixed-size container of
// objects, you can use AnySpan with a non-const value type.
//
//   void MyMutatingFunction(AnySpan<MyMessage> messages);
//
// To bind a mutable AnySpan to a container, callers must construct it
// explicitly around an lvalue:
//
//   std::vector<MyMessage> messages = ...;
//   MyMutatingFunction(AnySpan<MyMessages>(messages));
//
// Or use one of the "Make" functions:
//
//   std::vector<MyMessage*> message_ptrs = ...;
//   MyMutatingFunction(MakeDerefAnySpan(message_ptrs));
//
// Or, if you are already dealing with a mutable view-like object, construction
// can usually be implicit:
//
//   absl::Span<MyMessage> mutable_span = ...;
//   MyMutatingFunction(mutable_span);
//
// Transforming Spans:
//
// A set of useful transformation functors are provided (see the
// any_span_transform namespace), but you can provide your own transforms as
// well.
//
// Transforms work for both mutable and const values. When a transform is used
// for a mutable AnySpan, it will usually have to accept its argument as a
// mutable reference.
//
// Transforms can be any object supported by std::invoke, such as
// callable objects, function pointers, member function pointers, and even data
// members. Invoking a transform must return a reference to T or a reference to
// a compatible object such as a std::reference_wrapper or a child class.
// Transforms that return value types will not compile and would return
// dangling references if they did.
//
//  struct MyStruct {
//    int member;
//  }
//
//  std::vector<MyStruct> structs = ...;
//
//  // Create an AnySpan<const int> that accesses the members of 'structs':
//  auto mem_ptr = &MyStruct::member;
//  AnySpan<const int> members(structs, mem_ptr);
//
//  // Or, using a lambda:
//  auto get_member = [](const MyStruct& s) -> const int& {
//    return s.member;
//  };
//  AnySpan<const int> members_from_lambda(structs, get_member);
//
// Transforms must outlive the spans that use them (even member/method pointers,
// but not function pointer). Callable transforms must provide a const call
// operator that takes a single argument and returns a reference. Transforms
// will be executed every time an element is accessed, so complex transforms may
// have significant performance consequences.
//
// Factory Functions:
//
// A set of useful functions for constructing common types of AnySpans are
// provided. Factories with "Const" in the name produce AnySpans of const
// elements. Factories with "Deref" in the name will dereference elements of the
// container or array:
//
//  AnySpan<T> MakeAnySpan(Container& c);
//  AnySpan<T> MakeDerefAnySpan(Container& c);
//  AnySpan<T> MakeAnySpan(T* ptr, std::size_t size);
//  AnySpan<const T> MakeConstAnySpan(const Container& c);
//  AnySpan<const T> MakeConstDerefAnySpan(const Container& c);
//  AnySpan<const T> MakeConstAnySpan(const T* ptr, std::size_t size);
//
// Lifetime Gotchas:
//
// Take care when constructing spans as named variables! AnySpan captures all
// arguments by reference, even if it's a pointer:
//
//  AnySpan<T> span(v, &MyClass::SomeMethod);  // Dangling reference!
//
//  // Also bad! The lambda is destroyed before the span.
//  AnySpan<T> span(v, [](U& u) { return SomeFunction(u); });
//
// Free functions are ok:
//
//  AnySpan<T> span(v, SomeFunction);  // This is OK.
//  AnySpan<T> span(v, &SomeFunction);  // This is OK too.
//
// In all other cases, you must ensure that the object used as a transform
// outlives the span, even if that object is a pointer type.
//
// AnySpan is also capable of capturing another AnySpan, so watch out for
// implicit conversions between types of AnySpans:
//
//   // MakeDerefAnySpan() returns an AnySpan<Derived>, leaving 's' pointing to
//   // a temporary!
//   vector<Derived*> v;
//   AnySpan<Base> s = MakeDerefAnySpan(v);
//
// Adapting Spans:
//
// Since AnySpan only expects operator[] and size(), it is relatively simple to
// write light-weight adaptor classes that can behave like containers. See the
// any_span_adaptor namespace for a utility class that does this for iterators
// and views.
//
// Adapters are more powerful than transforms, since they allow you to change
// the value type and element order of a container, but transforms will
// generally perform better and leave code with fewer object lifetime concerns.
//
//
// Note about RepeatedPtrField performance:
//
// AnySpan will use data() when it returns a pointer to the same type returned
// by operator[], however RepeatedPtrField's operator[] returns T& and its
// data() returns a T**. Because of this, AnySpan will fall back to a less
// efficient version of type-erasure. If you have a performance critical use of
// RepeatedPtrField, you might find this pattern to have better performance:
//
//  MyFunction(AnySpan<const MyMessage>(
//      proto_message.repeated_field().data(),
//      proto_message.repeated_field().size(),
//      any_span_transform::Deref()));
//
#ifndef ABSL_TYPES_ANY_SPAN_H_
#define ABSL_TYPES_ANY_SPAN_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"
#include "absl/meta/type_traits.h"
#include "absl/types/internal/any_span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// The accessors in the 'any_span_transform' namespace return references to
// Transform functors that may be passed to AnySpan. Generally you should
// prefer to use these functors whenever possible, as they may trigger internal
// optimizations that are otherwise not possible, and they are valid for the
// duration of the program, so you do not have to worry about their lifetime.
/// Accessors returning Transform functors that may be passed to `AnySpan`.
namespace any_span_transform {

/// Functor that returns whatever is passed to it unchanged.
struct IdentityT {
  /// Returns its argument unchanged.
  ///
  /// @param v The value to return.
  /// @return A reference to `v`.
  template <typename T>
  T& operator()(T& v) const {  // NOLINT(runtime/references)
    return v;
  }
};

/// Returns a functor that returns whatever is passed to it.
///
/// Prefer `AnySpan`'s implicit constructor directly; this may be useful when
/// writing templates on top of `AnySpan`. The reference stays valid for the
/// duration of the program.
///
/// @return A const reference to the shared identity functor.
inline const IdentityT& Identity() {
  static const IdentityT f = {};
  return f;
}

/// Functor that dereferences whatever is passed to it.
struct DerefT {
  /// Dereferences its argument.
  ///
  /// @param ptr The pointer-like object to dereference; must not be null.
  /// @return A reference to the pointed-to value.
  template <typename Ptr>
  auto operator()(Ptr& ptr) const  // NOLINT(runtime/references)
      -> decltype(*ptr) {
    ABSL_RAW_DCHECK(ptr, "Cannot dereference null pointer");
    return *ptr;
  }
};

/// Returns a functor that dereferences whatever is passed to it.
///
/// Works for smart and raw pointers, as well as `std::optional`. Do not use it
/// with containers holding elements that cannot be dereferenced, such as null
/// pointers. The reference stays valid for the duration of the program.
///
/// @return A const reference to the shared deref functor.
inline const DerefT& Deref() {
  static const DerefT f = {};
  return f;
}

}  // namespace any_span_transform

/// Utilities for adapting objects to the interface that `AnySpan` expects.
namespace any_span_adaptor {

/// Adapts a pair of iterators into a container-like object for `AnySpan`.
///
/// Useful when faced with a range or view of random access iterators. `Iter`
/// must be a valid random access iterator.
template <typename Iter>
class Range {
 public:
  static_assert(
      std::is_same_v<typename std::iterator_traits<Iter>::iterator_category,
                     std::random_access_iterator_tag>,
      "Iter must be a random access iterator.");

  /// Constructs a range over `[begin, end)`.
  ///
  /// @param begin Iterator to the first element.
  /// @param end Iterator just past the last element.
  Range(Iter begin, Iter end) {
    absl::base_internal::HardeningAssertLE(begin, end);
    begin_ = begin;
    end_ = end;
  }

  /// Returns the number of elements in the range.
  ///
  /// @return The size of the range.
  std::size_t size() const { return end_ - begin_; }

  /// Returns the element at index `i`.
  ///
  /// @param i Index of the element to access.
  /// @return The element at index `i`.
  decltype(std::declval<Iter>()[0]) operator[](std::size_t i) const {
    absl::base_internal::HardeningAssertLT(i, size());
    return begin_[i];
  }

 private:
  Iter begin_;
  Iter end_;
};

/// Returns a `Range` adaptor wrapping the given pair of iterators.
///
/// The return value must outlive any spans that use it. `Iter` must be a valid
/// random access iterator.
///
/// @param begin Iterator to the first element.
/// @param end Iterator just past the last element.
/// @return A `Range` over `[begin, end)`.
template <typename Iter>
Range<Iter> MakeAdaptorFromRange(Iter begin, Iter end) {
  return Range<Iter>(begin, end);
}

/// Returns a `Range` adaptor wrapping the given view.
///
/// The view's `begin()` and `end()` must return valid random access iterators.
/// The return value must outlive any spans that use it.
///
/// @param view The view to adapt.
/// @return A `Range` over the view's elements.
template <typename View>
auto MakeAdaptorFromView(View& view)  // NOLINT(runtime/references)
    -> Range<decltype(view.begin())> {
  return Range<decltype(view.begin())>(view.begin(), view.end());
}

}  // namespace any_span_adaptor

template <typename T>
class AnySpan;

/// A type-erased, non-owning view over a random-access sequence.
///
/// Like `absl::Span`, an `AnySpan` refers to elements owned elsewhere, but it
/// can wrap any container, array, or view of a compatible element type and can
/// apply a transform to each element on access. It must not outlive the data,
/// transform, or container it refers to.
template <typename T>
class ABSL_ATTRIBUTE_VIEW AnySpan {
 private:
  template <typename Iter, typename Value>
  class IteratorBase;

  template <typename U>
  using EnableIfMutable = std::enable_if_t<!std::is_const_v<T>, U>;

  template <typename U>
  using EnableIfConst = std::enable_if_t<std::is_const_v<T>, U>;

  static std::true_type CreatesATemporaryImpl(std::decay_t<T>&&);
  static std::false_type CreatesATemporaryImpl(const T&);
  template <typename U,
            typename B = decltype(CreatesATemporaryImpl(std::declval<U>()))>
  struct CreatesATemporary : B {};

  // Enable if invoke(transform, element) is valid and if a reference to T can
  // bind to its output. This prevents situations where the constructor may be
  // ambiguous.
  // We also verify that the conversion from TransformResult to T& does not
  // create a temporary. Otherwise, we would get a false positive in the
  // enabler where `const char*` looks like can be converted to
  // `const std::string&`.
  template <typename Transform, typename Element,
            typename TransformResult = decltype(std::invoke(
                std::declval<const Transform&>(), std::declval<Element>()))>
  using EnableIfTransformIsValid =
      std::enable_if_t<std::is_convertible_v<TransformResult, T&> &&
                       !CreatesATemporary<TransformResult>::value>;

  // Enable if Container appears to be a valid container. Just checks for size()
  // and makes sure the class is not an AnySpan for now.
  template <typename Container>
  using EnableIfContainer =
      std::enable_if_t<any_span_internal::HasSize<Container>::value &&
                       !any_span_internal::IsAnySpan<Container>::value>;

  template <typename Element>
  using EnableIfDifferentElementType =
      std::enable_if_t<!std::is_same_v<T, Element> &&
                       !std::is_same_v<T, const Element>>;

  template <typename Transform>
  using EnableIfTransformIsByCopy =
      std::enable_if_t<any_span_internal::kIsTransformCopied<Transform>, bool>;
  template <typename Transform>
  using EnableIfTransformIsByRef =
      std::enable_if_t<!any_span_internal::kIsTransformCopied<Transform>, bool>;

 public:
  /// The element type, including any const-qualification.
  using element_type = T;
  /// The element type with const removed.
  using value_type = std::remove_const_t<T>;
  /// An unsigned type used for sizes and indices.
  using size_type = std::size_t;
  /// A signed type used for iterator differences.
  using difference_type = std::ptrdiff_t;
  /// Tag marking `AnySpan` as a view type.
  using absl_internal_is_view = std::true_type;

  /// Sentinel size value meaning "until the end of the span".
  static constexpr size_type npos = static_cast<size_type>(-1);  // NOLINT

  /// A reference to an element.
  using reference = T&;
  /// A reference to a const element.
  using const_reference = std::add_const_t<T>&;

  /// A pointer to an element.
  using pointer = T*;
  /// A pointer to a const element.
  using const_pointer = std::add_const_t<T>*;

  // Note that iterator will be const if T is const.
  class iterator;
  class const_iterator;

  /// A reverse iterator over the elements.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// A reverse iterator over const elements.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /// Constructs an empty (null) span.
  AnySpan() = default;

  /// Constructs a span wrapping an initializer list.
  ///
  /// The initializer list must outlive this span.
  ///
  /// @param l The initializer list whose elements the span refers to.
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      std::initializer_list<value_type> l ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : AnySpan(l.begin(), l.size()) {}

  /// Constructs a span wrapping an initializer list, applying a transform.
  ///
  /// Useful for a list of a type other than `value_type`. The transform is
  /// taken by copy. The initializer list must outlive this span.
  ///
  /// @param l The initializer list whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element, typename Transform,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr AnySpan(std::initializer_list<Element> l
                        ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    const Transform& transform)
      : AnySpan(l.begin(), l.size(), transform) {}
  /// Constructs a span wrapping an initializer list, applying a transform.
  ///
  /// Useful for a list of a type other than `value_type`. The transform is
  /// held by reference. The initializer list must outlive this span.
  ///
  /// @param l The initializer list whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr AnySpan(std::initializer_list<Element> l
                        ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
                        any_span_transform::Identity())
      : AnySpan(l.begin(), l.size(), transform) {}

  /// Constructs a span wrapping a const array, applying a transform (by copy).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param ptr Pointer to the first element.
  /// @param size Number of elements.
  /// @param transform Functor applied to each element on access.
  template <typename Element, typename Transform,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr AnySpan(const Element* absl_nullable ptr
                        ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    size_type size, const Transform& transform)
      : AnySpan(any_span_internal::MakeArrayGetter<T>(ptr, transform), size) {}
  /// Constructs a span wrapping a const array, applying a transform (by ref).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param ptr Pointer to the first element.
  /// @param size Number of elements.
  /// @param transform Functor applied to each element on access.
  template <typename Element,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr AnySpan(const Element* absl_nullable ptr
                        ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    size_type size,
                    const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
                        any_span_transform::Identity())
      : AnySpan(any_span_internal::MakeArrayGetter<T>(ptr, transform), size) {}

  /// Constructs a span wrapping a fixed-size const array (transform by copy).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param array The array whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element, size_type N, typename Transform,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Element (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N],
      const Transform& transform)
      : AnySpan(array, N, transform) {}
  /// Constructs a span wrapping a fixed-size const array (transform by ref).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param array The array whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element, size_type N,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfTransformIsValid<Transform, const Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Element (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N],
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
          any_span_transform::Identity())
      : AnySpan(array, N, transform) {}

  /// Constructs a span wrapping a const view container (transform by copy).
  ///
  /// Enabled even for mutable spans, since some views expose mutable element
  /// access when const. The transform, container, and its storage must outlive
  /// this span; reallocating or resizing the container invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Container, typename Transform,
            typename = EnableIfContainer<Container>,
            typename = EnableIfTransformIsValid<
                Transform, decltype(std::declval<const Container&>()[0])>,
            EnableIfTransformIsByCopy<std::enable_if_t<
                absl::type_traits_internal::IsView<Container>::value,
                Transform>> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Container& container, const Transform& transform)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}
  /// Constructs a span wrapping a const non-view container (transform by copy).
  ///
  /// The transform, container, and its storage must outlive this span;
  /// reallocating or resizing the container invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Container, typename Transform,
            typename = EnableIfContainer<Container>,
            typename = EnableIfTransformIsValid<
                Transform, decltype(std::declval<const Container&>()[0])>,
            EnableIfTransformIsByCopy<std::enable_if_t<
                !absl::type_traits_internal::IsView<Container>::value,
                Transform>> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Container& container ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const Transform& transform)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}
  /// Constructs a span wrapping a const view container (transform by ref).
  ///
  /// The transform, container, and its storage must outlive this span;
  /// reallocating or resizing the container invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <
      typename Container, typename Transform = any_span_transform::IdentityT,
      typename = EnableIfContainer<Container>,
      typename = EnableIfTransformIsValid<
          Transform, decltype(std::declval<const Container&>()[0])>,
      EnableIfTransformIsByRef<
          std::enable_if_t<absl::type_traits_internal::IsView<Container>::value,
                           Transform>> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Container& container,
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
          any_span_transform::Identity())
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}
  /// Constructs a span wrapping a const non-view container (transform by ref).
  ///
  /// The transform, container, and its storage must outlive this span;
  /// reallocating or resizing the container invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Container,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfContainer<Container>,
            typename = EnableIfTransformIsValid<
                Transform, decltype(std::declval<const Container&>()[0])>,
            EnableIfTransformIsByRef<std::enable_if_t<
                !absl::type_traits_internal::IsView<Container>::value,
                Transform>> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const Container& container ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
          any_span_transform::Identity())
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}

  /// Constructs a span wrapping a mutable array (transform by copy).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param ptr Pointer to the first element.
  /// @param size Number of elements.
  /// @param transform Functor applied to each element on access.
  template <typename Element, typename Transform,
            typename = EnableIfMutable<Element>,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr AnySpan(Element* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    size_type size, const Transform& transform)
      : AnySpan(any_span_internal::MakeArrayGetter<T>(ptr, transform), size) {}
  /// Constructs a span wrapping a mutable array (transform by ref).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param ptr Pointer to the first element.
  /// @param size Number of elements.
  /// @param transform Functor applied to each element on access.
  template <typename Element,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfMutable<Element>,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr AnySpan(Element* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND,
                    size_type size,
                    const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
                        any_span_transform::Identity())
      : AnySpan(any_span_internal::MakeArrayGetter<T>(ptr, transform), size) {}

  /// Constructs a span wrapping a fixed-size mutable array (transform by copy).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param array The array whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element, size_type N, typename Transform,
            typename = EnableIfMutable<Element>,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      Element (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N],
      const Transform& transform)
      : AnySpan(array, N, transform) {}
  /// Constructs a span wrapping a fixed-size mutable array (transform by ref).
  ///
  /// The transform must be a function object whose const `operator()` takes an
  /// `Element` and returns a reference to `T` or a compatible object. Both the
  /// transform and array must outlive this span.
  ///
  /// @param array The array whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Element, size_type N,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfMutable<Element>,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      Element (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N],
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
          any_span_transform::Identity())
      : AnySpan(array, N, transform) {}

  /// Constructs a span wrapping a mutable container (transform by copy).
  ///
  /// Only enabled when `T` is mutable. The transform, container, and its
  /// storage must outlive this span; reallocating or resizing the container
  /// invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Container, typename Transform,
            typename = EnableIfMutable<Container>,
            typename = EnableIfContainer<Container>,
            typename = EnableIfTransformIsValid<
                Transform, decltype(std::declval<Container&>()[0])>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr explicit AnySpan(  // NOLINT(google-explicit-constructor)
      Container& container ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const Transform& transform)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}
  /// Constructs a span wrapping a mutable container (transform by ref).
  ///
  /// Only enabled when `T` is mutable. The transform, container, and its
  /// storage must outlive this span; reallocating or resizing the container
  /// invalidates the span.
  ///
  /// @param container The container whose elements the span refers to.
  /// @param transform Functor applied to each element on access.
  template <typename Container,
            typename Transform = any_span_transform::IdentityT,
            typename = EnableIfMutable<Container>,
            typename = EnableIfContainer<Container>,
            typename = EnableIfTransformIsValid<
                Transform, decltype(std::declval<Container&>()[0])>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr explicit AnySpan(  // NOLINT(google-explicit-constructor)
      Container& container ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND =
          any_span_transform::Identity())
      : AnySpan(any_span_internal::MakeContainerGetter<T>(container, transform),
                container.size()) {}

  /// Converts a mutable span to a const span by copying its internal state.
  ///
  /// @param other The mutable span to convert from.
  template <typename LazyT = T, typename = EnableIfConst<LazyT>>
  constexpr AnySpan(  // NOLINT(google-explicit-constructor)
      const AnySpan<std::remove_const_t<T>>& other)
      : getter_(other.getter_), size_(other.size()) {}

  /// Constructs a span wrapping another span of a different element type.
  ///
  /// Made explicit because it has performance and lifetime consequences.
  ///
  /// @param other The source span to wrap.
  template <typename Element, typename = EnableIfDifferentElementType<Element>,
            typename = EnableIfTransformIsValid<any_span_transform::IdentityT,
                                                Element&>>
  constexpr explicit AnySpan(
      const AnySpan<Element>& other ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(
                    other, any_span_transform::Identity()),
                other.size()) {}

  /// Constructs a span wrapping another span, applying a transform (by copy).
  ///
  /// Made explicit because it has lifetime consequences.
  ///
  /// @param other The source span to wrap.
  /// @param transform Functor applied to each element on access.
  template <typename Element, typename Transform,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByCopy<Transform> = true>
  constexpr explicit AnySpan(const AnySpan<Element>& other
                                 ABSL_ATTRIBUTE_LIFETIME_BOUND,
                             const Transform& transform)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(other, transform),
                other.size()) {}
  /// Constructs a span wrapping another span, applying a transform (by ref).
  ///
  /// Made explicit because it has lifetime consequences.
  ///
  /// @param other The source span to wrap.
  /// @param transform Functor applied to each element on access.
  template <typename Element, typename Transform,
            typename = EnableIfTransformIsValid<Transform, Element&>,
            EnableIfTransformIsByRef<Transform> = true>
  constexpr explicit AnySpan(
      const AnySpan<Element>& other ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const Transform& transform ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : AnySpan(any_span_internal::MakeContainerGetter<T>(other, transform),
                other.size()) {}

  /// Returns a subspan starting at `pos` and spanning `len` elements.
  ///
  /// `pos` must be `<= size()`. `len` must be `<= size() - pos` or `npos`; when
  /// `npos`, the subspan runs to the end. The container and transform must stay
  /// valid, though this span may become invalid before the subspan.
  ///
  /// @param pos Index of the first element of the subspan.
  /// @param len Number of elements in the subspan, or `npos` for the rest.
  /// @return A span over the requested range.
  constexpr AnySpan subspan(size_type pos, size_type len = npos) const {
    const size_t this_size = size();
    if (len == AnySpan<T>::npos) {
      len = this_size - pos;
    }
    absl::base_internal::HardeningAssertLE(pos, this_size);
    absl::base_internal::HardeningAssertLE(
        len, static_cast<size_type>(this_size - pos));
    return AnySpan<T>(getter_.Offset(pos), len);
  }

  /// Returns a span containing the first `len` elements.
  ///
  /// `len` must be `<= size()`.
  ///
  /// @param len Number of elements from the front to include.
  /// @return A span over the first `len` elements.
  constexpr AnySpan first(size_type len) const {
    absl::base_internal::HardeningAssert(len != npos);
    return subspan(0, len);
  }

  /// Returns a span containing the last `len` elements.
  ///
  /// `len` must be `<= size()`.
  ///
  /// @param len Number of elements from the back to include.
  /// @return A span over the last `len` elements.
  constexpr AnySpan last(size_type len) const { return subspan(size() - len); }

  /// Returns the number of elements in this span.
  ///
  /// @return The size of the span.
  constexpr size_type size() const { return size_; }
  /// Reports whether this span is empty.
  ///
  /// @return `true` if the span contains no elements.
  constexpr bool empty() const { return size() == 0; }

  /// Returns a reference to the element at `index`.
  ///
  /// @param index Index of the element to access.
  /// @return A reference to the element at `index`.
  constexpr reference operator[](size_type index) const {
    absl::base_internal::HardeningAssertLT(index, size());
    return getter_.Get(index);
  }
  /// Returns a reference to the element at `index`, with bounds checking.
  ///
  /// @param index Index of the element to access.
  /// @return A reference to the element at `index`.
  constexpr reference at(size_type index) const {
    if (ABSL_PREDICT_FALSE(index >= size())) {
      absl::ThrowStdOutOfRange("AnySpan::at failed bounds check");
    }
    return getter_.Get(index);
  }
  /// Returns a reference to the first element; the span must not be empty.
  ///
  /// @return A reference to the first element.
  constexpr reference front() const {
    absl::base_internal::HardeningAssertGT(size(), size_type{0});
    return (*this)[0];
  }
  /// Returns a reference to the last element; the span must not be empty.
  ///
  /// @return A reference to the last element.
  constexpr reference back() const {
    absl::base_internal::HardeningAssertGT(size(), size_type{0});
    return (*this)[size() - 1];
  }

  /// Returns an iterator to the first element.
  ///
  /// @return An iterator to the first element, or `end()` if empty.
  constexpr iterator begin() const { return iterator(this, 0); }
  /// Returns an iterator just past the last element.
  ///
  /// @return A past-the-end iterator.
  constexpr iterator end() const { return iterator(this, size_); }
  /// Returns a reverse iterator to the last element.
  ///
  /// @return A reverse iterator to the last element, or `rend()` if empty.
  constexpr reverse_iterator rbegin() const { return reverse_iterator(end()); }
  /// Returns a reverse iterator just before the first element.
  ///
  /// @return A reverse past-the-end iterator.
  constexpr reverse_iterator rend() const { return reverse_iterator(begin()); }
  /// Returns a const iterator to the first element.
  ///
  /// @return A const iterator to the first element, or `cend()` if empty.
  constexpr const_iterator cbegin() const { return const_iterator(this, 0); }
  /// Returns a const iterator just past the last element.
  ///
  /// @return A past-the-end const iterator.
  constexpr const_iterator cend() const { return const_iterator(this, size_); }
  /// Returns a const reverse iterator to the last element.
  ///
  /// @return A const reverse iterator to the last element, or `crend()`.
  constexpr const_reverse_iterator crbegin() const { return rbegin(); }
  /// Returns a const reverse iterator just before the first element.
  ///
  /// @return A const reverse past-the-end iterator.
  constexpr const_reverse_iterator crend() const { return rend(); }

  /// Constructs a span from an element getter and size. Not for external use.
  ///
  /// @param getter The internal element accessor.
  /// @param size The number of elements.
  AnySpan(any_span_internal::Getter<T> getter, size_type size)
      : getter_(getter), size_(size) {}

  /// Hashes the span's elements for use with `absl::Hash`.
  ///
  /// @param state The hash state to combine into.
  /// @param any_span The span whose elements are hashed.
  /// @return The updated hash state.
  template <typename H>
  friend constexpr H AbslHashValue(H state, AnySpan any_span) {
    for (const auto& v : any_span) {
      state = H::combine(std::move(state), v);
    }
    return H::combine(std::move(state), any_span.size());
  }

 private:
  template <typename U>
  friend class AnySpan;

  template <typename U>
  friend bool any_span_internal::IsCheap(AnySpan<U> s);

  // Getter to access elements.
  any_span_internal::Getter<T> getter_;

  // The size of this span.
  size_type size_ = 0;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif
  // The technical reasons we need to declare these friends in this manner are
  // quite subtle and confusing, but they're necessary on some toolchains to
  // allow all mutable/const combinations with this & other range types while
  // avoiding symbol collisions or ODR violations.
  /// Compares two const spans for element-wise equality.
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if the spans have equal elements.
  friend bool operator==(AnySpan<const T> a, AnySpan<const T> b);
  /// Compares two const spans for inequality.
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if the spans differ.
  friend bool operator!=(AnySpan<const T> a, AnySpan<const T> b);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  /// Compares two spans for element-wise equality.
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if the spans have equal elements.
  friend bool operator==(AnySpan a, AnySpan b) {
    return any_span_internal::EqualImpl<const T>(a, b);
  }
  /// Compares two spans for inequality.
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if the spans differ.
  friend bool operator!=(AnySpan a, AnySpan b) { return !(a == b); }
};

/// Constructs an `AnySpan` from a view container or array.
///
/// @param c The container whose elements the span refers to.
/// @return An `AnySpan` over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::ElementType<Container>>
std::enable_if_t<
    absl::type_traits_internal::IsView<std::remove_cv_t<Container>>::value,
    AnySpan<T>>
MakeAnySpan(Container& c) {
  return AnySpan<T>(c);
}
/// Constructs an `AnySpan` from a non-view container or array.
///
/// @param c The container whose elements the span refers to.
/// @return An `AnySpan` over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::ElementType<Container>>
std::enable_if_t<
    !absl::type_traits_internal::IsView<std::remove_cv_t<Container>>::value,
    AnySpan<T>>
MakeAnySpan(Container& c ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return AnySpan<T>(c);
}

/// Constructs an `AnySpan` that dereferences a view container of pointers.
///
/// @param c The container of pointers whose pointees the span refers to.
/// @return An `AnySpan` over the dereferenced elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::DerefElementType<Container>>
std::enable_if_t<
    absl::type_traits_internal::IsView<std::remove_cv_t<Container>>::value,
    AnySpan<T>>
MakeDerefAnySpan(Container& c) {
  return AnySpan<T>(c, any_span_transform::Deref());
}
/// Constructs an `AnySpan` that dereferences a non-view container of pointers.
///
/// @param c The container of pointers whose pointees the span refers to.
/// @return An `AnySpan` over the dereferenced elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::DerefElementType<Container>>
std::enable_if_t<
    !absl::type_traits_internal::IsView<std::remove_cv_t<Container>>::value,
    AnySpan<T>>
MakeDerefAnySpan(Container& c ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return AnySpan<T>(c, any_span_transform::Deref());
}

/// Constructs an `AnySpan` from a pointer and a size.
///
/// @param ptr Pointer to the first element.
/// @param size Number of elements.
/// @return An `AnySpan` over `size` elements starting at `ptr`.
template <int&... ExplicitArgumentBarrier, typename T>
AnySpan<T> MakeAnySpan(T* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND,
                       std::size_t size) {
  return AnySpan<T>(ptr, size);
}

/// Constructs a const `AnySpan` from a view container or array.
///
/// @param c The container whose elements the span refers to.
/// @return A read-only `AnySpan` over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::ElementType<const Container>>
std::enable_if_t<absl::type_traits_internal::IsView<Container>::value,
                 AnySpan<const T>>
MakeConstAnySpan(const Container& c) {
  return AnySpan<const T>(c);
}
/// Constructs a const `AnySpan` from a non-view container or array.
///
/// @param c The container whose elements the span refers to.
/// @return A read-only `AnySpan` over the elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::ElementType<const Container>>
std::enable_if_t<!absl::type_traits_internal::IsView<Container>::value,
                 AnySpan<const T>>
MakeConstAnySpan(const Container& c ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return AnySpan<const T>(c);
}

/// Constructs a const `AnySpan` dereferencing a view container of pointers.
///
/// @param c The container of pointers whose pointees the span refers to.
/// @return A read-only `AnySpan` over the dereferenced elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::DerefElementType<const Container>>
std::enable_if_t<absl::type_traits_internal::IsView<Container>::value,
                 AnySpan<const T>>
MakeConstDerefAnySpan(const Container& c) {
  return AnySpan<const T>(c, any_span_transform::Deref());
}
/// Constructs a const `AnySpan` dereferencing a non-view container of pointers.
///
/// @param c The container of pointers whose pointees the span refers to.
/// @return A read-only `AnySpan` over the dereferenced elements of `c`.
template <int&... ExplicitArgumentBarrier, typename Container,
          typename T = any_span_internal::DerefElementType<const Container>>
std::enable_if_t<!absl::type_traits_internal::IsView<Container>::value,
                 AnySpan<const T>>
MakeConstDerefAnySpan(const Container& c ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return AnySpan<const T>(c, any_span_transform::Deref());
}

/// Constructs a const `AnySpan` from a pointer and a size.
///
/// @param ptr Pointer to the first element.
/// @param size Number of elements.
/// @return A read-only `AnySpan` over `size` elements starting at `ptr`.
template <int&... ExplicitArgumentBarrier, typename T>
AnySpan<const T> MakeConstAnySpan(const T* absl_nullable ptr,
                                  std::size_t size) {
  return AnySpan<const T>(ptr, size);
}

//
// Implementation details follow.
//

// Iterator base class. Uses CRTP (Iter should be the child class). Constness of
// the iterator is determined by the constness of Value.
template <typename T>
template <typename Iter, typename Value>
class ABSL_ATTRIBUTE_VIEW AnySpan<T>::IteratorBase {
 private:
  // Returns a reference to this as the child class.
  const Iter& self() const { return static_cast<const Iter&>(*this); }
  Iter& self() { return static_cast<Iter&>(*this); }

 public:
  /// The iterator category (random access).
  using iterator_category = std::random_access_iterator_tag;
  /// The element type with const removed.
  using value_type = std::remove_const_t<Value>;
  /// A signed type used for iterator differences.
  using difference_type = std::ptrdiff_t;
  /// A reference to an element.
  using reference = Value&;
  /// A pointer to an element.
  using pointer = Value*;

  /// Constructs an invalid iterator.
  IteratorBase() = default;

  /// Dereferences the iterator.
  ///
  /// @return A reference to the current element.
  reference operator*() const { return (*container_)[index_]; }

  /// Accesses members of the current element.
  ///
  /// @return A pointer to the current element.
  pointer absl_nonnull operator->() const { return &(*container_)[index_]; }

  /// Accesses the element `i` positions from the current one.
  ///
  /// @param i Offset from the current position.
  /// @return A reference to the element at the offset position.
  reference operator[](difference_type i) const {
    return (*container_)[index_ + i];
  }

  /// Advances the iterator by `d` positions.
  ///
  /// @param d Number of positions to advance.
  /// @return A reference to this iterator.
  Iter& operator+=(difference_type d) {
    index_ += d;
    return self();
  }

  /// Moves the iterator back by `d` positions.
  ///
  /// @param d Number of positions to move back.
  /// @return A reference to this iterator.
  Iter& operator-=(difference_type d) { return self() += -d; }

  /// Advances the iterator by one position.
  ///
  /// @return A reference to this iterator.
  Iter& operator++() {
    self() += 1;
    return self();
  }

  /// Advances the iterator by one position.
  ///
  /// @param unused Unused tag distinguishing the postfix form.
  /// @return A copy of the iterator before advancing.
  Iter operator++(int unused) {
    Iter copy(self());
    ++self();
    return copy;
  }

  /// Moves the iterator back by one position.
  ///
  /// @return A reference to this iterator.
  Iter& operator--() {
    self() -= 1;
    return self();
  }

  /// Moves the iterator back by one position.
  ///
  /// @param unused Unused tag distinguishing the postfix form.
  /// @return A copy of the iterator before moving back.
  Iter operator--(int unused) {
    Iter copy(self());
    --self();
    return copy;
  }

  /// Returns an iterator advanced by `d` positions.
  ///
  /// @param d Number of positions to advance.
  /// @return The advanced iterator.
  Iter operator+(difference_type d) const {
    Iter tmp = self();
    tmp += d;
    return tmp;
  }

  /// Returns an iterator advanced by `d` positions.
  ///
  /// @param d Number of positions to advance.
  /// @param i The iterator to advance.
  /// @return The advanced iterator.
  friend Iter operator+(difference_type d, Iter i) { return i + d; }

  /// Returns an iterator moved back by `d` positions.
  ///
  /// @param d Number of positions to move back.
  /// @return The moved iterator.
  Iter operator-(difference_type d) const { return self() + (-d); }

  /// Returns the distance to another iterator.
  ///
  /// @param other The iterator to measure against.
  /// @return The number of positions from `other` to this iterator.
  difference_type operator-(const Iter& other) const {
    return index_ - other.index_;
  }

  /// Tests whether two iterators refer to the same position.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` and `b` point to the same position.
  friend bool operator==(const Iter& a, const Iter& b) {
    return a.index_ == b.index_;
  }

  /// Tests whether two iterators refer to different positions.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` and `b` point to different positions.
  friend bool operator!=(const Iter& a, const Iter& b) {
    return a.index_ != b.index_;
  }

  /// Orders two iterators by position.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` precedes `b`.
  friend bool operator<(const Iter& a, const Iter& b) {
    return a.index_ < b.index_;
  }

  /// Orders two iterators by position.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` does not follow `b`.
  friend bool operator<=(const Iter& a, const Iter& b) {
    return a.index_ <= b.index_;
  }

  /// Orders two iterators by position.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` follows `b`.
  friend bool operator>(const Iter& a, const Iter& b) {
    return a.index_ > b.index_;
  }

  /// Orders two iterators by position.
  ///
  /// @param a The left operand.
  /// @param b The right operand.
  /// @return `true` if `a` does not precede `b`.
  friend bool operator>=(const Iter& a, const Iter& b) {
    return a.index_ >= b.index_;
  }

 protected:
  /// Constructs an iterator at the given index of the given span.
  ///
  /// @param container The span the iterator points into.
  /// @param index The element index the iterator points to.
  IteratorBase(const AnySpan* absl_nullable container, size_type index)
      : container_(container), index_(index) {}

  /// The span this iterator points into.
  const AnySpan* absl_nullable container_ = nullptr;
  /// The current element index.
  size_type index_ = 0;
};

/// Random-access iterator over the elements of an `AnySpan`.
template <typename T>
class ABSL_ATTRIBUTE_VIEW AnySpan<T>::iterator
    : public IteratorBase<iterator, T> {
 private:
  using Base = IteratorBase<iterator, T>;

 public:
  using typename Base::difference_type;
  using typename Base::iterator_category;
  using typename Base::pointer;
  using typename Base::reference;
  using typename Base::value_type;

  /// Constructs an invalid iterator.
  iterator() = default;

 private:
  // Only let AnySpan construct valid instances.
  friend class AnySpan;

  iterator(const AnySpan* absl_nullable container, size_type index)
      : Base(container, index) {}
};

/// Random-access iterator over the const elements of an `AnySpan`.
///
/// Also supports implicit conversion from a mutable `iterator`.
template <typename T>
class AnySpan<T>::const_iterator
    : public IteratorBase<const_iterator, std::add_const_t<T>> {
 private:
  using Base = IteratorBase<const_iterator, std::add_const_t<T>>;

 public:
  using typename Base::difference_type;
  using typename Base::iterator_category;
  using typename Base::pointer;
  using typename Base::reference;
  using typename Base::value_type;

  /// Constructs an invalid iterator.
  const_iterator() = default;

  /// Converts a mutable iterator to a const iterator.
  ///
  /// @param other The mutable iterator to convert from.
  // NOLINTNEXTLINE(google-explicit-constructor)
  const_iterator(const iterator& other)  // NOLINT(runtime/explicit)
      : Base(other.container_, other.index_) {}

 private:
  // Only let AnySpan construct valid instances.
  friend class AnySpan;

  const_iterator(const AnySpan* absl_nullable container, size_type index)
      : Base(container, index) {}
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_ANY_SPAN_H_
