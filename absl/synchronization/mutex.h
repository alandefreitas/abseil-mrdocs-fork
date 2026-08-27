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
// mutex.h
// -----------------------------------------------------------------------------
//
// This header file defines a `Mutex` -- a mutually exclusive lock -- and the
// most common type of synchronization primitive for facilitating locks on
// shared resources. A mutex is used to prevent multiple threads from accessing
// and/or writing to a shared resource concurrently.
//
// Unlike a `std::mutex`, the Abseil `Mutex` provides the following additional
// features:
//   * Conditional predicates intrinsic to the `Mutex` object
//   * Shared/reader locks, in addition to standard exclusive/writer locks
//   * Deadlock detection and debug support.
//
// The following helper classes are also defined within this file:
//
//  MutexLock - An RAII wrapper to acquire and release a `Mutex` for exclusive/
//              write access within the current scope.
//
//  ReaderMutexLock
//            - An RAII wrapper to acquire and release a `Mutex` for shared/read
//              access within the current scope.
//
//  WriterMutexLock
//            - Effectively an alias for `MutexLock` above, designed for use in
//              distinguishing reader and writer locks within code.
//
// In addition to simple mutex locks, this file also defines ways to perform
// locking under certain conditions.
//
//  Condition - (Preferred) Used to wait for a particular predicate that
//              depends on state protected by the `Mutex` to become true.
//  CondVar   - A lower-level variant of `Condition` that relies on
//              application code to explicitly signal the `CondVar` when
//              a condition has been met.
//
// See below for more information on using `Condition` or `CondVar`.
//
// Mutexes and mutex behavior can be quite complicated. The information within
// this header file is limited, as a result. Please consult the Mutex guide for
// more complete information and examples.

#ifndef ABSL_SYNCHRONIZATION_MUTEX_H_
#define ABSL_SYNCHRONIZATION_MUTEX_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/internal/tsan_mutex_interface.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/meta/type_traits.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/time/time.h"

// Abseil common libraries.
namespace absl {
ABSL_NAMESPACE_BEGIN

class Condition;
/// Internal parameters describing a pending wait on a `Mutex`.
struct SynchWaitParams;

namespace synchronization_internal {

template <typename T, typename = void>
struct HasConstMemberCallOperator : std::false_type {};

template <typename T>
struct HasConstMemberCallOperator<
    T, std::void_t<decltype(static_cast<bool (T::*)() const>(&T::operator()))>>
    : std::true_type {};

}  // namespace synchronization_internal

// -----------------------------------------------------------------------------
// Mutex
// -----------------------------------------------------------------------------
//
// A `Mutex` is a non-reentrant (aka non-recursive) Mutually Exclusive lock
// on some resource, typically a variable or data structure with associated
// invariants. Proper usage of mutexes prevents concurrent access by different
// threads to the same resource.
//
// A `Mutex` has two basic operations: `Mutex::lock()` and `Mutex::unlock()`.
// The `lock()` operation *acquires* a `Mutex` (in a state known as an
// *exclusive* -- or *write* -- lock), and the `unlock()` operation *releases* a
// Mutex. During the span of time between the lock() and unlock() operations,
// a mutex is said to be *held*. By design, all mutexes support exclusive/write
// locks, as this is the most common way to use a mutex.
//
// Mutex operations are only allowed under certain conditions; otherwise an
// operation is "invalid", and disallowed by the API. The conditions concern
// both the current state of the mutex and the identity of the threads that
// are performing the operations.
//
// The `Mutex` state machine for basic lock/unlock operations is quite simple:
//
// |                | lock()                 | unlock() |
// |----------------+------------------------+----------|
// | Free           | Exclusive              | invalid  |
// | Exclusive      | blocks, then exclusive | Free     |
//
// The full conditions are as follows.
//
// * Calls to `unlock()` require that the mutex be held, and must be made in the
//   same thread that performed the corresponding `lock()` operation which
//   acquired the mutex; otherwise the call is invalid.
//
// * The mutex being non-reentrant (or non-recursive) means that a call to
//   `lock()` or `try_lock()` must not be made in a thread that already holds
//   the mutex; such a call is invalid.
//
// * In other words, the state of being "held" has both a temporal component
//   (from `lock()` until `unlock()`) as well as a thread identity component:
//   the mutex is held *by a particular thread*.
//
// An "invalid" operation has undefined behavior. The `Mutex` implementation
// is allowed to do anything on an invalid call, including, but not limited to,
// crashing with a useful error message, silently succeeding, or corrupting
// data structures. In debug mode, the implementation may crash with a useful
// error message.
//
// `Mutex` is not guaranteed to be "fair" in prioritizing waiting threads; it
// is, however, approximately fair over long periods, and starvation-free for
// threads at the same priority.
//
// The lock/unlock primitives are now annotated with lock annotations
// defined in (base/thread_annotations.h). When writing multi-threaded code,
// you should use lock annotations whenever possible to document your lock
// synchronization policy. Besides acting as documentation, these annotations
// also help compilers or static analysis tools to identify and warn about
// issues that could potentially result in race conditions and deadlocks.
//
// For more information about the lock annotations, please see
// [Thread Safety
// Analysis](http://clang.llvm.org/docs/ThreadSafetyAnalysis.html) in the Clang
// documentation.
//
// See also `MutexLock`, below, for scoped `Mutex` acquisition.

/// A non-reentrant mutually exclusive lock that also supports reader-writer
/// locking and conditional critical regions.
class ABSL_LOCKABLE ABSL_ATTRIBUTE_WARN_UNUSED Mutex {
 public:
  /// Creates a `Mutex` that is not held by anyone.
  ///
  /// This constructor is typically used for Mutexes allocated on the heap or
  /// the stack. To create `Mutex` instances with static storage duration
  /// (e.g. a namespace-scoped or global variable), see
  /// `Mutex::Mutex(absl::kConstInit)` below instead.
  Mutex();

