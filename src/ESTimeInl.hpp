//
//  ESTimeInl.hpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESTIMEINL_HPP_
#define _ESTIMEINL_HPP_

#include "ESTimeSourceDriver.hpp"
#include "ESSystemTimeBase.hpp"

inline /*static*/ ESTimeInterval 
ESTime::adjustForLeapSecond(ESTimeInterval rawUTC) {
    if (rawUTC >= _nextLeapSecondDate) {
        rawUTC = adjustForLeapSecondGuts(rawUTC);
    }
    return rawUTC;
}

inline /*static*/ ESTimeInterval 
ESTime::adjustForLeapSecondWithLiveCorrection(ESTimeInterval rawUTC,
                                              ESTimeInterval *liveCorrection) {
    if (rawUTC >= _nextLeapSecondDate - 1.0) {
        return adjustForLeapSecondGutsWithLiveCorrection(rawUTC, liveCorrection);
    } else {
        *liveCorrection = 0;
        return rawUTC;
    }
}

/*static*/ inline ESTimeInterval
ESTime::currentTime() {
    return adjustForLeapSecond(_bestDriver->contSkew() + ESSystemTimeBase::currentContinuousSystemTime());
}

/*static*/ inline ESTimeInterval
ESTime::currentTimeWithLiveLeapSecondCorrection(ESTimeInterval *liveCorrection) {
    return adjustForLeapSecondWithLiveCorrection(_bestDriver->contSkew() + ESSystemTimeBase::currentContinuousSystemTime(), liveCorrection);
}

/*static*/ inline ESTimeInterval
ESTime::currentContinuousTime() {
    return ESSystemTimeBase::currentContinuousSystemTime();
}

/*static*/ inline ESTimeInterval
ESTime::skewForReportingPurposesOnly() {  // delta of NTP - SysTime = (contSkew + contSysTime()) - sysTime() = contSkew -+ contOffset
    return _bestDriver->contSkew() + ESSystemTimeBase::continuousOffset();
}

inline /*static*/ ESTimeSourceStatus 
ESTime::currentStatus() {
    return _bestDriver->currentStatus();
}

inline /*static*/ ESTimeInterval 
ESTime::skewAccuracy() {     // What is our confidence in skew()
    return _bestDriver->currentTimeError();
}

/*static*/ inline ESTimeInterval
ESTime::continuousOffset() {
    return ESSystemTimeBase::continuousOffset();
}


/*static*/ inline ESTimeInterval
ESTime::continuousSkewForReportingPurposesOnly() {
    return _bestDriver->contSkew();
}

/*static*/ inline ESTimeInterval
ESTime::ntpTimeForCTime(ESTimeInterval  cTime) {
    return adjustForLeapSecond(cTime + _bestDriver->contSkew());
}

/*static*/ inline ESTimeInterval
ESTime::ntpTimeForCTimeWithLiveCorrection(ESTimeInterval  cTime,
                                          ESTimeInterval  *liveCorrection) {
    return adjustForLeapSecondWithLiveCorrection(cTime + _bestDriver->contSkew(), liveCorrection);
}

/*static*/ inline ESTimeInterval
ESTime::cTimeForNTPTime(ESTimeInterval  ntpTime) {
    if (ntpTime > _nextLeapSecondDate) {
        return (ntpTime + _nextLeapSecondDelta) - _bestDriver->contSkew();
    }
    return ntpTime - _bestDriver->contSkew();
}

/*static*/ inline float
ESTime::currentTimeError() {
    return _bestDriver->currentTimeError();
}

/*static*/ inline ESTimeInterval
ESTime::sysTimeForCTime(ESTimeInterval cTime) {
    return cTime - ESSystemTimeBase::continuousOffset();
}

/*static*/ inline ESTimeInterval
ESTime::cTimeForSysTime(ESTimeInterval tim) {
    return tim + ESSystemTimeBase::continuousOffset();
}

inline /*static*/ ESTimeInterval 
ESTime::sysTimeForNTPTime(ESTimeInterval ntpTime) {
    return ESTime::cTimeForNTPTime(ntpTime) - ESSystemTimeBase::continuousOffset();
}

#else  // _ESTIMEINL_HPP_
#error "Don't include this file directly; include ESTime.hpp which will include it for you"
#endif  // _ESTIMEINL_HPP_
