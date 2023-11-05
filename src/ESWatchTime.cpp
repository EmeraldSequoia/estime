//
//  ESWatchTime.cpp
//  Created by Steve Pucci 29 May 2011.

//  Based on EC-WatchTime.m in Emerald Chronometer created by Steve Pucci 5/2008.
//  Copyright Emerald Sequoia LLC 2008-2011. All rights reserved.

#include "ESWatchTime.hpp"
#include "ESErrorReporter.hpp"
#include "ESTimeEnvironment.hpp"
#include "ESTime.hpp"
#include "ESCalendar.hpp"
#include "ESUserPrefs.hpp"
#undef ESTRACE
#include "ESTrace.hpp"

#include <math.h>  // For nan()
#include <stdlib.h>

#define kESWatchTimeAdvanceGap (2.0)  // Number of seconds prior to the advance "next" target that we stop

static double *warpValuesForCycle = NULL;
static ESWatchTimeCycle *nextCycleForFF = NULL;
static ESWatchTimeCycle *nextCycleForRewind = NULL;

static void
initializeWarpValues() {
    if (!warpValuesForCycle) {
	warpValuesForCycle = (double *)malloc(sizeof(double) * ECNumWatchCycles);
	nextCycleForFF = (ESWatchTimeCycle *)malloc(sizeof(ESWatchTimeCycle) * ECNumWatchCycles);
	nextCycleForRewind = (ESWatchTimeCycle *)malloc(sizeof(ESWatchTimeCycle) * ECNumWatchCycles);
    }
    
    // The value to use for _warp when cycling to this state
    warpValuesForCycle[ESWatchCycleReverseFF6]   = - 5 * 24 * 3600;
    warpValuesForCycle[ESWatchCycleReverseFF5] 	 = - 24 * 3600;
    warpValuesForCycle[ESWatchCycleReverseFF4] 	 = -  5 * 3600;
    warpValuesForCycle[ESWatchCycleReverseFF3] 	 = - 3600;
    warpValuesForCycle[ESWatchCycleReverseFF2] 	 = - 60;
    warpValuesForCycle[ESWatchCycleReverseFF1] 	 = - 5;
    warpValuesForCycle[ESWatchCycleReverse]      = - 1;
    warpValuesForCycle[ESWatchCycleReverseSlow1] = - .25;
    warpValuesForCycle[ESWatchCycleSlow1]        =   .25;
    warpValuesForCycle[ESWatchCycleNormal]       =   1;
    warpValuesForCycle[ESWatchCycleFF1]       =   5;
    warpValuesForCycle[ESWatchCycleFF2]       =   60;
    warpValuesForCycle[ESWatchCycleFF3]       =   3600;
    warpValuesForCycle[ESWatchCycleFF4]       =   5 * 3600;
    warpValuesForCycle[ESWatchCycleFF5]       =  24 * 3600;
    warpValuesForCycle[ESWatchCycleFF6]       =   5 * 24 * 3600;
    warpValuesForCycle[ESWatchCyclePaused]    =   0;
    warpValuesForCycle[ESWatchCycleOther]     =   1;  // Should never be used

    nextCycleForFF[ESWatchCycleReverseFF6]   = ESWatchCycleReverseFF5;
    nextCycleForFF[ESWatchCycleReverseFF5]   = ESWatchCycleReverseFF4;
    nextCycleForFF[ESWatchCycleReverseFF4]   = ESWatchCycleReverseFF3;
    nextCycleForFF[ESWatchCycleReverseFF3]   = ESWatchCycleReverseFF2;
    nextCycleForFF[ESWatchCycleReverseFF2]   = ESWatchCycleReverseFF1;
    nextCycleForFF[ESWatchCycleReverseFF1]   = ESWatchCycleReverse;
    nextCycleForFF[ESWatchCycleReverse]      = ESWatchCycleReverseSlow1;
    nextCycleForFF[ESWatchCycleReverseSlow1] = ESWatchCycleSlow1;
    nextCycleForFF[ESWatchCycleSlow1]        = ESWatchCycleNormal;
    nextCycleForFF[ESWatchCycleNormal]       = ESWatchCycleFF1;
    nextCycleForFF[ESWatchCycleFF1]          = ESWatchCycleFF2;
    nextCycleForFF[ESWatchCycleFF2]          = ESWatchCycleFF3;
    nextCycleForFF[ESWatchCycleFF3]          = ESWatchCycleFF4;
    nextCycleForFF[ESWatchCycleFF4]          = ESWatchCycleFF5;
    nextCycleForFF[ESWatchCycleFF5]          = ESWatchCycleFF6;
    nextCycleForFF[ESWatchCycleFF6]          = ESWatchCycleNormal;  // _cycle around, but only positive speeds
    nextCycleForFF[ESWatchCyclePaused]       = ESWatchCycleNormal;
    nextCycleForFF[ESWatchCycleOther]        = ESWatchCycleNormal;

    nextCycleForRewind[ESWatchCycleReverseFF6]   = ESWatchCycleReverse;  // _cycle around, but only negative or slow speeds
    nextCycleForRewind[ESWatchCycleReverseFF5]   = ESWatchCycleReverseFF6;
    nextCycleForRewind[ESWatchCycleReverseFF4]   = ESWatchCycleReverseFF5;
    nextCycleForRewind[ESWatchCycleReverseFF3]   = ESWatchCycleReverseFF4;
    nextCycleForRewind[ESWatchCycleReverseFF2]   = ESWatchCycleReverseFF3;
    nextCycleForRewind[ESWatchCycleReverseFF1]   = ESWatchCycleReverseFF2;
    nextCycleForRewind[ESWatchCycleReverse]      = ESWatchCycleReverseFF1;
    nextCycleForRewind[ESWatchCycleReverseSlow1] = ESWatchCycleReverse;
    nextCycleForRewind[ESWatchCycleSlow1]        = ESWatchCycleReverseSlow1;
    nextCycleForRewind[ESWatchCycleNormal]       = ESWatchCycleSlow1;
    nextCycleForRewind[ESWatchCycleFF1]          = ESWatchCycleNormal;
    nextCycleForRewind[ESWatchCycleFF2]          = ESWatchCycleFF1;
    nextCycleForRewind[ESWatchCycleFF3]          = ESWatchCycleFF2;
    nextCycleForRewind[ESWatchCycleFF4]          = ESWatchCycleFF3;
    nextCycleForRewind[ESWatchCycleFF5]          = ESWatchCycleFF4;
    nextCycleForRewind[ESWatchCycleFF6]          = ESWatchCycleFF5;
    nextCycleForRewind[ESWatchCyclePaused]       = ESWatchCycleReverseSlow1;
    nextCycleForRewind[ESWatchCycleOther]        = ESWatchCycleReverse;
}

#define calcEffectiveSkewIgnoringLeapSeconds(_useSmoothTime) (_useSmoothTime ? ESTime::continuousOffset() : ESTime::skewForReportingPurposesOnly())
#define calcOurTimeAtIPhoneZeroIgnoringLeapSeconds(_useSmoothTime) (_ourTimeAtCorrectedZero + calcEffectiveSkewIgnoringLeapSeconds(_useSmoothTime))
#define appropriateCorrectedTime(_useSmoothTime) (_useSmoothTime ? ESTime::currentContinuousTime() : ESTime::currentTime())

double
ESWatchTime::ourTimeAtIPhoneZeroIgnoringLeapSeconds() {
    if (_warp) {
	return calcOurTimeAtIPhoneZeroIgnoringLeapSeconds(_useSmoothTime);
    } else {
	return _ourTimeAtCorrectedZero;
    }
}

void
ESWatchTime::commonInit() {
    if (!warpValuesForCycle) {
	initializeWarpValues();
    }
    _warpBeforeFreeze = 0.0;
    _useSmoothTime = false;
    _latched = 0;
    _fastWarpReferenceTime = 0;
}

ESWatchTime::ESWatchTime() {
    _warp = 1.0;
    _ourTimeAtCorrectedZero = 0;
    _cycle = ESWatchCycleNormal;
    commonInit();
}

ESWatchTime::ESWatchTime(ESTimeInterval frozenTimeInterval) {
    _warp = 0.0;
    _cycle = ESWatchCyclePaused;
    _ourTimeAtCorrectedZero = frozenTimeInterval;
    commonInit();
}

/*virtual*/
ESWatchTime::~ESWatchTime() {
}