  /// Creates a mutex with static storage duration.
  ///
  /// A global variable constructed this way avoids the lifetime issues that can
  /// occur on program startup and shutdown. For Mutexes allocated on the heap
  /// and stack, instead use the default constructor.
  /// @param tag A tag that selects this overload.
  explicit constexpr Mutex(absl::ConstInitType tag);

  /// Destroys this `Mutex`.
  ~Mutex();

  /// Blocks the calling thread, if necessary, until this `Mutex` is free, and
  /// then acquires it exclusively.
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION();

  /// Deprecated alias for `lock()`.
  ABSL_DEPRECATE_AND_INLINE()
  inline void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { lock(); }

  /// Releases this `Mutex` and returns it from the exclusive/write state to the
  /// free state.
  void unlock() ABSL_UNLOCK_FUNCTION();

  /// Deprecated alias for `unlock()`.
  ABSL_DEPRECATE_AND_INLINE()
  inline void Unlock() ABSL_UNLOCK_FUNCTION() { unlock(); }

  /// Tries to acquire this `Mutex` exclusively without blocking.
  ///
  /// @return `true` if the mutex was acquired, `false` otherwise.
  [[nodiscard]] bool try_lock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true);

  /// Deprecated alias for `try_lock()`.
  ///
  /// @return `true` if the mutex was acquired, `false` otherwise.
  ABSL_DEPRECATE_AND_INLINE()
  [[nodiscard]] bool TryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return try_lock();
  }

  /// Requires that the mutex be held exclusively (write mode) by this thread.
  ///
  /// Intended only as a debugging aid; it doesn't guarantee correctness.
  void AssertHeld() const ABSL_ASSERT_EXCLUSIVE_LOCK();

  // ---------------------------------------------------------------------------
  // Reader-Writer Locking
  // ---------------------------------------------------------------------------

  // A Mutex can also be used as a starvation-free reader-writer lock.
  // Neither read-locks nor write-locks are reentrant/recursive to avoid
  // potential client programming errors.
  //
  // The Mutex API provides `Writer*()` aliases for the existing `lock()`,
  // `unlock()` and `try_lock()` methods for use within applications mixing
  // reader/writer locks. Using `*_shared()` and `Writer*()` operations in this
  // manner can make locking behavior clearer when mixing read and write modes.
  //
  // Introducing reader locks necessarily complicates the `Mutex` state
  // machine somewhat. The table below illustrates the allowed state transitions
  // of a mutex in such cases. Note that lock_shared() may block even if the
  // lock is held in shared mode; this occurs when another thread is blocked on
  // a call to lock().
  //
  // ---------------------------------------------------------------------------
  //     Operation: lock()       unlock()  lock_shared() unlock_shared()
  // ---------------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------------
  // Free           Exclusive    invalid   Shared(1)              invalid
  // Shared(1)      blocks       invalid   Shared(2) or blocks    Free
  // Shared(n) n>1  blocks       invalid   Shared(n+1) or blocks  Shared(n-1)
  // Exclusive      blocks       Free      blocks                 invalid
  // ---------------------------------------------------------------------------
  //
  // In comments below, "shared" refers to a state of Shared(n) for any n > 0.

  /// Blocks the calling thread, if necessary, until this `Mutex` is either
  /// free or in shared mode, and then acquires a share of it.
  void lock_shared() ABSL_SHARED_LOCK_FUNCTION();

  /// Deprecated alias for `lock_shared()`.
  ABSL_DEPRECATE_AND_INLINE()
  void ReaderLock() ABSL_SHARED_LOCK_FUNCTION() { lock_shared(); }

  /// Releases a read share of this `Mutex`.
  void unlock_shared() ABSL_UNLOCK_FUNCTION();

  /// Deprecated alias for `unlock_shared()`.
  ABSL_DEPRECATE_AND_INLINE()
  void ReaderUnlock() ABSL_UNLOCK_FUNCTION() { unlock_shared(); }

  /// Tries to acquire this `Mutex` for shared access without blocking.
  ///
  /// @return `true` if the mutex was acquired, `false` otherwise.
  [[nodiscard]] bool try_lock_shared() ABSL_SHARED_TRYLOCK_FUNCTION(true);

  /// Deprecated alias for `try_lock_shared()`.
  ///
  /// @return `true` if the mutex was acquired, `false` otherwise.
  ABSL_DEPRECATE_AND_INLINE()
  [[nodiscard]] bool ReaderTryLock() ABSL_SHARED_TRYLOCK_FUNCTION(true) {
    return try_lock_shared();
  }

  /// Requires that the mutex be held at least in shared mode (read mode) by
  /// this thread.
  ///
  /// Intended only as a debugging aid; it doesn't guarantee correctness.
  void AssertReaderHeld() const ABSL_ASSERT_SHARED_LOCK();

  /// Deprecated writer alias for `lock()`.
  ABSL_DEPRECATE_AND_INLINE()
  void WriterLock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { lock(); }

  /// Deprecated writer alias for `unlock()`.
  ABSL_DEPRECATE_AND_INLINE()
  void WriterUnlock() ABSL_UNLOCK_FUNCTION() { unlock(); }

  /// Deprecated writer alias for `try_lock()`.
  ///
  /// @return `true` if the mutex was acquired, `false` otherwise.
  ABSL_DEPRECATE_AND_INLINE()
  [[nodiscard]] bool WriterTryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return try_lock();
  }

  // ---------------------------------------------------------------------------
  // Conditional Critical Regions
  // ---------------------------------------------------------------------------

  // Conditional usage of a `Mutex` can occur using two distinct paradigms:
  //
  //   * Use of `Mutex` member functions with `Condition` objects.
  //   * Use of the separate `CondVar` abstraction.
  //
  // In general, prefer use of `Condition` and the `Mutex` member functions
  // listed below over `CondVar`. When there are multiple threads waiting on
  // distinctly different conditions, however, a battery of `CondVar`s may be
  // more efficient. This section discusses use of `Condition` objects.
  //
  // `Mutex` contains member functions for performing lock operations only under
  // certain conditions, of class `Condition`. For correctness, the `Condition`
  // must return a boolean that is a pure function, only of state protected by
  // the `Mutex`. The condition must be invariant w.r.t. environmental state
  // such as thread, cpu id, or time, and must be `noexcept`. The condition will
  // always be invoked with the mutex held in at least read mode, so you should
  // not block it for long periods or sleep it on a timer.
  //
  // Since a condition must not depend directly on the current time, use
  // `*WithTimeout()` member function variants to make your condition
  // effectively true after a given duration, or `*WithDeadline()` variants to
  // make your condition effectively true after a given time.
  //
  // The condition function should have no side-effects aside from debug
  // logging; as a special exception, the function may acquire other mutexes
  // provided it releases all those that it acquires.  (This exception was
  // required to allow logging.)

  /// Unlocks this `Mutex` and blocks until `cond` is `true` and the `Mutex`
  /// can be reacquired, then reacquires it in the same mode.
  ///
  /// @param cond The condition to wait for.
  void Await(const Condition& cond) {
    AwaitCommon(cond, synchronization_internal::KernelTimeout::Never());
  }

  /// Blocks until `cond` is `true` and the `Mutex` can be acquired, then
  /// atomically acquires it exclusively.
  ///
  /// @param cond The condition to wait for.
  void LockWhen(const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    LockWhenCommon(cond, synchronization_internal::KernelTimeout::Never(),
                   true);
  }

  /// Blocks until `cond` is `true` and the `Mutex` can be acquired, then
  /// atomically acquires it in shared mode.
  ///
  /// @param cond The condition to wait for.
  void ReaderLockWhen(const Condition& cond) ABSL_SHARED_LOCK_FUNCTION() {
    LockWhenCommon(cond, synchronization_internal::KernelTimeout::Never(),
                   false);
  }

  /// Blocks until `cond` is `true` and the `Mutex` can be acquired, then
  /// atomically acquires it in write (exclusive) mode.
  ///
  /// @param cond The condition to wait for.
  void WriterLockWhen(const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    this->LockWhen(cond);
  }

  // ---------------------------------------------------------------------------
  // Mutex Variants with Timeouts/Deadlines
  // ---------------------------------------------------------------------------

  /// Unlocks this `Mutex` and blocks until `cond` is `true` or the timeout
  /// expires, then reacquires it in the same mode.
  ///
  /// @param cond The condition to wait for.
  /// @param timeout The maximum time to wait.
  /// @return `true` if `cond` is `true` on return.
  bool AwaitWithTimeout(const Condition& cond, absl::Duration timeout) {
    return AwaitCommon(cond, synchronization_internal::KernelTimeout{timeout});
  }

  /// Unlocks this `Mutex` and blocks until `cond` is `true` or the deadline
  /// passes, then reacquires it in the same mode.
  ///
  /// @param cond The condition to wait for.
  /// @param deadline The absolute time after which to stop waiting.
  /// @return `true` if `cond` is `true` on return.
  bool AwaitWithDeadline(const Condition& cond, absl::Time deadline) {
    return AwaitCommon(cond, synchronization_internal::KernelTimeout{deadline});
  }

  /// Blocks until `cond` is `true` or the timeout expires, then atomically
  /// acquires this `Mutex` exclusively.
  ///
  /// @param cond The condition to wait for.
  /// @param timeout The maximum time to wait.
  /// @return `true` if `cond` is `true` on return.
  bool LockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{timeout}, true);
  }
  /// Blocks until `cond` is `true` or the timeout expires, then atomically
  /// acquires this `Mutex` in shared mode.
  ///
  /// @param cond The condition to wait for.
  /// @param timeout The maximum time to wait.
  /// @return `true` if `cond` is `true` on return.
  bool ReaderLockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_SHARED_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{timeout}, false);
  }
  /// Blocks until `cond` is `true` or the timeout expires, then atomically
  /// acquires this `Mutex` in write (exclusive) mode.
  ///
  /// @param cond The condition to wait for.
  /// @param timeout The maximum time to wait.
  /// @return `true` if `cond` is `true` on return.
  bool WriterLockWhenWithTimeout(const Condition& cond, absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return this->LockWhenWithTimeout(cond, timeout);
  }

  /// Blocks until `cond` is `true` or the deadline passes, then atomically
  /// acquires this `Mutex` exclusively.
  ///
  /// @param cond The condition to wait for.
  /// @param deadline The absolute time after which to stop waiting.
  /// @return `true` if `cond` is `true` on return.
  bool LockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{deadline}, true);
  }
  /// Blocks until `cond` is `true` or the deadline passes, then atomically
  /// acquires this `Mutex` in shared mode.
  ///
  /// @param cond The condition to wait for.
  /// @param deadline The absolute time after which to stop waiting.
  /// @return `true` if `cond` is `true` on return.
  bool ReaderLockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_SHARED_LOCK_FUNCTION() {
    return LockWhenCommon(
        cond, synchronization_internal::KernelTimeout{deadline}, false);
  }
  /// Blocks until `cond` is `true` or the deadline passes, then atomically
  /// acquires this `Mutex` in write (exclusive) mode.
  ///
  /// @param cond The condition to wait for.
  /// @param deadline The absolute time after which to stop waiting.
  /// @return `true` if `cond` is `true` on return.
  bool WriterLockWhenWithDeadline(const Condition& cond, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    return this->LockWhenWithDeadline(cond, deadline);
  }

  // ---------------------------------------------------------------------------
  // Debug Support: Invariant Checking, Deadlock Detection, Logging.
  // ---------------------------------------------------------------------------

  /// Registers an invariant predicate to be checked for this `Mutex` when
  /// invariant debugging has been enabled globally.
  ///
  /// @param invariant The predicate to invoke, or null to disable checking.
  /// @param arg The argument passed to `invariant`.
  void EnableInvariantDebugging(
      void (*absl_nullable invariant)(void* absl_nullability_unknown),
      void* absl_nullability_unknown arg);

  /// Causes all subsequent uses of this `Mutex` to be logged.
  ///
  /// @param name Tag applied to log entries.
  void EnableDebugLog(const char* absl_nullable name);

  // Deadlock detection

  /// Forgets any deadlock-detection information previously gathered about this
  /// `Mutex`.
  void ForgetDeadlockInfo();

  /// Asserts that this thread does not hold this `Mutex` in any mode.
  ///
  /// Intended only as a debugging aid; it may report an error or return
  /// immediately.
  void AssertNotHeld() const;

  // Special cases.

  /// A constant that indicates how a lock should be acquired.
  typedef const struct MuHowS* MuHow;

  /// Prepares the `Mutex` implementation for re-entry from within a fatal
  /// signal handler.
  ///
  /// Intended only for last-ditch attempts to log crash information. It must be
  /// invoked from a signal handler that either loops forever or terminates the
  /// process.
  static void InternalAttemptToUseMutexInFatalSignalHandler();

 private:
  std::atomic<intptr_t> mu_;  // The Mutex state.

  // Post()/Wait() versus associated PerThreadSem; in class for required
  // friendship with PerThreadSem.
  static void IncrementSynchSem(Mutex* absl_nonnull mu,
                                base_internal::PerThreadSynch* absl_nonnull w);
  static bool DecrementSynchSem(Mutex* absl_nonnull mu,
                                base_internal::PerThreadSynch* absl_nonnull w,
                                synchronization_internal::KernelTimeout t);

  // slow path acquire
  void LockSlowLoop(SynchWaitParams* absl_nonnull waitp, int flags);
  // wrappers around LockSlowLoop()
  bool LockSlowWithDeadline(MuHow absl_nonnull how,
                            const Condition* absl_nullable cond,
                            synchronization_internal::KernelTimeout t,
                            int flags);
  void LockSlow(MuHow absl_nonnull how, const Condition* absl_nullable cond,
                int flags) ABSL_ATTRIBUTE_COLD;
  // slow path release
  void UnlockSlow(SynchWaitParams* absl_nullable waitp) ABSL_ATTRIBUTE_COLD;
  // TryLock slow path.
  bool TryLockSlow();
  // ReaderTryLock slow path.
  bool ReaderTryLockSlow();
  // Common code between Await() and AwaitWithTimeout/Deadline()
  bool AwaitCommon(const Condition& cond,
                   synchronization_internal::KernelTimeout t);
  bool LockWhenCommon(const Condition& cond,
                      synchronization_internal::KernelTimeout t, bool write);
  // Attempt to remove thread s from queue.
  void TryRemove(base_internal::PerThreadSynch* absl_nonnull s);
  // Block a thread on mutex.
  void Block(base_internal::PerThreadSynch* absl_nonnull s);
  // Wake a thread; return successor.
  base_internal::PerThreadSynch* absl_nullable Wakeup(
      base_internal::PerThreadSynch* absl_nonnull w);
  void Dtor();

  friend class CondVar;                // for access to Trans()/Fer().
  void Trans(MuHow absl_nonnull how);  // used for CondVar->Mutex transfer
  void Fer(base_internal::PerThreadSynch* absl_nonnull
               w);  // used for CondVar->Mutex transfer

  // Catch the error of writing Mutex when intending MutexLock.
  explicit Mutex(const volatile Mutex* absl_nullable /*ignored*/) {}

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

