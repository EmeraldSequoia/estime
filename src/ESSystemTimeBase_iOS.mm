//
//  ESSystemTimeBase_iOS.mm
//
//  Copied on 17 Oct 2017 from:
//
//  ESSystemTimeBase_MacOS.mm
//
//  Created by Steven Pucci 26 Feb 2013
//  Copyright Emerald Sequoia LLC 2013. All rights reserved.
//

#include "ESErrorReporter.hpp"
#include "ESSystemTimeBase.hpp"
#include "ESTime.hpp"
#include "ESUtil.hpp"

/*static*/ bool            ESSystemTimeBase::_usingAltTime;
/*static*/ ESTimeInterval  ESSystemTimeBase::_altOffset;              //  (systemTime - systemAltTime) aka EC mediaOffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousOffset;       //  (continuousTime - systemTime) aka EC dateROffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousAltOffset;    //  (continuousTime - systemAltTime) aka EC mediaROffset
/*static*/ bool            ESSystemTimeBase::_isSingleTimeBase = false;
/*static*/ bool            ESSystemTimeBase::_continuousTimeStableAtStart = false;

// For iOS, we presume the system clock is tuned to NTP and has less drift than CACurrentMediaTime()

/*static*/ ESTimeInterval 
ESSystemTimeBase::currentSystemTime() {   // Like [NSDate timeIntervalSinceReferenceDate]
    return [NSDate timeIntervalSinceReferenceDate];
}

/*static*/ ESTimeInterval 
ESSystemTimeBase::currentContinuousSystemTime() {  // Like EC's rDate, based on media time while awake and NSDate while asleep;
    return [NSDate timeIntervalSinceReferenceDate];
}

// Methods called by private observer classes
/*static*/ void 
ESSystemTimeBase::systemBaseChange() {
    ESTime::continuousTimeReset();  // We care about big shifts in NSDate now since it's our actual time base.
}

/*static*/ void 
ESSystemTimeBase::goingToSleep() {
    // Do nothing
}

/*static*/ void 
ESSystemTimeBase::wakingUp() {
    printf("Waking up\n");
    static bool firstTime = true;
    if (firstTime) {
        printf("...first time\n");
        firstTime = false;
    } else {
        printf("...NOT first time\n");
        ESTime::continuousTimeReset();  // Maybe.  We don't know.  But we notify anyway because we have no way of knowing.
    }
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentSystemAltTime() {  // CACurrentMediaTime()
    return [NSDate timeIntervalSinceReferenceDate];
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
ESSystemTimeBase::init() {
    // Set up notifications for sleep/wake and sig time change
    ESUtil::registerSleepWakeObserver(new SleepWakeObserver);
    ESUtil::registerSignificantTimeChangeObserver(new TimeChangeObserver);

    _usingAltTime = false;
    _altOffset = 0;
    _continuousOffset = 0;
    _continuousAltOffset = 0;
    return [NSDate timeIntervalSinceReferenceDate];
}