void
ESWatchTime::saveStateForWatch(const std::string & nam) {
    // note that we save "_warp-1" and add 1 back in in restoreStateForWatch so that
    //  when it's restored the very first time the uninitialized value of zero becomes 1 which is what we want
    ESUserPrefs::setPref((nam + "-_warp")/*forKey*/, _warp-1);
    ESUserPrefs::setPref((nam + "-_warpBeforeFreeze")/*forKey*/, _warpBeforeFreeze-1);
    ESUserPrefs::setPref((nam + "-timeZero")/*forKey*/, _ourTimeAtCorrectedZero);
}

void
ESWatchTime::restoreStateForWatch(const std::string & nam) {
    _warp = ESUserPrefs::doublePref((nam + "-_warp")) + 1;
    _warpBeforeFreeze = ESUserPrefs::doublePref((nam + "-_warpBeforeFreeze")) + 1;
    _ourTimeAtCorrectedZero = ESUserPrefs::doublePref((nam + "-timeZero"));
}

#if 0
static ESTimeInterval lastMaxRangeMessageTime = 0;
static ESTimeInterval lastMinRangeMessageTime = 0;

void
ESWatchTime::possiblyPutUpMessageBoxAboutMaxRange() {
    ESTimeInterval now = ESTime::currentContinuousTime();
    if (now - lastMinRangeMessageTime > ECLastAstroDateWarningInterval) {
        ESErrorReporter::reportErrorToUser(ESLocalizedString("The earliest time supported is \n%s", "Astronomy date range checking"),
                                           "4000 Jan 1 BCE 00:00:00 UT");
	lastMinRangeMessageTime = now;
    }
}

void
ESWatchTime::possiblyPutUpMessageBoxAboutMinRange() {
    ESTimeInterval now = ESTime::currentContinuousTime();
    if (now - lastMinRangeMessageTime > ECLastAstroDateWarningInterval) {
        ESErrorReporter::reportErrorToUser(ESLocalizedString("The latest time supported is \n%s", "Astronomy date range checking"),
                                           "2800 Dec 31 23:59:59 UT");
	lastMaxRangeMessageTime = now;
    }
}
#endif

bool
ESWatchTime::checkAndConstrainAbsoluteTime() {
    if (currentTime() <= ESMinimumSupportedAstroDate) {
	//possiblyPutUpMessageBoxAboutMaxRange();
	if (_warp != 0) {
	    stop();
	} else {
	    _ourTimeAtCorrectedZero = ESMinimumSupportedAstroDate;
	}
	return true;
    } 
    if (currentTime() >= ESMaximumSupportedAstroDate) {
	//possiblyPutUpMessageBoxAboutMinRange();
	if (_warp != 0) {
	    stop();
	} else {
	    _ourTimeAtCorrectedZero = ESMaximumSupportedAstroDate;
	}
	return true;
    }
    return false;
}

void
ESWatchTime::setFastWarpReferenceTime(ESTimeInterval fastWarpReferenceTime) {
    _fastWarpReferenceTime = fastWarpReferenceTime;
}

ESTimeInterval
ESWatchTime::latchTimeForBeatsPerSecond(int beatsPerSecond) {
    ESAssert(_latched >= 0);
    if (!_latched) {
        if (beatsPerSecond > 0) {
            _latchCorrectedTime = rint(currentTime() * beatsPerSecond) / beatsPerSecond;
        } else {
            _latchCorrectedTime = currentTime();
        }
        // We freeze a hand if it would otherwise be going more than ten revolutions per second.
        double absoluteWarp = fabs(_warp);
        double latchUnit = 0;
        if (absoluteWarp > 60 * 10) {  // 10 minutes per second, so ten revolutions of second hand
            latchUnit = 60;
            if (absoluteWarp > 60 * 60 * 10) {  // 10 hours per second, so ten revolutions of minute hand
                latchUnit = 60 * 60;
                if (absoluteWarp > 12 * 60 * 60 * 10)  {  // 120 hours per second, so 10 revolutions of 12-hour hand
                    latchUnit = 24 * 60 * 60;
                    // We could check for the day hand here, but that requires having an env, so we don't bother.
                    // Note that this will latch to the hour shown on the timezone at _fastWarpReferenceTime, so
                    // if DST has changed since then the hour hand will jump at the transition.  This could also
                    // be fixed by maintaining an env, but that seems counter to the philosophy used elsewhere
                    // in this file (note that a single ESWatchTime can serve the time in many time zones, as with
                    // Terra I and II).
                }
            }
            // ESErrorReporter::logInfo("latchTimeForBeatsPerSecond", "prior latch %.0f, rint %.0f, fmod %.0f, result %.0f",
            //                          _latchCorrectedTime, rint(_latchCorrectedTime / latchUnit) * latchUnit, fmod(_fastWarpReferenceTime, latchUnit),
            //                          rint(_latchCorrectedTime / latchUnit ) * latchUnit + fmod(_fastWarpReferenceTime, latchUnit));
            _latchCorrectedTime = rint(_latchCorrectedTime / latchUnit) * latchUnit + fmod(_fastWarpReferenceTime, latchUnit);
        }
    }
    _latched++;
    return _latchCorrectedTime;
}

ESTimeInterval
ESWatchTime::latchTimeOnMinuteBoundary() {
    ESAssert(_latched >= 0);
    if (!_latched) {
        _latchCorrectedTime = rint(currentTime() / 60.0) * 60.0;
    }
    _latched++;
    return _latchCorrectedTime;
}

void
ESWatchTime::unlatchTime() {
    ESAssert(_latched > 0);
    _latched--;
}

// *****************
// Internal methods:
// *****************

double
ESWatchTime::currentTimeIgnoringLatch() {
    if (_warp) { // If running, incorporate skew; else not; that way skew can change without affecting stopped clocks (and stopwatches)
	// This is just y = mx + b
	return _warp * appropriateCorrectedTime(_useSmoothTime) + _ourTimeAtCorrectedZero;
    } else {
	return _ourTimeAtCorrectedZero;  // ourTimeAtAnyTimeWhatsoever
    }
}

double
ESWatchTime::currentTime() {
    if (_latched) {
	return _latchCorrectedTime;
    }
    if (_warp) { // If running, incorporate skew; else not; that way skew can change without affecting stopped clocks (and stopwatches)
	// This is just y = mx + b
	return _warp * appropriateCorrectedTime(_useSmoothTime) + _ourTimeAtCorrectedZero;
    } else {
	return _ourTimeAtCorrectedZero;  // ourTimeAtAnyTimeWhatsoever
    }
}

// Return the watch time corresponding to the given iPhone time
double
ESWatchTime::convertFromSystemToWatch(double t) {
    if (_warp) {  // only include skew when watch is running
        ESTimeInterval continuousTime = ESTime::cTimeForSysTime(t);
        ESTimeInterval correctedTime;
        if (_useSmoothTime) {
            correctedTime = continuousTime;
        } else {
            correctedTime = ESTime::ntpTimeForCTime(continuousTime);
        }
        return _warp * correctedTime + _ourTimeAtCorrectedZero;
    } else {
	return _ourTimeAtCorrectedZero;  // ourTimeAtAnyTimeWhatsoever, since it's constant
    }
}

// Return the iPhone time corresponding to the given watch time
double
ESWatchTime::convertFromWatchToSystem(double t) {
    // This is just the inverse of the above x = (y-b)/m
    if (_warp == 0) {
	return 0;  // no possible answer unless _ourTimeAtCorrectedZero == t in which case every answer is correct
    } else {
        ESTimeInterval correctedTime = (t - _ourTimeAtCorrectedZero) / _warp;
        ESTimeInterval continuousTime;
        if (_useSmoothTime) {
            continuousTime = correctedTime;
        } else {
            continuousTime = ESTime::cTimeForNTPTime(correctedTime);
        }
        return ESTime::sysTimeForCTime(continuousTime);
    }
}

ESTimeInterval 
ESWatchTime::contTimeForWatchTime(ESTimeInterval t) {
    if (_warp == 0) {
        ESTimeInterval effectiveWatchTime =
            _latched
            ? _latchCorrectedTime
            : _ourTimeAtCorrectedZero;
        if (effectiveWatchTime == t) {
            return ESTime::currentContinuousTime();  // Now and every other time
        } else {
            return ESUtil::unspecifiedNAN();  // No answer is correct
        }
    } else {
        ESTimeInterval correctedTime = (t - _ourTimeAtCorrectedZero) / _warp;
        ESTimeInterval continuousTime;
        if (_useSmoothTime) {
            continuousTime = correctedTime;
        } else {
            continuousTime = ESTime::cTimeForNTPTime(correctedTime);
        }
        return continuousTime;
    }
}

