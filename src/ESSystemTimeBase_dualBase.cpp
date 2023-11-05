//
//  ESSystemTimeBase_dualBase.cpp
//
//  Created by Steve Pucci 13 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

// Code common to platforms (e.g., iOS and android) which have two underlying time bases

#include "ESTime.hpp"
#include "ESSystemTimeBase.hpp"
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
/*static*/ bool            ESSystemTimeBase::_isSingleTimeBase = false;

/*static*/ void
ESSystemTimeBase::systemBaseChange() {  // iOS getting a new cell-tower time; NSDate changes but CACurrentMediaTime doesn't
    if (_usingAltTime) {  // Otherwise we're asleep and we can't learn anything from the time change
        ESTimeInterval sysTime = currentSystemTime();
        ESTimeInterval altTime = currentSystemAltTime();
        _altOffset = sysTime - altTime;
        _continuousOffset = _continuousAltOffset - _altOffset;
        ESTime::notifyContinuousOffsetChange();
    }
}

/*static*/ void
ESSystemTimeBase::goingToSleep() {  // iOS going to sleep: Go back to using NSDate because CAMediaTime is stopped while asleep
    tracePrintf("ESSystemTimeBase Switching from alt (going to sleep)");
    assert(_usingAltTime);
    _usingAltTime = false;
    // The following shouldn't be necessary unless sysTime and altTime diverge while both are running or
    //  unless a systemBaseChange is pending
    ESTimeInterval sysTime = currentSystemTime();
    ESTimeInterval altTime = currentSystemAltTime();
    _altOffset = sysTime - altTime;
    _continuousOffset = _continuousAltOffset - _altOffset;
    // no ESTime::continuousTimeReset() here because (we assert) the continuous time remains continuous when going into sleep
}

/*static*/ void
ESSystemTimeBase::wakingUp() {  // iOS waking up
    static bool firstTime = true;
    tracePrintf("ESSystemTimeBase Switching to alt (waking up)");
    assert(firstTime || !_usingAltTime);
    _usingAltTime = true;
    ESTimeInterval sysTime = currentSystemTime();
    ESTimeInterval altTime = currentSystemAltTime();
    _altOffset = sysTime - altTime;
    _continuousAltOffset = _continuousOffset + _altOffset;  // Note: This is different from the EC/EO/ETS TSTime implementation, which was wrong
    if (firstTime) {
        firstTime = false;
    } else {
        ESTime::continuousTimeReset();  // Maybe.  We don't know.  But we notify anyway because we have no way of knowing.
    }
}

class SleepWakeObserver : public ESUtilSleepWakeObserver {
  public:
    virtual void            goingToSleep() { ESSystemTimeBase::goingToSleep(); }
    virtual void            wakingUp() { ESSystemTimeBase::wakingUp(); }
    virtual void            enteringBackground() { }  // Nothing special happens here to the timebase compared to just going to sleep
    virtual void            leavingBackground() { }
};

class TimeChangeObserver : public ESUtilSignificantTimeChangeObserver {
  public:
    virtual void            significantTimeChange() { ESSystemTimeBase::systemBaseChange(); }
};

/*static*/ ESTimeInterval 
ESSystemTimeBase::initCommonDualBase() {
    // Presume we're awake (running alt) now:  Set up offsets
    ESTimeInterval sysTime = currentSystemTime();  // Do this first:  It will be the "start of main" time
    ESTimeInterval altTime = currentSystemAltTime();

    // Set up notifications for sleep/wake and sig time change
    ESUtil::registerSleepWakeObserver(new SleepWakeObserver);
    ESUtil::registerSignificantTimeChangeObserver(new TimeChangeObserver);

    _usingAltTime = true;
    _altOffset = sysTime - altTime;
    _continuousOffset = 0;              // Start out continuous time at system time
    _continuousAltOffset = _altOffset;  // Start out continuous time at system time
    return sysTime;
}
