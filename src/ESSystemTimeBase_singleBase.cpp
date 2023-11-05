//
//  ESSystemTimeBase_singleBase.cpp
//
//  Created by Steve Pucci 16 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

// Code common to platforms (e.g., Android Wear) which want to pretend there's only a single time base.

#include "ESTime.hpp"
#include "ESSystemTimeBase.hpp"
#if !ES_TRIPLEBASE

#include "ESUtil.hpp"

#undef ESTRACE
#include "ESTrace.hpp"

#include "assert.h"

// Class static variables
/*static*/ bool            ESSystemTimeBase::_usingAltTime;
/*static*/ ESTimeInterval  ESSystemTimeBase::_altOffset;              //  (systemTime - systemAltTime) aka EC mediaOffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousOffset;       //  (continuousTime - systemTime) aka EC dateROffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousAltOffset;    //  (continuousTime - systemAltTime) aka EC mediaROffset
/*static*/ bool            ESSystemTimeBase::_continuousTimeStableAtStart = false;
/*static*/ bool            ESSystemTimeBase::_isSingleTimeBase = true;

/*static*/ void
ESSystemTimeBase::systemBaseChange() {  // iOS getting a new cell-tower time; NSDate changes but CACurrentMediaTime doesn't
    // Do nothing
}

/*static*/ void
ESSystemTimeBase::goingToSleep() {  // iOS going to sleep: Go back to using NSDate because CAMediaTime is stopped while asleep
    // Do nothing
}

/*static*/ void
ESSystemTimeBase::wakingUp() {  // iOS waking up
    // Do nothing
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentSystemTime() {
    struct timespec ts;
    int st = clock_gettime(CLOCK_REALTIME, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1E9;
    //ESErr::logInfo("ESSystemTimeBase", "The system time is %.9f (=> %.9f)\n", t, t - ESTIME_EPOCH);
    return t - ESTIME_EPOCH;
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentSystemAltTime() {
    return currentSystemTime();
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentContinuousSystemTime() {
    return currentSystemTime();
}

/*static*/ ESTimeInterval
ESSystemTimeBase::init() {
    ESTimeInterval sysTime = currentSystemTime();
    _usingAltTime = false;
    _altOffset = 0;
    _continuousOffset = 0;
    _continuousAltOffset = 0;
    _continuousTimeStableAtStart = false;
    return sysTime;
}

/*static*/ void 
ESSystemTimeBase::getTimesSinceBoot(ESTimeInterval *bootTimeAsSystemTime, ESTimeInterval *suspendedTimeSinceBoot) {
    *bootTimeAsSystemTime = 0;
    *suspendedTimeSinceBoot = 0;
}

#endif  // !ES_TRIPLEBASE