ESTimeInterval 
ESWatchTime::contTimeForWatchTime() {
    if (_warp == 0) {
        if (_latched) {
            if (_useSmoothTime) {
                return _latchCorrectedTime;
            } else {
                return ESTime::cTimeForNTPTime(_latchCorrectedTime);
            }
        } else {
            return ESTime::currentContinuousTime();  // Now and every other time
        }
    } else {
        if (_useSmoothTime) {
            return (ESTime::currentContinuousTime() - _ourTimeAtCorrectedZero) / _warp;
        } else {
            return ESTime::cTimeForNTPTime((ESTime::currentTime() - _ourTimeAtCorrectedZero) / _warp);
        }
    }
}

ESTimeInterval 
ESWatchTime::watchTimeForContTime(ESTimeInterval continuousTime) {
    if (_warp) {  // only include skew when watch is running
        ESTimeInterval correctedTime;
        if (_useSmoothTime) {
            correctedTime = continuousTime;
        } else {
            correctedTime = ESTime::ntpTimeForCTime(continuousTime);
        }
        return _warp * correctedTime + _ourTimeAtCorrectedZero;
    } else {
	return _ourTimeAtCorrectedZero;  // ourTimeAtAnyTimeWhatsoever, since it's constant
    }
}

#if 0  //BOGUS
// Return how far the indicated time is from now
double
ESWatchTime::offsetFromNow() {
    ESAssert(_warp == 0 || _warp == 1);
    ESAssert(!_useSmoothTime);  // This makes no sense if we're not after the absolute time
    if (_warp) {
	//     _warp * date + calcOurTimeAtIPhoneZero(_useSmoothTime) - ESTime::currentTime()
	// ==  _warp * date + _ourTimeAtCorrectedZero + calcEffectiveSkew(_useSmoothTime) - (date + dateSkew)
	// ==  date + _ourTimeAtCorrectedZero + (_useSmoothTime ? dateROffset : dateSkew) - (date + dateSkew)
	// ==  _ourTimeAtCorrectedZero + _useSmoothTime ? dateROffset - dateSkew : 0;
	return _useSmoothTime ? _ourTimeAtCorrectedZero + ESTime::continuousOffset() - ESTime::currentTime() : _ourTimeAtCorrectedZero;
    } else {
	// Nothing for it, we need current time.  Might as well just do it directly
	return currentTime() - ESTime::currentTime();
    }
}
#endif

bool
ESWatchTime::isStopped() {
    return (_warp == 0);
}

bool
ESWatchTime::runningBackward() {
    return (_warp < 0);
}

int
ESWatchTime::secondsSinceMidnightNumberUsingEnv(ESTimeEnvironment *env) {
    double now = floor(currentTime());
    double midnight = env->midnightForTimeInterval(now);
    return (int)(now - midnight);
}

double
ESWatchTime::secondsSinceMidnightValueUsingEnv(ESTimeEnvironment *env) {
    double now = currentTime();
    double midnight = env->midnightForTimeInterval(now);
    return now - midnight;
}

// *****************
// Methods useful for watch hands and moving dials:
// *****************

// 12:35:45.9 => 45
int
ESWatchTime::secondNumberUsingEnv(ESTimeEnvironment *env) {
    return secondsSinceMidnightNumberUsingEnv(env) % 60;
}

// 12:35:45.9 => 45.9
double
ESWatchTime::secondValueUsingEnv(ESTimeEnvironment *env) {
    return ESUtil::fmod(secondsSinceMidnightValueUsingEnv(env), 60);
}

// 12:35:45 => 35
int
ESWatchTime::minuteNumberUsingEnv(ESTimeEnvironment *env) {
    return (secondsSinceMidnightNumberUsingEnv(env) / 60) % 60;
}

// 12:35:45 => 35.75
double
ESWatchTime::minuteValueUsingEnv(ESTimeEnvironment *env) {
    return ESUtil::fmod(secondsSinceMidnightValueUsingEnv(env) / 60, 60);
}

// 13:45:00 => 1
int
ESWatchTime::hour12NumberUsingEnv(ESTimeEnvironment *env) {
    return (secondsSinceMidnightNumberUsingEnv(env) / 3600) % 12;
}

// 13:45:00 => 1.75
double
ESWatchTime::hour12ValueUsingEnv(ESTimeEnvironment *env) {
    return ESUtil::fmod(secondsSinceMidnightValueUsingEnv(env) / 3600, 12);
}

// 13:45:00 => 13
int
ESWatchTime::hour24NumberUsingEnv(ESTimeEnvironment *env) {
    return (secondsSinceMidnightNumberUsingEnv(env) / 3600) % 24;
}

// 13:45:00 => 13.75
double
ESWatchTime::hour24ValueUsingEnv(ESTimeEnvironment *env) {
    return ESUtil::fmod(secondsSinceMidnightValueUsingEnv(env) / 3600, 24);
}

// March 1 => 0  (n.b, not 1; useful for angles and for arrays of images, and consistent with double form below)
int
ESWatchTime::dayNumberUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return cs.day - 1;
}
    
// March 1 at 6pm  =>  0.75;  useful for continuous hands displaying day
double
ESWatchTime::dayValueUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return cs.day - 1.0 + cs.hour / 24.0 + cs.minute / (60.0 * 24.0) + cs.seconds / (3600.0 * 24.0);
} 

// March 1 => 2  (n.b., not 3)
int
ESWatchTime::monthNumberUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return cs.month - 1;
}

static int
calcDaysInMonth(int        nsMonthNumber,
		int        year,
		int        era,
		ESTimeZone *estz)
{
    switch(nsMonthNumber) {
      case 1:
	return 31;
      case 2:
        {
	    // look up March 1 of this year and subtract Feb 1, divide by 3600 * 24 => num days
	    ESDateComponents cs;
	    cs.day = 1;
	    cs.month = 2;
	    cs.year = year;
	    cs.era = era;
	    cs.hour = 0;
	    cs.minute = 0;
	    cs.seconds = 0;
	    ESTimeInterval feb1 = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
	    cs.month = 3;
	    ESTimeInterval mar1 = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
	    return rint((mar1 - feb1) / (3600.0 * 24.0));
        }
      case 3:
	return 31;
      case 4:
	return 30;
      case 5:
	return 31;
      case 6:
	return 30;
      case 7:
	return 30;
      case 8:
	return 31;
      case 9:
	return 30;
      case 10:
	return 31;
      case 11:
	return 30;
      case 12:
	return 31;
      default:
	return 0;
    }
}

// March 1 at noon  =>  12 / (31 * 24); useful for continuous hands displaying month
double
ESWatchTime::monthValueUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return (cs.month - 1.0) + (cs.day - 1.0 + cs.hour / 24.0 + cs.minute / (60.0 * 24.0) + cs.seconds / (3600.0 * 24.0))/calcDaysInMonth(cs.month, cs.year, cs.era, env->estz());
}

// March 1 1999 => 1999
int
ESWatchTime::yearNumberUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return cs.year;
}

// BCE => 0; CE => 1
int
ESWatchTime::eraNumberUsingEnv(ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    return cs.era;
}

// daylight => 1; standard => 0
bool
ESWatchTime::isDSTUsingEnv(ESTimeEnvironment *env) {
    double t = currentTime();
    if (_warp > 0) {
	t += 1.0;  // Apple doesn't seem to know that 02:00:00.4 is after the transition
    } else if (_warp < 0) {
	t -= 1.0;  // And when we go backwards, we want a time that's before the transition
    }
    // ESErrorReporter::logInfo("ESWatchTime::isDSTUsingEnv", "DSTNumber: Checking DSTForDate:");
    // ESTime::printADateWithTimeZone(t, env->estz());
    // ESErrorReporter::logInfo("ESWatchTime::isDSTUsingEnv", "getting '%s'\n", ESCalendar_isDSTAtTimeInterval(env->estz(), t) ? "yes" : "no");
    return ESCalendar_isDSTAtTimeInterval(env->estz(), t);
}

//extern void printADate(ESTimeInterval dt);  // Wrong prototype

