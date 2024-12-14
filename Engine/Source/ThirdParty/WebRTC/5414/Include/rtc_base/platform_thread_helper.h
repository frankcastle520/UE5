/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_PLATFORM_THREAD_HELPER_H_
#define RTC_BASE_PLATFORM_THREAD_HELPER_H_

#include "rtc_base/platform_thread_types.h"

namespace rtc {

class PlatformThreadHelper final {
public:
  using SetCurrentThreadAffinityMaskCallback = bool (*) (ThreadAffinityMask new_affinity_mask);
  static bool Initialize(SetCurrentThreadAffinityMaskCallback callback);
  static void Shutdown();

  static bool SetCurrentThreadAffinityMask(ThreadAffinityMask affinity_mask);
private:
  static SetCurrentThreadAffinityMaskCallback set_current_thread_affinity_mask_callback;
};

}  // namespace rtc

#endif  // RTC_BASE_PLATFORM_THREAD_HELPER_H_