// -----------------------------------------------------------------------------
// Mutex RAII Wrappers
// -----------------------------------------------------------------------------

// MutexLock
//
// `MutexLock` is a helper class, which acquires and releases a `Mutex` via
// RAII.
//
// Example:
//
// Class Foo {
//  public:
//   Foo::Bar* Baz() {
//     MutexLock lock(mu_);
//     ...
//     return bar;
//   }
//
// private:
//   Mutex mu_;
// };
/// RAII helper that acquires and releases a `Mutex` exclusively.
class ABSL_SCOPED_LOCKABLE MutexLock {
 public:
  /// Acquires `mu` exclusively for the lifetime of this object.
  ///
  /// @param mu The mutex to lock.
  explicit MutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    this->mu_.lock();
  }

  /// Deprecated pointer overload; acquires `*mu` exclusively.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit MutexLock(Mutex* absl_nonnull mu) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : MutexLock(*mu) {}

  /// Acquires `mu` exclusively once `cond` holds, for the lifetime of this
  /// object.
  ///
  /// @param mu The mutex to lock.
  /// @param cond The condition to wait for before locking.
  explicit MutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS,
                     const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    this->mu_.LockWhen(cond);
  }

  /// Deprecated pointer overload; acquires `*mu` exclusively once `cond` holds.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  /// @param cond The condition to wait for before locking.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit MutexLock(Mutex* absl_nonnull mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : MutexLock(*mu, cond) {}

  /// Deleted copy constructor.
  MutexLock(const MutexLock& other) = delete;  // NOLINT(runtime/mutex)
  /// Deleted move constructor.
  MutexLock(MutexLock&& other) = delete;       // NOLINT(runtime/mutex)
  /// Deleted copy assignment.
  MutexLock& operator=(const MutexLock& other) = delete;
  /// Deleted move assignment.
  MutexLock& operator=(MutexLock&& other) = delete;

  /// Releases the held mutex.
  ~MutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_.unlock(); }

 private:
  Mutex& mu_;
};