ESTimeInterval
ESWatchTime::nextDSTChangeUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval now = currentTimeIgnoringLatch();
    midnightForDateInterval(now, env/*usingEnv*/);  // force cache load
    if (now > env->_cacheMidnightBase && now < env->_cacheDSTEvent) {
	return env->_cacheDSTEvent;
    }
    // We'll get here if we're after the DST change but before the end of the midnight cache interval
    //printf("Cache miss for nextDST, now = "); printADate(now); printf(", midnightBase = "); printADate(env->cacheMidnightBase); printf(", dst event = "); printADate(env->cacheDSTEvent); printf("\n");
    ESTimeInterval nextChange = ESCalendar_nextDSTChangeAfterTimeInterval(env->estz(), now);
    //printf("Calculating next DST change for %s => %s\n",
    //	   [[now description] UTF8String],
    //	   [[date description] UTF8String]);
    return nextChange;
}

ESTimeInterval
ESWatchTime::prevDSTChangePrecisely(bool              precise,
                                    ESTimeEnvironment *env) {
    ESTimeZone *estz = env->estz();

    ESTimeInterval now = currentTimeIgnoringLatch();

    // Let's try back one year first (remember, there should be two every year)
    ESTimeInterval oneYearPrior = now - 365 * 24 * 3600;
    ESTimeInterval startInterval = oneYearPrior;
    ESTimeInterval nextShift = ESCalendar_nextDSTChangeAfterTimeInterval(estz, startInterval);
    if (nextShift) {
	if (nextShift < now) {
	    // If not, then our one and only known transition is after where we are and we know nothing
	    ESTimeInterval bestShiftSoFar = nextShift; // startDate >= nextShift > now
	    while (1) {
		//printf("%s is before %s?\n",
		//	   nextShift->description().c_str(),
		//	   now->description().c_str());
		startInterval = nextShift + 7200;  // move past ambiguity
		nextShift = ESCalendar_nextDSTChangeAfterTimeInterval(estz, startInterval);
		if (!nextShift || nextShift >= now) {
		    return bestShiftSoFar;
		}
		// OK, nextShift passes, so move on up and iterate
		bestShiftSoFar = nextShift;
	    }
	    //printf("One-year: Calculating prev DST change for %s => %s\n",
	    //       now->description().c_str(),
	    //       startDate->description().c_str());
	    return 0;
	}
    }
    if (!precise) {
	return 0;
    }
    ESAssert(false);  // Nothing should need precise prevDST; if it does, this should be rewritten 'cause it's too darned slow
    // Nothing back a year.  Let's try going back 10, 100, 1000, 10000 years just in case
    int i;
    for (i = 10; i <= 10000; i *= 10) {
	ESTimeInterval prior = now - i * 365.0 * 24 * 3600;
	nextShift = ESCalendar_nextDSTChangeAfterTimeInterval(estz, prior);
	if (nextShift) {  // found one in prior eras.  The following loop might take a while...
	    // Put some code here kinda like the code above
	}
    }
    //printf("No prev DST change for %s\n",
    //	   [[now description] UTF8String]);
    return 0;  // must be no DST in this time zone
}

// 2000 => 1, 2001 => 0
bool
ESWatchTime::leapYearUsingEnv(ESTimeEnvironment *env) {
    return (ESCalendar_daysInYear(env->estz(), currentTime()) > 365.5);
}

// Tuesday => 2
int
ESWatchTime::weekdayNumberUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval now = currentTime();
    ESTimeInterval localNow = now + ESCalendar_tzOffsetForTimeInterval(env->estz(), now);
    double localNowDays = localNow / (24 * 3600);
    double weekday = ESUtil::fmod(localNowDays + 1, 7);
    //printf("weekdayNumber is %d (%.4f)\n", (int)floor(weekday), weekday);
    return (int)floor(weekday);
}

// Tuesday at 6pm => 2.75
double
ESWatchTime::weekdayValueUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval now = currentTime();
    ESTimeInterval localNow = now + ESCalendar_tzOffsetForTimeInterval(env->estz(), now);
    double localNowDays = localNow / (24 * 3600);
    double weekday = ESUtil::fmod(localNowDays + 1, 7);
    return weekday;
}

// This function incorporates the value of ECCalendarWeekdayStart
int
ESWatchTime::weekdayNumberAsCalendarColumnUsingEnv(ESTimeEnvironment *env, int weekdayStart) {
    ESTimeInterval now = currentTime();
    ESTimeInterval localNow = now + ESCalendar_tzOffsetForTimeInterval(env->estz(), now);
    double localNowDays = localNow / (24 * 3600);
    double weekday = ESUtil::fmod(localNowDays + 1, 7);
    int weekdayNumber = (int)floor(weekday);
    //printf("weekdayNumber is %d (%.4f)\n", weekdayNumber, weekday);
    return (7 + weekdayNumber - weekdayStart) % 7;
}

// Leap year: fraction of 366 days since Jan 1
// Non-leap year: fraction of 366 days since Jan 1 through Feb 28, then that plus 24hrs starting Mar 1
// Result is indicator value on 366-year dial
double
ESWatchTime::year366IndicatorFractionUsingEnv(ESTimeEnvironment *env) {
    const int secondsIn366Year = 366 * 24 * 3600;  // 31,622,400
    double ct = currentTime();
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(ct, env->estz(), &cs);
    //printf("Date %s %04d-%02d\n",
    //cs.era ? " CE" : "BCE",
    //cs.year, cs.month);
    int month = cs.month;
    cs.month = 1;
    int day = cs.day;
    cs.day = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisYearInterval = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    bool isLeapYear = (ESCalendar_daysInYear(env->estz(), ct) > 365.5);
    bool isGregorianTransitionYear = (cs.year == 1582 && cs.era == 1);
    //printf("...daysInYear %d, isLeapYear %s\n", daysInYear(calendar, ct), isLeapYear ? "TRUE" : "FALSE");
    if (!isLeapYear && month > 2) {
	ct += 24 * 3600;  // We skip over Feb 28 in non-leap years
	if (isGregorianTransitionYear) {
	    if (month > 10 || (month == 10 && day > 4)) {
		ct += 10 * 24*3600;
	    }
	}
    }
    return (ct - firstOfThisYearInterval) / secondsIn366Year;
}

// Jan 1 => 0    
int 
ESWatchTime::dayOfYearNumberUsingEnv(ESTimeEnvironment *env) {
    double midnight = env->midnightForTimeInterval(currentTime()) + 2;  // Make sure we're in the same day, but just barely, so we get the right date and we can round to ignore DST effects
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(midnight, env->estz(), &cs);
    //printf("Date %s %04d-%02d\n",
    //cs.era ? " CE" : "BCE",
    //cs.year, cs.month);
    cs.month = 1;
    cs.day = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisYearInterval = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    return (int)rint((midnight - firstOfThisYearInterval) / (24 * 3600));
}

// First week => 0    
int 
ESWatchTime::weekOfYearNumberUsingEnv(int               weekStartDay,  // weekStartDay == 0 means weeks start on Sunday
                                      bool              useISO8601,    // use only when weekStartDay == 1 (Monday)
                                      ESTimeEnvironment *env) {
    ESAssert(!useISO8601 || weekStartDay == 1);
    double midnight = env->midnightForTimeInterval(currentTime()) + 2;  // Make sure we're in the same day, but just barely,
                                                                        // so we get the right date and we can round to ignore DST effects
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(midnight, env->estz(), &cs);
    //printf("Date %s %04d-%02d\n",
    //cs.era ? " CE" : "BCE",
    //cs.year, cs.month);

    if (useISO8601 && cs.month == 12 && cs.day >= 29) {  // End of December
        int weekday = ESCalendar_localWeekdayFromTimeInterval(midnight, env->estz());
        int weekdayOfDec31 = (weekday + (31 - cs.day)) % 7;
        if (weekdayOfDec31 <= 3 && weekdayOfDec31 >= 1 &&
            weekdayOfDec31 + cs.day >= 32) {   // weekdayOfDec31 == 3 && cs.day >= 29 ||
                                               // weekdayOfDec31 == 2 && cs.day >= 30 ||
                                               // weekdayOfDec31 == 1 && cs.day >= 31
            return 0;  // we're part of next year, and we're the first week
        }
    }

    cs.month = 1;
    cs.day = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisYearInterval = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);

    while (1) {
        int weekdayOfJan1 = ESCalendar_localWeekdayFromTimeInterval(firstOfThisYearInterval, env->estz());

        // Now get "effective" first day of year, which is the first day of the first week as defined by algorithm
        int deltaFromFirstDayToFirstWeek;  // positive means first day is behind first week (normally false, so normally negative)
        if (useISO8601) {
            if (weekdayOfJan1 == 0 || weekdayOfJan1 >= 5) {  // First day of year belongs to previous year's last week; move forward
                // 5 => 3, 6 => 2, 0 => 1
                deltaFromFirstDayToFirstWeek = (8 - weekdayOfJan1) % 7;
            } else {  // First day of year belongs to this year's first week; move backward
                // 1 => 0, 2 => -1, 3 => -2, 4 => -3
                deltaFromFirstDayToFirstWeek = (1 - weekdayOfJan1);
            }
        } else {
            // weekStartDay == 0: 0 => 0, 1 => -1, 2 => -2, ... 6 => -6
            // weekStartDay == 6: 6 => 0, 0 => -1, 1 => -2, ... 5 => -6
            // weekStartDay == 5: 5 => 0, 6 => -1, 0 => -2, ... 4 => -6
            deltaFromFirstDayToFirstWeek = - ((weekdayOfJan1 + 7 - weekStartDay) % 7);
        }

        firstOfThisYearInterval += (deltaFromFirstDayToFirstWeek * 24 * 3600);

        if (midnight >= firstOfThisYearInterval) {
            break;
        }

        // If we're here, then this day logically belongs to the previous year

        if (cs.era > 0) {
            if (cs.year == 1) {
                cs.era = 0;
            } else {
                cs.year--;
            }
        } else {
            cs.year++;
        }
        firstOfThisYearInterval = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
        ESAssert(midnight >= firstOfThisYearInterval);
    } 

    int logicalDayOfYear = (int)rint((midnight - firstOfThisYearInterval) / (24 * 3600));  // relative to first week start day
    return logicalDayOfYear / 7;
}


