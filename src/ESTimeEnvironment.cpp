//
//  ESTimeEnvironment.cpp
//
//  Created by Steven Pucci 29 May 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTimeEnvironment.hpp"
#include "ESErrorReporter.hpp"
#include "ESLock.hpp"
#include "ESUtil.hpp"
#include "ESThread.hpp"

#include <math.h>

static ESLock tzLock;
bool haveTZLock = false;

ESTimeEnvironment::ESTimeEnvironment(const char *timeZoneName,
                                     bool       observingIPhoneTime) {
    _estz = ESCalendar_initTimeZoneFromOlsonID(timeZoneName);
    _observingIPhoneTime = observingIPhoneTime;
    _prevTimeForTZ = 0;
    _cacheMidnightBase = ESFarInThePast;
}

/*virtual*/ 
ESTimeEnvironment::~ESTimeEnvironment() {
    ESCalendar_releaseTimeZone(_estz);
}


#define ECMidnightCacheSpan    (30 * 24 * 3600)  // 30 days total span
#define ECMidnightCachePreload (15 * 24 * 3600)  // starting 15 days back

ESTimeInterval 
ESTimeEnvironment::midnightForTimeInterval(ESTimeInterval timeInterval) {
    // if (timeInterval <= _cacheMidnightBase || timeInterval >= _cacheMidnightBase + ECMidnightCacheSpan || timeInterval >= _cacheDSTEvent) {
    if (timeInterval <= _cacheMidnightBase || timeInterval >= _cacheMidnightBase + ECMidnightCacheSpan) {
	double firstDay = timeInterval - ECMidnightCachePreload;
	ESDateComponents cs;
	ESCalendar_localDateComponentsFromTimeInterval(firstDay, _estz, &cs);
	cs.hour = 0;
	cs.minute = 0;
	cs.seconds = 1;  // 1 second past midnight
	ESTimeInterval firstMidnight = ESCalendar_timeIntervalFromLocalDateComponents(_estz, &cs);
	_cacheMidnightBase = firstMidnight - 1;
	ESTimeInterval nextTZChange = ESCalendar_nextDSTChangeAfterTimeInterval(_estz, firstMidnight);
	if (nextTZChange) {
	    _cacheDSTEvent = nextTZChange;
	    if (_cacheDSTEvent <= _cacheMidnightBase + ECMidnightCacheSpan) {
		ESTimeInterval postTransitionTimeInterval = _cacheDSTEvent + 1;
		ESTimeInterval preTransitionTimeInterval = _cacheDSTEvent - 1;
		ESCalendar_localDateComponentsFromTimeInterval(postTransitionTimeInterval, _estz, &cs);
		double postTransitionTime = cs.hour * 3600 + cs.minute * 60 + cs.seconds;
		ESCalendar_localDateComponentsFromTimeInterval(preTransitionTimeInterval, _estz, &cs);
		double preTransitionTime = cs.hour * 3600 + cs.minute * 60 + cs.seconds;
		if (fabs(postTransitionTime - preTransitionTime) > 3600 * 12) {  // Must be different days
		    if (postTransitionTime > preTransitionTime) {
			postTransitionTime -= 3600 * 24;
		    } else {
			preTransitionTime -= 3600 * 24;
		    }
		}
		_cacheDSTDelta = postTransitionTime - preTransitionTime - 2;
	    } else {
		// _cacheDSTEvent = ESFarInTheFuture;  // Don't throw this info away -- we need  for -[ESWatchTime nextDSTChangeUsingEnv:]
		_cacheDSTDelta = 0;  // But we don't need the delta in that method
	    }
	} else {
	    _cacheDSTEvent = ESFarFarInTheFuture;
	    _cacheDSTDelta = 0;
	}
    }
    double midnightBase = (timeInterval < _cacheDSTEvent) ? _cacheMidnightBase : _cacheMidnightBase - _cacheDSTDelta;
    double timeSinceMidnight = ESUtil::fmod(timeInterval - midnightBase, 24 * 3600);
    return timeInterval - timeSinceMidnight;
}

void 
ESTimeEnvironment::setTimeZone(ESTimeZone *newTimeZone) {
    ESAssert(ESThread::inMainThread()); // else our bool haveTZLock is unreliable
    bool wasLocked = haveTZLock;
    if (!wasLocked) {
	tzLock.lock();
	haveTZLock = true;
    }
    ESErrorReporter::logInfo("ESTimeEnvironment::setTimeZone", "releasing _estz");
    ESCalendar_releaseTimeZone(_estz);
    if (newTimeZone) {
        ESErrorReporter::logInfo("ESTimeEnvironment::setTimeZone", "retaining passed-in newTimeZone");
	_estz = ESCalendar_retainTimeZone(newTimeZone);
	_timeZoneIsDefault = false;
    } else {
	ESAssert(_observingIPhoneTime);
        ESErrorReporter::logInfo("ESTimeEnvironment::setTimeZone", "retaining local time zone");
	_estz = ESCalendar_retainTimeZone(timeZoneWhenNoneSpecified());
	_timeZoneIsDefault = true;
    }
    handleNewTimeZone();
    if (!wasLocked) {
	tzLock.unlock();
	haveTZLock = false;
    }
}

void 
ESTimeEnvironment::clearCache() {
    ESAssert(ESThread::inMainThread()); // else our bool haveTZLock is unreliable
    bool wasLocked = haveTZLock;
    if (!wasLocked) {
	tzLock.lock();
	haveTZLock = true;
    }
    _prevTimeForTZ = 0;
    _cacheMidnightBase = ESFarInThePast;
    if (!wasLocked) {
	tzLock.unlock();
	haveTZLock = false;
    }
}

void
ESTimeEnvironment::handleNewTimeZone() {
    ESAssert(ESThread::inMainThread()); // else our bool haveTZLock is unreliable
    bool wasLocked = haveTZLock;
    if (!wasLocked) {
	tzLock.lock();
	haveTZLock = true;
    }
    clearCache();
    if (!wasLocked) {
	tzLock.unlock();
	haveTZLock = false;
    }
}

std::string 
ESTimeEnvironment::timeZoneName() {
    return ESCalendar_timeZoneName(_estz);
}

/*static*/ void 
ESTimeEnvironment::lockForBGTZAccess() {
    ESAssert(!ESThread::inMainThread());  // Otherwise we have serious issues with recursive locks
    tzLock.lock();
}

/*static*/ void 
ESTimeEnvironment::unlockForBGTZAccess() {
    ESAssert(!ESThread::inMainThread());  // Otherwise we have serious issues with recursive locks
    tzLock.unlock();
}

/* static */ ESTimeZone  *
ESTimeEnvironment::timeZoneWhenNoneSpecified() {
    // [NS-TimeZone resetSystemTimeZone];  // FIX: This can apparently make systemTimeZone slow (kernel call?)
    return ESCalendar_localTimeZone();
}