/// RAII helper that acquires and releases a shared (reader) lock on a `Mutex`.
class ABSL_SCOPED_LOCKABLE ReaderMutexLock {
 public:
  /// Acquires a shared lock on `mu` for the lifetime of this object.
  ///
  /// @param mu The mutex to lock.
  explicit ReaderMutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_SHARED_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.lock_shared();
  }

  /// Deprecated pointer overload; acquires a shared lock on `*mu`.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit ReaderMutexLock(Mutex* absl_nonnull mu) ABSL_SHARED_LOCK_FUNCTION(mu)
      : ReaderMutexLock(*mu) {}

  /// Acquires a shared lock on `mu` once `cond` holds.
  ///
  /// @param mu The mutex to lock.
  /// @param cond The condition to wait for before locking.
  explicit ReaderMutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS,
                           const Condition& cond) ABSL_SHARED_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.ReaderLockWhen(cond);
  }

  /// Deprecated pointer overload; acquires a shared lock on `*mu` once `cond`
  /// holds.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  /// @param cond The condition to wait for before locking.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit ReaderMutexLock(Mutex* absl_nonnull mu, const Condition& cond)
      ABSL_SHARED_LOCK_FUNCTION(mu)
      : ReaderMutexLock(*mu, cond) {}

  /// Deleted copy constructor.
  ReaderMutexLock(const ReaderMutexLock& other) = delete;
  /// Deleted move constructor.
  ReaderMutexLock(ReaderMutexLock&& other) = delete;
  /// Deleted copy assignment.
  ReaderMutexLock& operator=(const ReaderMutexLock& other) = delete;
  /// Deleted move assignment.
  ReaderMutexLock& operator=(ReaderMutexLock&& other) = delete;

  /// Releases the held shared lock.
  ~ReaderMutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_.unlock_shared(); }

 private:
  Mutex& mu_;
};

