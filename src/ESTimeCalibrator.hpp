//
//  ESTimeCalibrator.hpp
//
//  Created by Steve Pucci 06 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

#ifndef _ESTIMECALIBRATOR_HPP_
#define _ESTIMECALIBRATOR_HPP_

#include "ESTime.hpp"

#if ES_ANDROID_USE_NTP

// Note [spucci 2023/11/05]: This code was never used: On Android a kernel bug
// meant that continuous time wasn't continuous across a device sleep, so we
// couldn't use it, and on iOS we never tried to implement it as mostly iOS is
// extremely accurate these days anyway.

// This class is notified every time a successful sync has been completed, with
// the current (continuous) skew value.  On being notified, the class goes and
// gets the latest offset values, and calculates data for the time since the
// previous successful sync:
//   - How much the skew moved in that period
//   - The error in that skew delta
//   - How much time has elapsed total
//   - How much of the elapsed time was spent suspended
// Once a sufficient number of data points has been collected, the class moves
// into active mode, resetting the skew periodically to keep it moving smoothly
// (often enough so that it doesn't move more than 0.05s at a time).
// Eventually we can stop doing syncs so often if we get confidence in the active
// drift correction.
class ESTimeCalibrator {
  public:
    static void             init(bool rebootDetected);
    static void             notifySuccessfulSkew(ESTimeInterval contSkew, ESTimeInterval skewError);
};

#endif  // ES_ANDROID_USE_NTP

#endif  // _ESTIMECALIBRATOR_HPP_
