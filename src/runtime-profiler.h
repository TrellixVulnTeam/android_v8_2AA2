// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_RUNTIME_PROFILER_H_
#define V8_RUNTIME_PROFILER_H_

#include "allocation.h"
#include "atomicops.h"

namespace v8 {
namespace internal {

class Isolate;
class JSFunction;
class Object;
class PendingListNode;
class Semaphore;


enum SamplerState {
  IN_NON_JS_STATE = 0,
  IN_JS_STATE = 1
};


class RuntimeProfiler {
 public:
  explicit RuntimeProfiler(Isolate* isolate);

  static bool IsEnabled();

  void OptimizeNow();
  void OptimizeSoon(JSFunction* function);

  void NotifyTick();

  void Setup();
  void Reset();
  void TearDown();

  Object** SamplerWindowAddress();
  int SamplerWindowSize();

  // Rate limiting support.

  // VM thread interface.
  //
  // Called by isolates when their states change.
  static inline void IsolateEnteredJS(Isolate* isolate);
  static inline void IsolateExitedJS(Isolate* isolate);

  // Profiler thread interface.
  //
  // IsSomeIsolateInJS():
  // The profiler thread can query whether some isolate is currently
  // running JavaScript code.
  //
  // WaitForSomeIsolateToEnterJS():
  // When no isolates are running JavaScript code for some time the
  // profiler thread suspends itself by calling the wait function. The
  // wait function returns true after it waited or false immediately.
  // While the function was waiting the profiler may have been
  // disabled so it *must check* whether it is allowed to continue.
  static bool IsSomeIsolateInJS();
  static bool WaitForSomeIsolateToEnterJS();

  // When shutting down we join the profiler thread. Doing so while
  // it's waiting on a semaphore will cause a deadlock, so we have to
  // wake it up first.
  static void WakeUpRuntimeProfilerThreadBeforeShutdown();

  void UpdateSamplesAfterScavenge();
  void RemoveDeadSamples();
  void UpdateSamplesAfterCompact(ObjectVisitor* visitor);

 private:
  static const int kSamplerWindowSize = 16;
  static const int kStateWindowSize = 128;

  static void HandleWakeUp(Isolate* isolate);

  void Optimize(JSFunction* function, bool eager, int delay);

  void AttemptOnStackReplacement(JSFunction* function);

  void ClearSampleBuffer();

  void ClearSampleBufferNewSpaceEntries();

  int LookupSample(JSFunction* function);

  void AddSample(JSFunction* function, int weight);

#ifdef ENABLE_LOGGING_AND_PROFILING
  void UpdateStateRatio(SamplerState current_state);
#endif

  Isolate* isolate_;

  int sampler_threshold_;
  int sampler_threshold_size_factor_;
  int sampler_ticks_until_threshold_adjustment_;

  // The ratio of ticks spent in JS code in percent.
  Atomic32 js_ratio_;

  Object* sampler_window_[kSamplerWindowSize];
  int sampler_window_position_;
  int sampler_window_weight_[kSamplerWindowSize];

  // Support for pending 'optimize soon' requests.
  PendingListNode* optimize_soon_list_;

  SamplerState state_window_[kStateWindowSize];
  int state_window_position_;
  int state_counts_[2];

  // Possible state values:
  //   -1            => the profiler thread is waiting on the semaphore
  //   0 or positive => the number of isolates running JavaScript code.
  static Atomic32 state_;
  static Semaphore* semaphore_;
};


// Rate limiter intended to be used in the profiler thread.
class RuntimeProfilerRateLimiter BASE_EMBEDDED {
 public:
  RuntimeProfilerRateLimiter() : non_js_ticks_(0) { }

  // Suspends the current thread (which must be the profiler thread)
  // when not executing JavaScript to minimize CPU usage. Returns
  // whether the thread was suspended (and so must check whether
  // profiling is still active.)
  //
  // Does nothing when runtime profiling is not enabled.
  bool SuspendIfNecessary();

 private:
  int non_js_ticks_;

  DISALLOW_COPY_AND_ASSIGN(RuntimeProfilerRateLimiter);
};


// Implementation of RuntimeProfiler inline functions.

void RuntimeProfiler::IsolateEnteredJS(Isolate* isolate) {
  Atomic32 new_state = NoBarrier_AtomicIncrement(&state_, 1);
  if (new_state == 0) {
    // Just incremented from -1 to 0. -1 can only be set by the
    // profiler thread before it suspends itself and starts waiting on
    // the semaphore.
    HandleWakeUp(isolate);
  }
  ASSERT(new_state >= 0);
}


void RuntimeProfiler::IsolateExitedJS(Isolate* isolate) {
  Atomic32 new_state = NoBarrier_AtomicIncrement(&state_, -1);
  ASSERT(new_state >= 0);
  USE(new_state);
}

} }  // namespace v8::internal

#endif  // V8_RUNTIME_PROFILER_H_