/// RAII helper that acquires and releases a write (exclusive) lock on a
/// `Mutex`.
class ABSL_SCOPED_LOCKABLE WriterMutexLock {
 public:
  /// Acquires a write lock on `mu` for the lifetime of this object.
  ///
  /// @param mu The mutex to lock.
  explicit WriterMutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.lock();
  }

  /// Deprecated pointer overload; acquires a write lock on `*mu`.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit WriterMutexLock(Mutex* absl_nonnull mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : WriterMutexLock(*mu) {}

  /// Acquires a write lock on `mu` once `cond` holds.
  ///
  /// @param mu The mutex to lock.
  /// @param cond The condition to wait for before locking.
  explicit WriterMutexLock(Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS,
                           const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    mu.WriterLockWhen(cond);
  }

  /// Deprecated pointer overload; acquires a write lock on `*mu` once `cond`
  /// holds.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  /// @param cond The condition to wait for before locking.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit WriterMutexLock(Mutex* absl_nonnull mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : WriterMutexLock(*mu, cond) {}

  /// Deleted copy constructor.
  WriterMutexLock(const WriterMutexLock& other) = delete;
  /// Deleted move constructor.
  WriterMutexLock(WriterMutexLock&& other) = delete;
  /// Deleted copy assignment.
  WriterMutexLock& operator=(const WriterMutexLock& other) = delete;
  /// Deleted move assignment.
  WriterMutexLock& operator=(WriterMutexLock&& other) = delete;

  /// Releases the held write lock.
  ~WriterMutexLock() ABSL_UNLOCK_FUNCTION() { this->mu_.unlock(); }

 private:
  Mutex& mu_;
};

// -----------------------------------------------------------------------------
// Condition
// -----------------------------------------------------------------------------
//
// `Mutex` contains a number of member functions which take a `Condition` as an
// argument; clients can wait for conditions to become `true` before attempting
// to acquire the mutex. These sections are known as "condition critical"
// sections. To use a `Condition`, you simply need to construct it, and use
// within an appropriate `Mutex` member function; everything else in the
// `Condition` class is an implementation detail.
//
// A `Condition` is specified as a function pointer which returns a boolean.
// `Condition` functions should be pure functions -- their results should depend
// only on passed arguments, should not consult any external state (such as
// clocks), and should have no side-effects, aside from debug logging. Any
// objects that the function may access should be limited to those which are
// constant while the mutex is blocked on the condition (e.g. a stack variable),
// or objects of state protected explicitly by the mutex.
//
// No matter which construction is used for `Condition`, the underlying
// function pointer / functor / callable must not throw any
// exceptions. Correctness of `Mutex` / `Condition` is not guaranteed in
// the face of a throwing `Condition`. (When Abseil is allowed to depend
// on C++17, these function pointers will be explicitly marked
// `noexcept`; until then this requirement cannot be enforced in the
// type system.)
//
// Note: to use a `Condition`, you need only construct it and pass it to a
// suitable `Mutex' member function, such as `Mutex::Await()`, or to the
// constructor of one of the scope guard classes.
//
// Example using LockWhen/Unlock:
//
//   // assume count_ is not internal reference count
//   int count_ ABSL_GUARDED_BY(mu_);
//   Condition count_is_zero(+[](int *count) { return *count == 0; }, &count_);
//
//   mu_.LockWhen(count_is_zero);
//   // ...
//   mu_.Unlock();
//
// Example using a scope guard:
//
//   {
//     MutexLock lock(mu_, count_is_zero);
//     // ...
//   }
//
// When multiple threads are waiting on exactly the same condition, make sure
// that they are constructed with the same parameters (same pointer to function
// + arg, or same pointer to object + method), so that the mutex implementation
// can avoid redundantly evaluating the same condition for each thread.

/// A predicate on state protected by a `Mutex`, used to wait for a condition
/// to become `true`.
class Condition {
 public:
  /// Constructs a `Condition` that returns the result of `(*func)(arg)`.
  ///
  /// @param func The predicate function to evaluate.
  /// @param arg The argument passed to `func`.
  Condition(bool (*absl_nonnull func)(void* absl_nullability_unknown),
            void* absl_nullability_unknown arg);

  /// Constructs a `Condition` from a typed function pointer and argument.
  ///
  /// To use a lambda, prepend it with unary plus to convert it into a function
  /// pointer.
  ///
  /// @param func The predicate function to evaluate.
  /// @param arg The argument passed to `func`.
  template <typename T>
  Condition(bool (*absl_nonnull func)(T* absl_nullability_unknown),
            T* absl_nullability_unknown arg);

  /// Constructs a `Condition` allowing `arg` to be implicitly convertible to
  /// the function parameter type.
  ///
  /// @param func The predicate function to evaluate.
  /// @param arg The argument passed to `func`.
  template <typename T, typename = void>
  Condition(
      bool (*absl_nonnull func)(T* absl_nullability_unknown),
      typename absl::type_identity<T>::type* absl_nullability_unknown
          arg);