// *****************
// Stopwatch methods:  The y for a stopwatch watch is zero at reset
// *****************

// 00:04:03.9 => 3
int
ESWatchTime::stopwatchSecondNumber() {
    return (int)(llrint(floor(currentTime())) % 60);
}

// 00:04:03.9 => 3.9
double
ESWatchTime::stopwatchSecondValue() {
    return ESUtil::fmod(currentTime(), 60);
}

// 01:04:45 => 4
int
ESWatchTime::stopwatchMinuteNumber() {
    return (int)((llrint(floor(currentTime())) / 60) % 60);
}

// 01:04:45 => 4.75
double
ESWatchTime::stopwatchMinuteValue() {
    return ESUtil::fmod(currentTime() / 60, 60);
}

// 5d 01:30:00 => 1
int
ESWatchTime::stopwatchHour24Number() {
    return (int)((llrint(floor(currentTime())) / 3600) % 24);
}

// 5d 01:30:00 => 1.5
double
ESWatchTime::stopwatchHour24Value() {
    return ESUtil::fmod(currentTime() / 3600, 24);
}

// 5d 01:30:00 => 1
int
ESWatchTime::stopwatchHour12Number() {
    return (int)((llrint(floor(currentTime())) / 3600) % 12);
}

// 5d 01:30:00 => 1.5
double
ESWatchTime::stopwatchHour12Value() {
    return ESUtil::fmod(currentTime() / 3600, 12);
}

// 5d 18:00:00 => 5
int
ESWatchTime::stopwatchDayNumber() {
    return (int)(llrint(floor(currentTime())) / (3600 * 24));
}

// 5d 18:00:00 => 5.75
double
ESWatchTime::stopwatchDayValue() {
    return currentTime() / (3600 * 24);
}

// *****************
// Alarm methods
// *****************

// *****************
// Set methods
// *****************

static ESWatchTimeCycle
cycleFromWarp(double warp) {
    if (warp == 0) {
	return ESWatchCyclePaused;
    }
    ESWatchTimeCycle cycle;
    for (cycle = ESWatchCycleFirst; cycle <= ESWatchCycleLastForward; cycle = (ESWatchTimeCycle)(cycle + 1)) {
	if (warpValuesForCycle[cycle] > warp) {
	    if (cycle < ESWatchCycleFirst) {
		if (warpValuesForCycle[cycle] - warp > warp - warpValuesForCycle[cycle - 1] ) {
		    return (ESWatchTimeCycle)(cycle - 1);
		} else {
		    return cycle;
		}
	    }
	}
    }
    return ESWatchCycleOther;
}

// Freeze at current time; does nothing if already frozen
// Skew is removed from _ourTimeAtCorrectedZero when stopping so we don't have to recalculate stopped watches when skew changes
void
ESWatchTime::stop() {
    if (_warp != 0.0) {
	// m = 0 => b = y;
	_ourTimeAtCorrectedZero = _latched ? _latchCorrectedTime : currentTime();
	_warpBeforeFreeze = _warp;
	_warp = 0.0;
	_cycle = ESWatchCyclePaused;
    }
}

// Unfreeze; does nothing if not frozen
// Skew is inserted back into _ourTimeAtCorrectedZero when restarting so we don't have to modify stopped watches when skew changes
void
ESWatchTime::start() {
    if (_warp == 0.0) {
	// b = y - mx;
	if (_warpBeforeFreeze == 0) {
	    _warpBeforeFreeze = 1.0;
	}
	_ourTimeAtCorrectedZero = _ourTimeAtCorrectedZero - (appropriateCorrectedTime(_useSmoothTime) * _warpBeforeFreeze);
	_warp = _warpBeforeFreeze;
	_cycle = cycleFromWarp(_warp);
    }
}

// Freeze or unfreeze
void
ESWatchTime::toggleStop() {
    if (_warp == 0.0) {
	start();
    } else {
	stop();
    }
}

// Reverse direction
void
ESWatchTime::reverse() {
    _warp = -_warp;
    _warpBeforeFreeze = -_warpBeforeFreeze;
}

// Do this once for a given watchTime if it wants to use smooth time (i.e., is relative not absolute)
// BEFORE it does anything at all
void
ESWatchTime::setUseSmoothTime(bool newUseSmoothTime) {
    _useSmoothTime = newUseSmoothTime;
}

// Like toggleStop, but if stopping then round value to nearest rounding value
// (useful for stopwatches, so the restart time is exactly where it purports to be)
void
ESWatchTime::toggleStopWithRounding(double rounding) {
    if (_warp == 0.0) {
	start();
    } else {
	stop();
//	_ourTimeAtCorrectedZero = round(_ourTimeAtCorrectedZero / rounding) * rounding;
	// Well actually, it looks better if it truncates instead of rounding
	_ourTimeAtCorrectedZero = floor(_ourTimeAtCorrectedZero / rounding) * rounding;
    }
}

// Initialize and set value according to recorded defaults
void
ESWatchTime::stopwatchInitStoppedReading(double interval) {
    ESAssert(_useSmoothTime);
    _warp = 0.0;
    _cycle = ESWatchCyclePaused;
    _ourTimeAtCorrectedZero = interval;
}

// Initialize and set value according to recorded defaults
void
ESWatchTime::stopwatchInitRunningFromZeroTime(double zeroRTime) {
    ESAssert(_useSmoothTime);
    _warp = 1.0;
    _cycle = ESWatchCycleNormal;
    _ourTimeAtCorrectedZero = -zeroRTime;
}

void
ESWatchTime::stopwatchReset() {
    ESAssert(_useSmoothTime);
    if (_warp) {
	_ourTimeAtCorrectedZero = - _warp * ESTime::currentContinuousTime();
    } else {
	_ourTimeAtCorrectedZero = 0;
    }
}

// Copy lap time
void
ESWatchTime::copyLapTimeFromOtherTimer(ESWatchTime *otherTimer) {
    ESAssert(_useSmoothTime);
    ESAssert(otherTimer->_useSmoothTime);
    _ourTimeAtCorrectedZero = otherTimer->currentTime();
    _warp = 0.0;
    _cycle = ESWatchCyclePaused;
}

void
ESWatchTime::makeTimeIdenticalToOtherTimer(ESWatchTime *otherTimer) {
    ESAssert(_useSmoothTime == otherTimer->_useSmoothTime);
    _ourTimeAtCorrectedZero = otherTimer->_ourTimeAtCorrectedZero;
    _warp = otherTimer->_warp;
    _cycle = otherTimer->_cycle;
}

