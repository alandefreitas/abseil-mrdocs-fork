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
// blocking_counter.h
// -----------------------------------------------------------------------------

#ifndef ABSL_SYNCHRONIZATION_BLOCKING_COUNTER_H_
#define ABSL_SYNCHRONIZATION_BLOCKING_COUNTER_H_

#include <atomic>

#include "absl/base/internal/tracing.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

/// A counter that lets a thread block until a number of actions complete.
///
/// This class allows a thread to block for a pre-specified number of actions.
/// `BlockingCounter` maintains a single non-negative abstract integer "count"
/// with an initial value `initial_count`. A thread can then call `Wait()` on
/// this blocking counter to block until the specified number of events occur;
/// worker threads then call `DecrementCount()` on the counter upon completion of
/// their work. Once the counter's internal "count" reaches zero, the blocked
/// thread unblocks.
///
/// A `BlockingCounter` requires the following:
///     - its `initial_count` is non-negative.
///     - the number of calls to `DecrementCount()` on it is at most
///       `initial_count`.
///     - `Wait()` is called at most once on it.
///
/// Given the above requirements, a `BlockingCounter` provides the following
/// guarantees:
///     - Once its internal "count" reaches zero, no legal action on the object
///       can further change the value of "count".
///     - When `Wait()` returns, it is legal to destroy the `BlockingCounter`.
///     - When `Wait()` returns, the number of calls to `DecrementCount()` on
///       this blocking counter exactly equals `initial_count`.
///
/// Example:
/// @code
///     BlockingCounter bcount(N);         // there are N items of work
///     ... Allow worker threads to start.
///     ... On completing each work item, workers do:
///     ... bcount.DecrementCount();      // an item of work has been completed
///
///     bcount.Wait();                    // wait for all work to be complete
/// @endcode
class BlockingCounter {
 public:
  /// Constructs a blocking counter with an initial count.
  ///
  /// @param initial_count The non-negative number of `DecrementCount()` calls
  ///   required before `Wait()` unblocks.
  explicit BlockingCounter(int initial_count);

  /// Deleted copy constructor; `BlockingCounter` is not copyable.
  BlockingCounter(const BlockingCounter& other) = delete;

  /// Deleted copy assignment; `BlockingCounter` is not copyable.
  BlockingCounter& operator=(const BlockingCounter& other) = delete;

  /// Decrements the counter by one.
  ///
  /// Decrements the counter's "count" by one, and returns "count == 0". This
  /// function requires that "count != 0" when it is called.
  ///
  /// Memory ordering: For any threads X and Y, any action taken by X
  /// before it calls `DecrementCount()` is visible to thread Y after
  /// Y's call to `DecrementCount()`, provided Y's call returns `true`.
  ///
  /// @return `true` if the counter reached zero as a result of this call.
  bool DecrementCount();

  /// Blocks until the counter reaches zero.
  ///
  /// Blocks until the counter reaches zero. This function may be called at most
  /// once. On return, `DecrementCount()` will have been called "initial_count"
  /// times and the blocking counter may be destroyed.
  ///
  /// Memory ordering: For any threads X and Y, any action taken by X
  /// before X calls `DecrementCount()` is visible to Y after Y returns
  /// from `Wait()`.
  void Wait();

 private:
  // Convenience helper to reduce verbosity at call sites.
  static constexpr base_internal::ObjectKind TraceObjectKind() {
    return base_internal::ObjectKind::kBlockingCounter;
  }

  Mutex lock_;
  std::atomic<int> count_;
  int num_waiting_ ABSL_GUARDED_BY(lock_);
  bool done_ ABSL_GUARDED_BY(lock_);
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_BLOCKING_COUNTER_H_