  /// Constructs a `Condition` that evaluates `object->method()`.
  ///
  /// @param object The object on which to invoke the method.
  /// @param method The member function to evaluate.
  template <typename T>
  Condition(
      T* absl_nonnull object,
      bool (absl::type_identity<T>::type::* absl_nonnull method)());

  /// Constructs a `Condition` that evaluates a const member function.
  ///
  /// @param object The object on which to invoke the method.
  /// @param method The const member function to evaluate.
  template <typename T>
  Condition(
      const T* absl_nonnull object,
      bool (absl::type_identity<T>::type::* absl_nonnull method)()
          const);

  /// Constructs a `Condition` that returns the value of `*cond`.
  ///
  /// @param cond Pointer to the boolean to read.
  explicit Condition(const bool* absl_nonnull cond);

  // Templated version for invoking a functor that returns a `bool`.
  // This approach accepts pointers to non-mutable lambdas, `std::function`,
  // the result of` std::bind` and user-defined functors that define
  // `bool F::operator()() const`.
  //
  // Example:
  //
  //   auto reached = [this, current]() {
  //     mu_.AssertReaderHeld();                // For annotalysis.
  //     return processed_ >= current;
  //   };
  //   mu_.Await(Condition(&reached));
  //
  // NOTE: never use "mu_.AssertHeld()" instead of "mu_.AssertReaderHeld()" in
  // the lambda as it may be called when the mutex is being unlocked from a
  // scope holding only a reader lock, which will make the assertion not
  // fulfilled and crash the binary.

  // See class comment for performance advice. In particular, if there
  // might be more than one waiter for the same condition, make sure
  // that all waiters construct the condition with the same pointers.

  /// Constructs a `Condition` from a functor exposing `bool operator() const`.
  ///
  /// @param obj The functor to evaluate.
  template <typename T,
            std::enable_if_t<
                synchronization_internal::HasConstMemberCallOperator<T>::value,
                int> = 0>
  explicit Condition(const T* absl_nonnull obj)
      : Condition(obj, static_cast<bool (T::*)() const>(&T::operator())) {}

  /// Constructs a `Condition` from a functor that does not match the
  /// `bool operator()() const` signature.
  ///
  /// @param obj The functor to evaluate.
  template <
      typename T,
      typename = std::enable_if_t<
          !synchronization_internal::HasConstMemberCallOperator<T>::value &&
          sizeof(static_cast<bool (*)(const T&)>(&T::operator())) != 0>>
  explicit Condition(const T* absl_nonnull obj)
      : Condition(&CallByRef<T>, obj) {}

  /// A `Condition` that always returns `true`.
  ABSL_CONST_INIT static const Condition kTrue;

  /// Evaluates the condition.
  ///
  /// @return The current value of the predicate.
  bool Eval() const;

  /// Determines whether two conditions are guaranteed to evaluate equally.
  ///
  /// @param a The first condition, or null for a `true` condition.
  /// @param b The second condition, or null for a `true` condition.
  /// @return `true` if both are guaranteed to return the same value, `false`
  ///   if they may differ.
  static bool GuaranteedEqual(const Condition* absl_nullable a,
                              const Condition* absl_nullable b);

 private:
  // Sizing an allocation for a method pointer can be subtle. In the Itanium
  // specifications, a method pointer has a predictable, uniform size. On the
  // other hand, MSVC ABI, method pointer sizes vary based on the
  // inheritance of the class. Specifically, method pointers from classes with
  // multiple inheritance are bigger than those of classes with single
  // inheritance. Other variations also exist.

#ifndef _MSC_VER
  // Allocation for a function pointer or method pointer.
  // The {0} initializer ensures that all unused bytes of this buffer are
  // always zeroed out.  This is necessary, because GuaranteedEqual() compares
  // all of the bytes, unaware of which bytes are relevant to a given `eval_`.
  using MethodPtr = bool (Condition::*)();
  char callback_[sizeof(MethodPtr)] = {0};
#else
  // It is well known that the larget MSVC pointer-to-member is 24 bytes. This
  // may be the largest known pointer-to-member of any platform. For this
  // reason we will allocate 24 bytes for MSVC platform toolchains.
  char callback_[24] = {0};
#endif

  // Function with which to evaluate callbacks and/or arguments.
  bool (*absl_nullable eval_)(const Condition* absl_nonnull) = nullptr;

  // Either an argument for a function call or an object for a method call.
  void* absl_nullable arg_ = nullptr;

  // Various functions eval_ can point to:
  static bool CallVoidPtrFunction(const Condition* absl_nonnull c);
  template <typename T>
  static bool CastAndCallFunction(const Condition* absl_nonnull c);
  template <typename T, typename ConditionMethodPtr>
  static bool CastAndCallMethod(const Condition* absl_nonnull c);

  template <typename T>
  static bool CallByRef(const T* absl_nonnull self) {
    return (*self)();
  }

  // Helper methods for storing, validating, and reading callback arguments.
  template <typename T>
  void StoreCallback(T callback) {
    static_assert(
        sizeof(callback) <= sizeof(callback_),
        "An overlarge pointer was passed as a callback to Condition.");
    std::memcpy(callback_, &callback, sizeof(callback));
  }

  template <typename T>
  void ReadCallback(T* absl_nonnull callback) const {
    std::memcpy(callback, callback_, sizeof(*callback));
  }

  static bool AlwaysTrue(const Condition* absl_nullable) { return true; }

  // Used only to create kTrue.
  constexpr Condition() : eval_(AlwaysTrue), arg_(nullptr) {}
};