// Clone time + interval
void
ESWatchTime::makeTimeIdenticalToOtherTimer(ESWatchTime    *otherTimer,
                                           ESTimeInterval delta) {
    ESAssert(_useSmoothTime);
    ESAssert(!otherTimer->_useSmoothTime);
    //_ourTimeAtCorrectedZero = [TSTime rDateForNTPTime:otherTimer->_ourTimeAtCorrectedZero] + delta;
    _ourTimeAtCorrectedZero = otherTimer->_ourTimeAtCorrectedZero + delta;
    //printf("raw delta = %.2f\n", _ourTimeAtCorrectedZero - otherTimer->_ourTimeAtCorrectedZero);
    //printf("delta = %.2f => _ourTimeAtCorrectedZero %.2f\n", delta, _ourTimeAtCorrectedZero);
    //printf("...checking, my time - other timer = %.2f, dateROffset = %.2f, dateRSkew = %.2f, dateSkew = %.2f, ntp conversion delta is %.2f\n",
    //       ntpTimeForCTime(currentTime()) - otherTimer->currentTime(), ESTime::continuousOffset(), ESTime::continuousSkew(), ESTime::skew(), ntpTimeForCTime(currentTime()) - currentTime());
    //ESUtil::reportAllSkewsAndOffset("setting ESWatchTime");
    _warp = otherTimer->_warp;
    _cycle = otherTimer->_cycle;
}

double
ESWatchTime::offsetFromOtherTimer(ESWatchTime *otherTimer) {
    ESAssert(!otherTimer->_useSmoothTime);
    if (_warp == otherTimer->_warp) {
	return _ourTimeAtCorrectedZero - otherTimer->_ourTimeAtCorrectedZero;
    } else if (_useSmoothTime) {
	//ESUtil::reportAllSkewsAndOffset("reporting offset");
	return 
            ESTime::sysTimeForCTime(currentTime()) - otherTimer->currentTime();  // other timer is asserted not to be smooth
    } else {
	return currentTime() - otherTimer->currentTime();
    }
}

// Check for clone
bool
ESWatchTime::isIdenticalTo(ESWatchTime *otherTimer) {
    ESAssert(_useSmoothTime == otherTimer->_useSmoothTime);
    return _ourTimeAtCorrectedZero == otherTimer->_ourTimeAtCorrectedZero
	&& _warp == otherTimer->_warp;
}

// reset to a saved time
void
ESWatchTime::setToFrozenDateInterval(ESTimeInterval date) {
    _warp = 0.0;
    _cycle = ESWatchCyclePaused;
    _ourTimeAtCorrectedZero = date;
    checkAndConstrainAbsoluteTime();
}

// Reset to following actual iPhone time (subject to calendar installed)
void
ESWatchTime::resetToLocal() {
    _ourTimeAtCorrectedZero = 0;
    _warp = 1.0;
    _cycle = ESWatchCycleNormal;
}

bool
ESWatchTime::frozen() {
    return (_warp == 0.0);
}

bool
ESWatchTime::isCorrect() {
    return (_warp == 1.0 && _ourTimeAtCorrectedZero == 0);
}

bool
ESWatchTime::lastMotionWasInReverse() {
    return _warp < 0 || (_warp == 0 && _warpBeforeFreeze < 0);
}

// Change _warp speed, recalculating internal data as necessary
void
ESWatchTime::setWarp(double newWarp) {
    if (newWarp == 0) {
	stop();
    } else {
	if (_warp == 0) {  // need to re-incorporate skew
	    _ourTimeAtCorrectedZero = _ourTimeAtCorrectedZero - (appropriateCorrectedTime(_useSmoothTime) * newWarp);
	} else {
	    // yold = ynew
	    // mold * x + bold + skew = mnew * x + bnew + skew
	    // (mold - mnew) * x = bnew - bold
	    // bnew = bold + (mold - mnew) * x
	    _ourTimeAtCorrectedZero += (_warp - newWarp) * appropriateCorrectedTime(_useSmoothTime);
	}
	_warp = newWarp;
	_cycle = cycleFromWarp(_warp);
    }
}

ESTimeInterval
ESWatchTime::tzOffsetUsingEnv(ESTimeEnvironment *env) {
    double now = currentTime();
    if (env->_prevTimeForTZ == now) {
	//printf("returning previous tzOffset at date ");
	//printADateWithTimeZone(now, env->estz());
	//printf("\n");
    } else {
	env->_prevTimeForTZ = now;
	env->_resultForTZ = ESCalendar_tzOffsetForTimeInterval(env->estz(), now);
        // ESErrorReporter::logInfo("ESWatchTime::tzOffsetUsingEnv", 
        //                          ESUtil::stringWithFormat("TZ %s offset %g at %s",
        //                                                   ESCalendar_timeZoneName(env->estz()).c_str(),
        //                                                   env->_resultForTZ,
        //                                                   ESTime::timeAsString(now, 0,
        //                                                                        env->estz(), 
        //                                                                        ESCalendar_abbrevName(env->estz()
        //                                                                                              ).c_str()
        //                                                                        ).c_str()
        //                                                   ).c_str()
        //                          );
    }
    return env->_resultForTZ;
}

int
ESWatchTime::numberOfDaysOffsetFrom(ESWatchTime       *other,
                                    ESTimeEnvironment *env1,
                                    ESTimeEnvironment *env2) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env1->estz(), &cs);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval myMidnight = ESCalendar_timeIntervalFromLocalDateComponents(env1->estz(), &cs);
    ESCalendar_localDateComponentsFromTimeInterval(other->currentTime(), env2->estz(), &cs);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval otherMidnight = ESCalendar_timeIntervalFromLocalDateComponents(env2->estz(), &cs);
    ESTimeInterval deltaSeconds = myMidnight - otherMidnight;
    return rint(deltaSeconds/(24 * 3600));
}

// Number of days between two times (if this is Wed and other's Thu, answer is -1)
int
ESWatchTime::numberOfDaysOffsetFrom(ESWatchTime       *other,
                                    ESTimeEnvironment *env) {
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(currentTime(), env->estz(), &cs);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval myMidnight = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    ESCalendar_localDateComponentsFromTimeInterval(other->currentTime(), env->estz(), &cs);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval otherMidnight = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    ESTimeInterval deltaSeconds = myMidnight - otherMidnight;
    return rint(deltaSeconds/(24 * 3600));
}

// 1d 4:00:00 (localized (not yet))
std::string
ESWatchTime::representationOfDeltaOffsetUsingEnv(ESTimeEnvironment *env) {
    ESAssert(!_useSmoothTime);
    double delta;
    if (_warp) {
//    delta = currentDate - actualDate
//    delta = currentTime() - (ESSystemTimeBase::currentSystemTime() + skew);
//    delta = (_warp * ESSystemTimeBase::currentSystemTime() + _ourTimeAtCorrectedZero + skew) - (ESSystemTimeBase::currentSystemTime() + skew);
//    delta = _warp * ESSystemTimeBase::currentSystemTime() + _ourTimeAtCorrectedZero - ESSystemTimeBase::currentSystemTime();
	delta = (_warp - 1) * ESSystemTimeBase::currentSystemTime() + _ourTimeAtCorrectedZero;
    } else {
//    delta = currentDate - actualDate
//    delta = currentTime() - (ESSystemTimeBase::currentSystemTime() + skew);
//    delta = _ourTimeAtCorrectedZero - (ESSystemTimeBase::currentSystemTime() + skew);
	delta = _ourTimeAtCorrectedZero - ESTime::currentTime();
    }
    
    const char *sign;
    ESTimeInterval now = currentTime();
    ESTimeInterval time1;
    ESTimeInterval time2;
    if (delta > 0) {
	sign = "+";
	time1 = now - delta;
	time2 = now;
    } else {
	sign = "-";
	time1 = now;
	time2 = now - delta;
	delta = -delta;  // futureproof -- in case we specify fractional seconds at some point
	delta = delta;   // for now, shut up the warning
    }
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromDeltaTimeInterval(env->estz(), time1, time2, &cs);

    if (cs.year != 0) {
	return ESUtil::stringWithFormat("%s%dy %dd %02d:%02d:%02d", sign, cs.year, cs.day, cs.hour, cs.minute, (int)floor(cs.seconds));
    } else if (cs.day != 0) {
	return ESUtil::stringWithFormat("%s%dd %02d:%02d:%02d", sign, cs.day, cs.hour, cs.minute, (int)floor(cs.seconds));
    } else {
	return ESUtil::stringWithFormat("%s%02d:%02d:%02d", sign, cs.hour, cs.minute, (int)floor(cs.seconds));
    }
}

