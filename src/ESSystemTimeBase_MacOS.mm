//
//  ESSystemTimeBase_MacOS.mm
//
//  Created by Steven Pucci 26 Feb 2013
//  Copyright Emerald Sequoia LLC 2013. All rights reserved.
//

#include "ESErrorReporter.hpp"

/*static*/ bool            ESSystemTimeBase::_usingAltTime;
/*static*/ ESTimeInterval  ESSystemTimeBase::_altOffset;              //  (systemTime - systemAltTime) aka EC mediaOffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousOffset;       //  (continuousTime - systemTime) aka EC dateROffset
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousAltOffset;    //  (continuousTime - systemAltTime) aka EC mediaROffset
/*static*/ bool            ESSystemTimeBase::_isSingleTimeBase = false;

// For MacOS, we presume the system clock is tuned to NTP and requires no further correction.  Moreover, if we were to
// use CACurrentMediaTime() as we do on iOS, it's not clear that wouldn't be less stable than NSDate

/*static*/ ESTimeInterval 
ESSystemTimeBase::init() {  // Returns currentSystemTime() at start
    _continuousOffset = 0;
    _usingAltTime = 0;
    _altOffset = 0;
    _continuousAltOffset = 0;
    return [NSDate timeIntervalSinceReferenceDate];
}

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
    // Do nothing
}

/*static*/ void 
ESSystemTimeBase::goingToSleep() {
    // Do nothing
}

/*static*/ void 
ESSystemTimeBase::wakingUp() {
    // Do nothing
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentSystemAltTime() {  // CACurrentMediaTime()
    return [NSDate timeIntervalSinceReferenceDate];
}


/*static*/ ESTimeInterval 
ESSystemTimeBase::initCommonDualBase() {
    // Do nothing
    ESAssert(false);
    return [NSDate timeIntervalSinceReferenceDate];
}