// -----------------------------------------------------------------------------
// CondVar
// -----------------------------------------------------------------------------
//
// A condition variable, reflecting state evaluated separately outside of the
// `Mutex` object, which can be signaled to wake callers.
// This class is not normally needed; use `Mutex` member functions such as
// `Mutex::Await()` and intrinsic `Condition` abstractions. In rare cases
// with many threads and many conditions, `CondVar` may be faster.
//
// The implementation may deliver signals to any condition variable at
// any time, even when no call to `Signal()` or `SignalAll()` is made; as a
// result, upon being awoken, you must check the logical condition you have
// been waiting upon.
//
// Examples:
//
// Usage for a thread waiting for some condition C protected by mutex mu:
//       mu.Lock();
//       while (!C) { cv->Wait(&mu); }        // releases and reacquires mu
//       //  C holds; process data
//       mu.Unlock();
//
// Usage to wake T is:
//       mu.Lock();
//       // process data, possibly establishing C
//       if (C) { cv->Signal(); }
//       mu.Unlock();
//
// If C may be useful to more than one waiter, use `SignalAll()` instead of
// `Signal()`.
//
// With this implementation it is efficient to use `Signal()/SignalAll()` inside
// the locked region; this usage can make reasoning about your program easier.
//
/// A condition variable that can be signaled to wake threads waiting on state
/// evaluated outside a `Mutex`.
class CondVar {
 public:
  /// Constructs a `CondVar` allocated on the heap or the stack.
  CondVar();

  /// Atomically releases `mu` and blocks until signaled, then reacquires it.
  ///
  /// @param mu The mutex held by the calling thread.
  void Wait(Mutex* absl_nonnull mu) {
    WaitCommon(mu, synchronization_internal::KernelTimeout::Never());
  }

  /// Atomically releases `mu` and blocks until signaled or the timeout
  /// expires, then reacquires it.
  ///
  /// @param mu The mutex held by the calling thread.
  /// @param timeout The maximum time to wait.
  /// @return `true` if the timeout expired without this `CondVar` being
  ///   signaled.
  bool WaitWithTimeout(Mutex* absl_nonnull mu, absl::Duration timeout) {
    return WaitCommon(mu, synchronization_internal::KernelTimeout(timeout));
  }

  /// Atomically releases `mu` and blocks until signaled or the deadline
  /// passes, then reacquires it.
  ///
  /// @param mu The mutex held by the calling thread.
  /// @param deadline The absolute time after which to stop waiting.
  /// @return `true` if the deadline passed without this `CondVar` being
  ///   signaled.
  bool WaitWithDeadline(Mutex* absl_nonnull mu, absl::Time deadline) {
    return WaitCommon(mu, synchronization_internal::KernelTimeout(deadline));
  }

  /// Signals this `CondVar`, waking at least one waiter if one exists.
  void Signal();

  /// Signals this `CondVar`, waking all waiters.
  void SignalAll();

  /// Causes all subsequent uses of this `CondVar` to be logged.
  ///
  /// @param name Tag applied to log entries.
  void EnableDebugLog(const char* absl_nullable name);

 private:
  bool WaitCommon(Mutex* absl_nonnull mutex,
                  synchronization_internal::KernelTimeout t);
  void Remove(base_internal::PerThreadSynch* absl_nonnull s);
  std::atomic<intptr_t> cv_;  // Condition variable state.
  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;
};

// Variants of MutexLock.
//
// If you find yourself using one of these, consider instead using
// Mutex::Unlock() and/or if-statements for clarity.

/// RAII helper like `MutexLock`, but a no-op when the mutex pointer is null.
class ABSL_SCOPED_LOCKABLE MutexLockMaybe {
 public:
  /// Acquires `mu` exclusively if it is non-null.
  ///
  /// @param mu The mutex to lock, or null for a no-op.
  explicit MutexLockMaybe(Mutex* absl_nullable mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (this->mu_ != nullptr) {
      this->mu_->lock();
    }
  }

  /// Acquires `mu` exclusively once `cond` holds if it is non-null.
  ///
  /// @param mu The mutex to lock, or null for a no-op.
  /// @param cond The condition to wait for before locking.
  explicit MutexLockMaybe(Mutex* absl_nullable mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    if (this->mu_ != nullptr) {
      this->mu_->LockWhen(cond);
    }
  }

  /// Releases the held mutex if one was acquired.
  ~MutexLockMaybe() ABSL_UNLOCK_FUNCTION() {
    if (this->mu_ != nullptr) {
      this->mu_->unlock();
    }
  }

 private:
  Mutex* absl_nullable const mu_;
  MutexLockMaybe(const MutexLockMaybe&) = delete;
  MutexLockMaybe(MutexLockMaybe&&) = delete;
  MutexLockMaybe& operator=(const MutexLockMaybe&) = delete;
  MutexLockMaybe& operator=(MutexLockMaybe&&) = delete;
};