// 1 day/s (localized (not yet))
std::string
ESWatchTime::representationOfWarp() {
    if (_warp == 0) {
	return "Stopped";
    } else if (_warp < 0) {
	if (_warp > -60) {
	    return ESUtil::stringWithFormat("%gx", _warp);
	} else if (_warp > -3600) {
	    return ESUtil::stringWithFormat("%g min/sec", _warp/60);
	} else if (_warp > -24 * 3600) {
	    return ESUtil::stringWithFormat("%g hr/sec", _warp/3600);
	} else {
	    double rate = _warp/(3600*24);
	    const char *plural = ((fabs(rate) - 1) < 0.001) ? "" : "s";
	    return ESUtil::stringWithFormat("%g day%s/sec", rate, plural);
	}
    } else {
	if (_warp < 60) {
	    return ESUtil::stringWithFormat("%gx", _warp);
	} else if (_warp < 3600) {
	    return ESUtil::stringWithFormat("%g min/sec", _warp/60);
	} else if (_warp < 24 * 3600) {
	    return ESUtil::stringWithFormat("%g hr/sec", _warp/3600);
	} else {
	    double rate = _warp/(3600*24);
	    const char *plural = ((fabs(rate) - 1) < 0.001) ? "" : "s";
	    return ESUtil::stringWithFormat("%g day%s/sec", rate, plural);
	}
    }
}

// If pause, then resume from current position at normal speed (_warp == 1); if not paused, then pause
void
ESWatchTime::pausePlay() {
    if (_warp == 0) {
	setWarp(1);
	_cycle = ESWatchCycleNormal;
    } else {
	stop();
	_cycle = ESWatchCyclePaused;
    }
}

// Cycle through speeds
void
ESWatchTime::cycleFastForward() {
    if (_cycle >= ESWatchCycleFirst && _cycle <= ESWatchCycleLast) {
	ESWatchTimeCycle newCycle = nextCycleForFF[_cycle];
	setWarp(warpValuesForCycle[newCycle]);
	_cycle = newCycle;
    } else {
	setWarp(1);
	_cycle = ESWatchCycleNormal;
    }
}

// Cycle through speeds
void
ESWatchTime::cycleRewind() {
    if (_cycle >= ESWatchCycleFirst && _cycle <= ESWatchCycleLast) {
	ESWatchTimeCycle newCycle = nextCycleForRewind[_cycle];
	setWarp(warpValuesForCycle[newCycle]);
	_cycle = newCycle;
    } else {
	setWarp(1);
	_cycle = ESWatchCycleNormal;
    }
}

static ESTimeInterval onEvenTime(double t, double interval) {
    ESTimeInterval nt = floor(t / interval) * interval;
    if (nt < t) {
	nt += interval;
    }
    return nt;
}

static ESTimeInterval onEvenTimeBack(double t, double interval) {
    ESTimeInterval nt = ceil(t / interval) * interval;
    if (nt > t) {
	nt -= interval;
    }
    return nt;
}

// Amount to move forward or backwards before calculating the next place to stop
ESTimeInterval
ESWatchTime::gapJumpBeforeInterval() {
    if (_warp >= 0) {
	// Stopping just before midnight, so jump past
	return   kESWatchTimeAdvanceGap + 1;
    } else {
	// Stopping just after midnight, so jump back
	return - kESWatchTimeAdvanceGap - 1;
    }
}

ESTimeInterval
ESWatchTime::offsetAfterJump() {
    if (_warp >= 0) {
	return - kESWatchTimeAdvanceGap;
    } else {
	return kESWatchTimeAdvanceGap;
    }
}

ESTimeInterval
ESWatchTime::preAdvanceForNextAtTime(ESTimeInterval currentLocal,
                                     ESTimeInterval interval) {
    if (_warp >= 0) {
	return currentLocal + kESWatchTimeAdvanceGap + 1;
    } else {
	return currentLocal + (interval/2);
    }
}

ESTimeInterval
ESWatchTime::preAdvanceForPrevAtTime(ESTimeInterval currentLocal,
                                     ESTimeInterval interval) {
    if (_warp >= 0) {
	return currentLocal - (interval/2);
    } else {
	return currentLocal - kESWatchTimeAdvanceGap - 1;
    }
}

void
ESWatchTime::advanceToNextInterval(ESTimeInterval    interval,
                                   ESTimeEnvironment *env) {
    ESTimeInterval tzOff = tzOffsetUsingEnv(env);
    ESTimeInterval currentTimeGMT = currentTime();
    ESTimeInterval currentTimeLocal = currentTimeGMT + tzOff;
    ESTimeInterval newTimeLocal = onEvenTime(preAdvanceForNextAtTime(currentTimeLocal, interval/*andInterval*/), interval);
    newTimeLocal += offsetAfterJump();
    _ourTimeAtCorrectedZero += (newTimeLocal - currentTimeLocal);
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceToPrevInterval(ESTimeInterval    interval,
                                   ESTimeEnvironment *env) {
    ESTimeInterval tzOff = tzOffsetUsingEnv(env);
    ESTimeInterval currentTimeGMT = currentTime();
    ESTimeInterval currentTimeLocal = currentTimeGMT + tzOff;
    ESTimeInterval newTimeLocal = onEvenTimeBack(preAdvanceForPrevAtTime(currentTimeLocal, interval/*andInterval*/), interval);
    newTimeLocal += offsetAfterJump();
    _ourTimeAtCorrectedZero += (newTimeLocal - currentTimeLocal);
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceToNextMonthUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval curTime = currentTime();
    ESTimeInterval preTime = preAdvanceForNextAtTime(curTime, (3600 * 24 * 15)/*andInterval*/);
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(preTime, env->estz(), &cs);
    // Find the first of this month
    cs.day = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisMonth = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    // Add one month
    ESTimeInterval firstOfNextMonth = ESCalendar_addMonthsToTimeInterval(firstOfThisMonth, env->estz(), 1);
    _ourTimeAtCorrectedZero += (firstOfNextMonth - curTime + offsetAfterJump());
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceToPrevMonthUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval curTime = currentTime();
    ESTimeInterval preTime = preAdvanceForPrevAtTime(curTime, (3600 * 24 * 15)/*andInterval*/);
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(preTime, env->estz(), &cs);
    // Find the first of this month
    cs.day = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisMonth = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    _ourTimeAtCorrectedZero += (firstOfThisMonth - curTime + offsetAfterJump());
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceToNextYearUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval curTime = currentTime();
    ESTimeInterval preTime = preAdvanceForNextAtTime(curTime, (3600 * 24 * 15)/*andInterval*/);
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(preTime, env->estz(), &cs);
    // Find the first of this year
    cs.day = 1;
    cs.month = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisYear = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    // Add one year
    ESTimeInterval firstOfNextYear = ESCalendar_addYearsToTimeInterval(firstOfThisYear, env->estz(), 1);
    _ourTimeAtCorrectedZero += (firstOfNextYear - curTime + offsetAfterJump());
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceToPrevYearUsingEnv(ESTimeEnvironment *env) {
    ESTimeInterval curTime = currentTime();
    ESTimeInterval preTime = preAdvanceForNextAtTime(curTime, (3600 * 24 * 15)/*andInterval*/);
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(preTime, env->estz(), &cs);
    // Find the first of this year
    cs.day = 1;
    cs.month = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    ESTimeInterval firstOfThisYear = ESCalendar_timeIntervalFromLocalDateComponents(env->estz(), &cs);
    _ourTimeAtCorrectedZero += (firstOfThisYear - curTime + offsetAfterJump());
    checkAndConstrainAbsoluteTime();
}

// Advance to next point (context sensitive)
void
ESWatchTime::advanceToNextUsingEnv(ESTimeEnvironment *env) {
    switch (_cycle) {
      case ESWatchCycleFF6: // 5x24x3600x
      case ESWatchCycleReverseFF6:
	advanceToNextYearUsingEnv(env);
	break;

      case ESWatchCycleFF5: // 24x3600x
      case ESWatchCycleReverseFF5:
	advanceToNextMonthUsingEnv(env);
	break;

      case ESWatchCycleFF2: // 60x
      case ESWatchCycleReverseFF2:
	advanceToNextInterval(3600, env/*usingEnv*/);
	break;

      case ESWatchCycleFF1: // 5x
      case ESWatchCycleReverseFF1:
	advanceToNextInterval(60, env/*usingEnv*/);
	break;

      case ESWatchCycleSlow1: // 0.1
	advanceToNextInterval(1, env/*usingEnv*/);
	break;

      case ESWatchCycleReverseSlow1:
	advanceToNextInterval(1, env/*usingEnv*/);
	break;

      default:
      case ESWatchCycleOther:
      case ESWatchCycleNormal:
      case ESWatchCyclePaused:
      case ESWatchCycleReverse:
      case ESWatchCycleFF3: // 3600x
      case ESWatchCycleReverseFF3:
      case ESWatchCycleFF4: // 5x3600x
      case ESWatchCycleReverseFF4:
	advanceToNextInterval((24 * 3600), env/*usingEnv*/);
	break;
    }
}

// Advance to prev point (context sensitive)
void
ESWatchTime::advanceToPreviousUsingEnv(ESTimeEnvironment *env) {
    switch (_cycle) {
      case ESWatchCycleFF6: // 5x24x3600x
      case ESWatchCycleReverseFF6:
	advanceToPrevYearUsingEnv(env);
	break;

      case ESWatchCycleFF5: // 24x3600x
      case ESWatchCycleReverseFF5:
	advanceToPrevMonthUsingEnv(env);
	break;

      case ESWatchCycleFF2: // 60x
      case ESWatchCycleReverseFF2:
	advanceToPrevInterval(3600, env/*usingEnv*/);
	break;

      case ESWatchCycleFF1: // 5x
      case ESWatchCycleReverseFF1:
	advanceToPrevInterval(60, env/*usingEnv*/);
	break;

      case ESWatchCycleSlow1: // 0.1
	advanceToPrevInterval(1, env/*usingEnv*/);
	break;

      case ESWatchCycleReverseSlow1:
	advanceToPrevInterval(1, env/*usingEnv*/);
	break;

      default:
      case ESWatchCycleOther:
      case ESWatchCycleNormal:
      case ESWatchCyclePaused:
      case ESWatchCycleReverse:
      case ESWatchCycleFF3: // 3600x
      case ESWatchCycleReverseFF3:
      case ESWatchCycleFF4: // 5x3600x
      case ESWatchCycleReverseFF4:
	advanceToPrevInterval((24 * 3600), env/*usingEnv*/);
	break;
    }
}

void
ESWatchTime::advanceBySeconds(double         numSeconds,
                              ESTimeInterval startTime) {
    if (!isnan(startTime)) {
	double timeSinceStartDate = currentTime() - startTime;
	_ourTimeAtCorrectedZero += (numSeconds - timeSinceStartDate);
    } else {
	_ourTimeAtCorrectedZero += numSeconds;
    }
    checkAndConstrainAbsoluteTime();
}

void
ESWatchTime::advanceByDays(int               intDays,
                           ESTimeInterval    startTime,
                           ESTimeEnvironment *env) { // works on DST days
    //printf("advanceByDays %d using timezone %s\n", intDays, env->timeZone()->description().c_str());
    if (isnan(startTime)) {
	startTime = currentTime();
    }
    ESTimeInterval sameTimeNextDay = ESCalendar_addDaysToTimeInterval(startTime, env->estz(), intDays);
    advanceBySeconds((sameTimeNextDay - startTime), startTime/*fromTime*/);
}

void
ESWatchTime::advanceOneDayUsingEnv(ESTimeEnvironment *env) { // works on DST days
    advanceByDays(1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceByMonths(int               intMonths,
                             ESTimeInterval    startTime,
                             ESTimeEnvironment *env) {  // keeping day the same, unless there's no such day, in which case go to last day of month
    if (isnan(startTime)) {
	startTime = currentTime();
    }
    ESTimeInterval sameTimeNextMonth = ESCalendar_addMonthsToTimeInterval(startTime, env->estz(), intMonths);
    advanceBySeconds((sameTimeNextMonth - startTime), startTime/*fromTime*/);
}

void
ESWatchTime::advanceOneMonthUsingEnv(ESTimeEnvironment *env) {  // keeping day the same, unless there's no such day, in which case go to last day of month
    advanceByMonths(1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceByYears(int               numYears,
                            ESTimeInterval    startTime,
                            ESTimeEnvironment *env) {   // keeping month and day the same, unless it was Feb 29, in which case move back to Feb 28
    if (isnan(startTime)) {
	startTime = currentTime();
    }
    ESTimeInterval sameTimeNextYear = ESCalendar_addYearsToTimeInterval(startTime, env->estz(), numYears);
    advanceBySeconds((sameTimeNextYear - startTime), startTime/*fromTime*/);
}

void
ESWatchTime::advanceOneYearUsingEnv(ESTimeEnvironment *env) {   // keeping month and day the same, unless it was Feb 29, in which case move back to Feb 28
    advanceByYears(1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::retardOneDayUsingEnv(ESTimeEnvironment *env) { // works on DST days
    advanceByDays(-1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::retardOneMonthUsingEnv(ESTimeEnvironment *env) {
    advanceByMonths(-1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::retardOneYearUsingEnv(ESTimeEnvironment *env) {
    advanceByYears(-1, currentTime()/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceByYears(int               numYears,
                            ESTimeEnvironment *env) {
    advanceByYears(numYears, nan("")/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceByMonths(int               numMonths,
                             ESTimeEnvironment *env) {
    advanceByMonths(numMonths, nan("")/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceByDays(int               numDays,
                           ESTimeEnvironment *env) {
    advanceByDays(numDays, nan("")/*fromTime*/, env/*usingEnv*/);
}

void
ESWatchTime::advanceBySeconds(double numSeconds) {
    advanceBySeconds(numSeconds, nan("")/*fromTime*/);
}

void
ESWatchTime::advanceToQuarterHourUsingEnv(ESTimeEnvironment *env) {
    double ct = currentTime();
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(ct, env->estz(), &cs);
    double secondsSinceHour = cs.minute * 60 + cs.seconds;
    if (secondsSinceHour < 15*60) {
	advanceBySeconds((15*60 - secondsSinceHour), nan("")/*fromTime*/);
    } else if (secondsSinceHour < 30*60) {
	advanceBySeconds((30*60 - secondsSinceHour), nan("")/*fromTime*/);
    } else if (secondsSinceHour < 45*60) {
	advanceBySeconds((45*60 - secondsSinceHour), nan("")/*fromTime*/);
    } else {
	advanceBySeconds((60*60 - secondsSinceHour), nan("")/*fromTime*/);
    }
}

void
ESWatchTime::retreatToQuarterHourUsingEnv(ESTimeEnvironment *env) {
    double ct = currentTime();
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(ct, env->estz(), &cs);
    double fudge = 0.01;
    double secondsSinceHour = cs.minute * 60 + cs.seconds - fudge;
    if (secondsSinceHour < 0) {
	secondsSinceHour += 60*60;
    }
    if (secondsSinceHour < 15*60) {
	advanceBySeconds((-fudge - secondsSinceHour), nan("")/*fromTime*/);
    } else if (secondsSinceHour < 30*60) {
	advanceBySeconds((-fudge + 15*60 - secondsSinceHour), nan("")/*fromTime*/);
    } else if (secondsSinceHour < 45*60) {
	advanceBySeconds((-fudge + 30*60 - secondsSinceHour), nan("")/*fromTime*/);
    } else {
	advanceBySeconds((-fudge + 45*60 - secondsSinceHour), nan("")/*fromTime*/);
    }
}

// *****************
// Debug methods
// *****************

// Note: The following routine adds one to the returned month and day, for readability
std::string
ESWatchTime::dumpAllUsingEnv(ESTimeEnvironment *env) {
    // Temporarily stop the watch so the values are consistent, then restore when we're done
    double oldWarp = _warp;
    double oldTimeAtCorrectedZero = _ourTimeAtCorrectedZero;
    _ourTimeAtCorrectedZero = currentTime();
    _warp = 0.0;
    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const std::string &returnString = ESUtil::stringWithFormat("Watch time Numbers:\n  %d/%02d/%02d %02d:%02d:%02d %s\nWatch time Values:\n   year: %5d\n  month: %10.4f\n    day: %10.4f\n hour24: %10.4f\n hour12: %10.4f\n minute: %10.4f\n second: %10.4f\nWeekday: %10.4f\n",
                                                               yearNumberUsingEnv(env),
                                                               monthNumberUsingEnv(env) + 1,
                                                               dayNumberUsingEnv(env) + 1,
                                                               hour24NumberUsingEnv(env),
                                                               minuteNumberUsingEnv(env),
                                                               secondNumberUsingEnv(env),
                                                               days[weekdayNumberUsingEnv(env)],
                                                               yearNumberUsingEnv(env),
                                                               monthValueUsingEnv(env) + 1.0,
                                                               dayValueUsingEnv(env) + 1.0,
                                                               hour24ValueUsingEnv(env),
                                                               hour12ValueUsingEnv(env),
                                                               minuteValueUsingEnv(env),
                                                               secondValueUsingEnv(env),
                                                               weekdayValueUsingEnv(env)
			      );
    _warp = oldWarp;
    _ourTimeAtCorrectedZero = oldTimeAtCorrectedZero;
    return returnString;
}