/// RAII helper like `MutexLock` that also permits releasing the mutex before
/// destruction.
class ABSL_SCOPED_LOCKABLE ReleasableMutexLock {
 public:
  /// Acquires `mu` exclusively for the lifetime of this object.
  ///
  /// @param mu The mutex to lock.
  explicit ReleasableMutexLock(
      Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(&mu) {
    this->mu_->lock();
  }

  /// Deprecated pointer overload; acquires `*mu` exclusively.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit ReleasableMutexLock(Mutex* absl_nonnull mu)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : ReleasableMutexLock(*mu) {}

  /// Acquires `mu` exclusively once `cond` holds.
  ///
  /// @param mu The mutex to lock.
  /// @param cond The condition to wait for before locking.
  explicit ReleasableMutexLock(
      Mutex& mu ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS,
      const Condition& cond) ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(&mu) {
    this->mu_->LockWhen(cond);
  }

  /// Deprecated pointer overload; acquires `*mu` exclusively once `cond` holds.
  ///
  /// @param mu The mutex to lock; must be dereferenceable.
  /// @param cond The condition to wait for before locking.
  [[deprecated("Use the constructor that takes a reference instead")]]
  ABSL_REFACTOR_INLINE
  explicit ReleasableMutexLock(Mutex* absl_nonnull mu, const Condition& cond)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : ReleasableMutexLock(*mu, cond) {}

  /// Releases the held mutex if it has not already been released.
  ~ReleasableMutexLock() ABSL_UNLOCK_FUNCTION() {
    if (this->mu_ != nullptr) {
      this->mu_->unlock();
    }
  }

  /// Releases the held mutex before destruction. May be called at most once.
  void Release() ABSL_UNLOCK_FUNCTION();

 private:
  Mutex* absl_nullable mu_;
  ReleasableMutexLock(const ReleasableMutexLock&) = delete;
  ReleasableMutexLock(ReleasableMutexLock&&) = delete;
  ReleasableMutexLock& operator=(const ReleasableMutexLock&) = delete;
  ReleasableMutexLock& operator=(ReleasableMutexLock&&) = delete;
};

inline Mutex::Mutex() : mu_(0) {
  ABSL_TSAN_MUTEX_CREATE(this, __tsan_mutex_not_static);
}

constexpr Mutex::Mutex(absl::ConstInitType) : mu_(0) {}

#if !defined(__APPLE__) && !defined(ABSL_BUILD_DLL)
ABSL_ATTRIBUTE_ALWAYS_INLINE
inline Mutex::~Mutex() { Dtor(); }
#endif

#if defined(NDEBUG) && !defined(ABSL_HAVE_THREAD_SANITIZER) && \
    !defined(ABSL_BUILD_DLL)
// Under NDEBUG and without TSAN, Dtor is normally fully inlined for
// performance. However, when building Abseil as a shared library
// (ABSL_BUILD_DLL), we must provide an out-of-line definition. This ensures the
// Mutex::Dtor symbol is exported from the DLL, maintaining ABI compatibility
// with clients that might be built in debug mode and thus expect the symbol.
ABSL_ATTRIBUTE_ALWAYS_INLINE
inline void Mutex::Dtor() {}
#endif

inline CondVar::CondVar() : cv_(0) {}

// static
template <typename T, typename ConditionMethodPtr>
bool Condition::CastAndCallMethod(const Condition* absl_nonnull c) {
  T* object = static_cast<T*>(c->arg_);
  ConditionMethodPtr condition_method_pointer;
  c->ReadCallback(&condition_method_pointer);
  return (object->*condition_method_pointer)();
}

// static
template <typename T>
bool Condition::CastAndCallFunction(const Condition* absl_nonnull c) {
  bool (*function)(T*);
  c->ReadCallback(&function);
  T* argument = static_cast<T*>(c->arg_);
  return (*function)(argument);
}

template <typename T>
inline Condition::Condition(
    bool (*absl_nonnull func)(T* absl_nullability_unknown),
    T* absl_nullability_unknown arg)
    : eval_(&CastAndCallFunction<T>),
      arg_(const_cast<void*>(static_cast<const void*>(arg))) {
  static_assert(sizeof(&func) <= sizeof(callback_),
                "An overlarge function pointer was passed to Condition.");
  StoreCallback(func);
}

template <typename T, typename>
inline Condition::Condition(
    bool (*absl_nonnull func)(T* absl_nullability_unknown),
    typename absl::type_identity<T>::type* absl_nullability_unknown
        arg)
    // Just delegate to the overload above.
    : Condition(func, arg) {}

template <typename T>
inline Condition::Condition(
    T* absl_nonnull object,
    bool (absl::type_identity<T>::type::* absl_nonnull method)())
    : eval_(&CastAndCallMethod<T, decltype(method)>), arg_(object) {
  static_assert(sizeof(&method) <= sizeof(callback_),
                "An overlarge method pointer was passed to Condition.");
  StoreCallback(method);
}

template <typename T>
inline Condition::Condition(
    const T* absl_nonnull object,
    bool (absl::type_identity<T>::type::* absl_nonnull method)()
        const)
    : eval_(&CastAndCallMethod<const T, decltype(method)>),
      arg_(reinterpret_cast<void*>(const_cast<T*>(object))) {
  StoreCallback(method);
}

/// Registers a hook for mutex contention profiling.
///
/// Only a single profiler can be installed in a running binary.
///
/// @param fn Callback invoked with the number of wait cycles on contention.
void RegisterMutexProfiler(void (*absl_nonnull fn)(int64_t wait_cycles));

/// Registers a hook for `Mutex` tracing.
///
/// This has the same ordering and single-use limitations as
/// `RegisterMutexProfiler()`.
///
/// @param fn Callback invoked with an event name, an opaque mutex handle, and
///   the number of wait cycles.
void RegisterMutexTracer(void (*absl_nonnull fn)(const char* absl_nonnull msg,
                                                 const void* absl_nonnull obj,
                                                 int64_t wait_cycles));

/// Registers a hook for `CondVar` tracing.
///
/// This is thread-safe, but only a single tracer can be registered.
///
/// @param fn Callback invoked with an event name and an opaque `CondVar`
///   handle.
void RegisterCondVarTracer(void (*absl_nonnull fn)(
    const char* absl_nonnull msg, const void* absl_nonnull cv));

/// Enables or disables global support for `Mutex` invariant debugging.
///
/// @param enabled Whether invariant debugging should be enabled.
void EnableMutexInvariantDebugging(bool enabled);

// When in debug mode, and when the feature has been enabled globally, the
// implementation will keep track of lock ordering and complain (or optionally
// crash) if a cycle is detected in the acquired-before graph.

/// Possible modes of operation for the deadlock detector in debug mode.
enum class OnDeadlockCycle {
  kIgnore,  ///< Neither report on nor track cycles in lock ordering.
  kReport,  ///< Report lock cycles to stderr when detected.
  kAbort,   ///< Report lock cycles to stderr when detected, then abort.
};

/// Enables or disables global detection of potential deadlocks due to `Mutex`
/// lock ordering inversions.
///
/// @param mode How detected lock-ordering cycles should be reported.
void SetMutexDeadlockDetectionMode(OnDeadlockCycle mode);

ABSL_NAMESPACE_END
}  // namespace absl

// In some build configurations we pass --detect-odr-violations to the
// gold linker.  This causes it to flag weak symbol overrides as ODR
// violations.  Because ODR only applies to C++ and not C,
// --detect-odr-violations ignores symbols not mangled with C++ names.
// By changing our extension points to be extern "C", we dodge this
// check.
extern "C" {
void ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)();
}  // extern "C"

#endif  // ABSL_SYNCHRONIZATION_MUTEX_H_
