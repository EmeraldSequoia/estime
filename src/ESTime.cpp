//
//  ESTime.cpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTime.hpp"
#include "ESTimeSourceDriver.hpp"
#include "ESSystemTimeDriver.hpp"
#include "ESSystemTimeBase.hpp"
#include "ESUtil.hpp"
#include "ESLock.hpp"
#include "ESCalendar.hpp"
#include "ESThread.hpp"
#include "ESLeapSecond.hpp" // For test
#include "ESTimer.hpp"

#include "ESNTPDriver.hpp"
#include "ESFakeTimeDriver.hpp"
#include "ESXGPS150TimeDriver.hpp"

#include <math.h>
#include <assert.h>

#include <list>

// Class static member variables
/*static*/ ESTimeSourceDriver *ESTime::_bestDriver;
/*static*/ std::list<ESTimeSourceDriver *> *ESTime::_drivers;
/*static*/ ESTimeInterval      ESTime::_nextLeapSecondDate = ESFarFarInTheFuture;  // Meaning "never"
/*static*/ ESTimeInterval      ESTime::_nextLeapSecondDelta = 0;

// File static variables
static std::list<ESTimeSyncObserver *> *timeSyncObservers;
static ESLock *printfLock;
static double startOfMainTime;
static double startOfMainCTime;
static double lastTimeNoted = -1;
static double lastCTimeNoted = -1;
static bool initialized = false;


/*static*/ std::string 
ESTime::timeAsString(ESTimeInterval dt,
                     int            framesPerSecond,
                     ESTimeZone     *estz,
                     const char     *tzAbb) {
    if (dt == -1) {
        dt = currentTime();
    }
    if (!estz) {
        estz = ESCalendar_localTimeZone();
        tzAbb = "LT";
    }
    std::string tzAbbrev = tzAbb ? tzAbb : ESCalendar_abbrevName(estz);
    ESDateComponents ltcs;
    ESCalendar_localDateComponentsFromTimeInterval(dt, estz, &ltcs);
    double fractionalSeconds = ltcs.seconds - floor(ltcs.seconds);
    if (framesPerSecond) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%d", framesPerSecond - 1);
        int lengthOfMaxFrame = (int)strlen(buf);
        char fmt[60];
        snprintf(fmt, sizeof(fmt), "%%d %%04d/%%02d/%%02d %%02d:%%02d:%%02d*%%0%dd.%%02d (%%.5f) %%s", lengthOfMaxFrame);
        double frames = fractionalSeconds * framesPerSecond;
        int framesI = (int)floor(frames);
        int frameHundredths = round((frames - framesI) * 100);
        if (frameHundredths >= 100) {
            framesI++;
            frameHundredths -= 100;
        }
        int secondsI = (int)floor(ltcs.seconds);
        if (framesI >= framesPerSecond) {
            secondsI++;
            framesI -= framesPerSecond;
        }
        if (secondsI >= 60) {
            ltcs.minute++;
            secondsI -= 60;
        }
        if (ltcs.minute >= 60) {
            ltcs.hour++;
            ltcs.minute -= 60;
        }
        return ESUtil::stringWithFormat(fmt,
                                        ltcs.era, ltcs.year, ltcs.month, ltcs.day, ltcs.hour, ltcs.minute, secondsI, framesI, frameHundredths, fractionalSeconds, tzAbbrev.c_str());
    } else {
        int secondsI = (int)floor(ltcs.seconds);
        int tenthousandths = round(fractionalSeconds * 10000);
        if (tenthousandths >= 10000) {
            secondsI++;
            tenthousandths -= 10000;
        }
        if (secondsI >= 60) {
            ltcs.minute++;
            secondsI -= 60;
        }
        if (ltcs.minute >= 60) {
            ltcs.hour++;
            ltcs.minute -= 60;
        }
        if (ltcs.hour >= 24) {
            ltcs.day++;
            ltcs.hour -= 24;
        }
        // OK, if we're at midnight on the new month boundary, we accept the error.
        return ESUtil::stringWithFormat("%d %04d/%02d/%02d %02d:%02d:%02d.%04d %s",
                                        ltcs.era, ltcs.year, ltcs.month, ltcs.day, ltcs.hour, ltcs.minute, secondsI, tenthousandths, tzAbbrev.c_str());
    }
}

static void
printADateWithTimeZoneAbbrev(ESTimeInterval dt,
			     ESTimeZone     *estz,
			     const char     *tzAbbrev,
                             int            framesPerSecond = 0) {
    ESErrorReporter::logInfo("printADateWithTimeZoneAbbrev", ESTime::timeAsString(dt, framesPerSecond, estz, tzAbbrev).c_str());
}

static void
printHMSWithTimeZoneAbbrev(ESTimeInterval dt,
                           ESTimeZone     *estz,
                           const char     *tzAbbrev,
                           int            framesPerSecond = 0) {
    ESDateComponents ltcs;
    ESCalendar_localDateComponentsFromTimeInterval(dt, estz, &ltcs);
    double fractionalSeconds = dt - floor(dt);
    if (framesPerSecond) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%d", framesPerSecond - 1);
        int lengthOfMaxFrame = (int)strlen(buf);
        char fmt[48];
        snprintf(fmt, sizeof(fmt), "%%02d:%%02d:%%02d*%%0%dd.%%02d %%s", lengthOfMaxFrame);
        double frames = fractionalSeconds * framesPerSecond;
        int framesI = (int)floor(frames);
        int frameHundredths = round((frames - framesI) * 100);
        if (frameHundredths >= 100) {
            framesI++;
            frameHundredths -= 100;
        }
        int secondsI = (int)floor(ltcs.seconds);
        if (framesI >= framesPerSecond) {
            secondsI++;
            framesI -= framesPerSecond;
        }
        if (secondsI >= 60) {
            ltcs.minute++;
            secondsI -= 60;
        }
        if (ltcs.minute >= 60) {
            ltcs.hour++;
            ltcs.minute -= 60;
        }
        printf(fmt,
               ltcs.hour, ltcs.minute, secondsI, framesI, frameHundredths, tzAbbrev);
    } else {
        int microseconds = round(fractionalSeconds * 1000000);
        printf("%02d:%02d:%02d.%06d %s",
               ltcs.hour, ltcs.minute, (int)floor(ltcs.seconds), microseconds, tzAbbrev);
    }
}
static void
printADate(ESTimeInterval dt,
           int            framesPerSecond = 0) {
    printADateWithTimeZoneAbbrev(dt, ESCalendar_localTimeZone(), "LT", framesPerSecond);
}

static void
printHMS(ESTimeInterval dt,
         int            framesPerSecond = 0) {
    printHMSWithTimeZoneAbbrev(dt, ESCalendar_localTimeZone(), "LT", framesPerSecond);
}

/*static*/ void
ESTime::printADateWithTimeZone(ESTimeInterval dt,
		       ESTimeZone     *estz,
                       int            framesPerSecond) {
    printADateWithTimeZoneAbbrev(dt, estz, "", framesPerSecond);
}

/*static*/ void
ESTime::noteTimeAtPhase(const char  *phaseName) {
    if (!printfLock) {
        //ESErrorReporter::logInfo("ESTime", "creating nTAP printf lock");
	printfLock = new ESLock;
    }
    printfLock->lock();
    double t = initialized ? currentTime() : ESSystemTimeBase::currentContinuousSystemTime();
    double tc = initialized ? cTimeForSysTime(t) : t;
    if (lastTimeNoted < 0) {
	printf("Phase time      Total ContinPhase  ContinTotal Description\n");
	printf("%10.4f %10.4f %10.4fC %10.4fC: ", 0.0, t - startOfMainTime, 0.0, tc - startOfMainCTime);
    } else {
	printf("%10.4f %10.4f %10.4fC %10.4fC: ", t - lastTimeNoted, t - startOfMainTime, tc - lastTimeNoted, tc - startOfMainCTime);
    }
#undef NTAP_FULL_TIMES
#ifdef NTAP_FULL_TIMES
    printTimes(phaseName, t);
#else
    printf("%s\n", phaseName);
#endif
    lastTimeNoted = t;
    lastCTimeNoted = tc;
    printfLock->unlock();
}

/*static*/ void 
ESTime::printTimes(const char     *phaseName,
                   ESTimeInterval whenNTP,
                   int            framesPerSecond) {
    if (whenNTP == -1) {
        whenNTP = ESTime::currentTime();
    }
    ESTimeInterval whenSkew = initialized ? skewForReportingPurposesOnly() : 0;
    ESTimeInterval whenSystem = whenNTP - whenSkew;
    printf("< ");
    printHMS(whenSystem);
    if (initialized) {
        printf("(system)%+5.3f == ", whenSkew);
    } else {
        printf("(system)+< uninit 0 > == ");
    }
    printADate(whenNTP, framesPerSecond);
    printf(" > %s\n", phaseName);
}

/*static*/ void 
ESTime::printContTimes(const char     *phaseName,
                       ESTimeInterval whenCont,
                       int            framesPerSecond) {
    printTimes(phaseName, ntpTimeForCTime(whenCont), framesPerSecond);
}

// Debug
#ifndef NDEBUG
/*static*/ void 
ESTime::reportAllSkewsAndOffset(const char  *description) {
    // Put some code here
}
#endif

// Public registration method to be informed when the sync changes
/*static*/ void 
ESTime::registerTimeSyncObserver(ESTimeSyncObserver *observer) {
    timeSyncObservers->push_back(observer);
}

/*static*/ void 
ESTime::unregisterTimeSyncObserver(ESTimeSyncObserver *observer) {
    timeSyncObservers->remove(observer);
}

// Methods to be called by app delegate
/*static*/ void 
ESTime::startOfMain(const char *fourByteAppSig) {
    startOfMainTime = ESSystemTimeBase::init();
    startOfMainCTime = cTimeForSysTime(startOfMainTime);
    ESUtil::registerNoteTimeAtPhaseCapability(&ESTime::noteTimeAtPhase);
    ESNTPDriver::setAppSignature(fourByteAppSig);
}

/*static*/ void
ESTime::init(unsigned int   makerFlags,
             ESTimeInterval fakeTimeForSync,
             ESTimeInterval fakeTimeError) {
    ESAssert(!initialized);
    timeSyncObservers = new std::list<ESTimeSyncObserver *>;
    ESTimer::init();
    ESCalendar_init();
    _bestDriver = NULL;
    ESAssert(!_drivers);
    _drivers = new std::list<ESTimeSourceDriver *>;
    if (makerFlags & ESNTPMakerFlag) {
        ESTimeSourceDriver *d = (*ESNTPDriver::makeOne)();
        _drivers->push_back(d);
        _bestDriver = d;
    }
#if ES_IOS
    if (makerFlags & ESXGPS150MakerFlag) {
        ESTimeSourceDriver *d = new ESXGPS150TimeDriver;
        _drivers->push_back(d);
        if (!_bestDriver) {
            _bestDriver = d;
        }
    }
#endif
    if (makerFlags & ESFakeMakerFlag) {
        ESTimeSourceDriver *d = new ESFakeTimeDriver(fakeTimeForSync, fakeTimeError);
        _drivers->push_back(d);
        ESAssert(!_bestDriver);  // fake driver only makes sense if it's the only source
        _bestDriver = d;
    } else {
        ESAssert(fakeTimeForSync == 0);
        ESAssert(fakeTimeError == 0);
    }
    if (makerFlags & ESSystemTimeMakerFlag) {
        ESTimeSourceDriver *d = new ESSystemTimeDriver;
        _drivers->push_back(d);
        ESAssert(!_bestDriver);  // system time driver only makes sense if it's the only source
        _bestDriver = d;
    }
    initialized = true;
#ifdef ESLEAPSECOND_TEST  // Defined (or not) in ESLeapSecond.hpp
    ESTestLeapSecond();
#endif
}

// This is called when the system is going down (as in Android, when switching away from a watch
// face).  The convention is that we should clear all resource usage, including threads, and we
// should assume that ESTime::init() will be called again before we need to do anything else.
/*static*/ void
ESTime::shutdown() {
    ESAssert(false);  // Nowhere near ready for prime time.  Problem is NTPDriver, hard to coordinate its soft releases with this.
    std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
    std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
    while(iter != end) {
        ESTimeSourceDriver *timeSourceDriver = *iter;
        timeSourceDriver->release();
        iter++;
    }    

    delete _drivers;
    _drivers = NULL;

    ESTimer::shutdown();

    initialized = false;
}

/*static*/ ESTimeInterval 
ESTime::adjustForLeapSecondGuts(ESTimeInterval rawUTC) {
    ESAssert(rawUTC >= _nextLeapSecondDate);  // otherwise should have been caught in calling inline
    if (_nextLeapSecondDelta > 0) {  // positive leap second, as with all of first 15+
        if (rawUTC > _nextLeapSecondDate + _nextLeapSecondDelta) {
            return rawUTC - _nextLeapSecondDelta;
        } else {
            return _nextLeapSecondDate - ESLeapSecondHoldShortInterval;  // We hold in place during the transition
        }
    } else {
        ESAssert(_nextLeapSecondDelta < 0);  // If we get here, we should have a zero delta
        return rawUTC - _nextLeapSecondDelta;  // Jump back; no transition period to worry about
    }
}

/*static*/ ESTimeInterval 
ESTime::adjustForLeapSecondGutsWithLiveCorrection(ESTimeInterval rawUTC,
                                                  ESTimeInterval *liveCorrection) {
    ESAssert(rawUTC >= _nextLeapSecondDate - 1.0);  // otherwise should have been caught in calling inline
    if (_nextLeapSecondDelta > 0) {  // positive leap second, as with all of first 15+
        ESTimeInterval timeSinceStart = rawUTC - _nextLeapSecondDate;  // Might be negative up to -1
        if (timeSinceStart > _nextLeapSecondDelta) {
            *liveCorrection = 0;
            return rawUTC - _nextLeapSecondDelta;
        } else {
            *liveCorrection = timeSinceStart + ESLeapSecondHoldShortInterval + 1;  // Live correction goes from ~0 (the second before, for rounding purposes) to 2 (after the leap)
            if (timeSinceStart > 0) {
                return _nextLeapSecondDate - ESLeapSecondHoldShortInterval;  // We hold in place during the transition
            } else {
                return rawUTC;  // During the lead-up second we proceed normally with UTC; only the live correction is active
            }
        }
    } else {
        ESAssert(_nextLeapSecondDelta < 0);  // If we get here, we should have a zero delta
        *liveCorrection = 0;  // No live correction for negative leap seconds
        return rawUTC - _nextLeapSecondDelta;  // Jump back; no transition period to worry about
    }
}

/*static*/ void 
ESTime::setupNextLeapSecondData() {
    ESTimeInterval utcNow = currentTime();
    _nextLeapSecondDate = ESLeapSecond::nextLeapSecondAfter(utcNow, &_nextLeapSecondDelta);
}

/*static*/ ESTimeInterval 
ESTime::notificationTimeForDST(ESTimeInterval secondsSinceMidnight,
                               bool           beforeNotAfter) {
    ESTimeZone *estz = ESCalendar_localTimeZone();  // Or should this be a parameter?
    ESTimeInterval now = ESTime::currentTime();
    ESTimeInterval nextDSTChange = ESCalendar_nextDSTChangeAfterTimeInterval(estz, now);
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(nextDSTChange, estz, &cs);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 1;  // 1 second past midnight
    ESTimeInterval midnightBeforeDST = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs) - 1;
    ESTimeInterval secondsFromMidnightToDST = nextDSTChange - midnightBeforeDST;
    ESAssert(secondsFromMidnightToDST >= 0);
    if (beforeNotAfter) {
        if (secondsFromMidnightToDST >= secondsSinceMidnight) {
            // requested time is already before dst change, just use it
            return midnightBeforeDST + secondsSinceMidnight;
        } else {
            // requested time is after dst change.  We want previous day.  We don't have to worry about
            // dst changes before midnightBeforeDST because that would mean two DST changes in a row on
            // two consecutive days, which we assert can't happen
            return midnightBeforeDST - (24 * 3600) + secondsSinceMidnight;
        }
    } else {
        if (secondsFromMidnightToDST <= secondsSinceMidnight) {
            // requested time is already after dst change, just use it
            return midnightBeforeDST + secondsSinceMidnight;
        } else {
            // requested time is before dst change.  We want next day.  This is more complicated because the dst event intervenes; let ESCalendar do it
            return ESCalendar_addDaysToTimeInterval(midnightBeforeDST + secondsSinceMidnight, estz, 1);
        }
    }
}

/*static*/ void 
ESTime::syncValueReallyChanged() {
    setupNextLeapSecondData();
    // Call observers "safely" with respect to removing the observer we're on
    {
        ESTimeSyncObserver *observer;
        std::list<ESTimeSyncObserver *>::iterator end = timeSyncObservers->end();
        std::list<ESTimeSyncObserver *>::iterator iter = timeSyncObservers->begin();
        if (iter != end) {
            observer = *iter;
            iter++;
            while (iter != end) {
                observer->callSyncValueChanged();  // Last one
                observer = *iter;
                iter++;
            }
            observer->callSyncValueChanged();  // Last one
        }
    }
}

/*static*/ void 
ESTime::syncStatusReallyChanged() {
    // Call observers "safely" with respect to removing the observer we're on
    {
        ESTimeSyncObserver *observer;
        std::list<ESTimeSyncObserver *>::iterator end = timeSyncObservers->end();
        std::list<ESTimeSyncObserver *>::iterator iter = timeSyncObservers->begin();
        if (iter != end) {
            observer = *iter;
            iter++;
            while (iter != end) {
                observer->callSyncStatusChanged();  // Last one
                observer = *iter;
                iter++;
            }
            observer->callSyncStatusChanged();  // Last one
        }
    }
}

/*static*/ void 
ESTime::syncValueChangedByDriver(ESTimeSourceDriver *driver) {
    ESAssert(_drivers);
    bool driverChanged = recheckForBestDriver();
    if (driverChanged) {
        syncValueReallyChanged();
        syncStatusReallyChanged();
    } else if (driver == _bestDriver) {
        syncValueReallyChanged();
    }
}

/*static*/ void 
ESTime::syncStatusChangedByDriver(ESTimeSourceDriver *driver) {
    ESAssert(_drivers);
    bool driverChanged = recheckForBestDriver();
    if (driverChanged) {
        syncValueReallyChanged();
        syncStatusReallyChanged();
    } else if (driver == _bestDriver) {
        syncStatusReallyChanged();
    }
}

/*static*/ void 
ESTime::continuousTimeReset() {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->resync(false/* !userRequested*/);
        }
    }
    // Call observers "safely" with respect to removing the observer we're on
    {
        ESTimeSyncObserver *observer;
        std::list<ESTimeSyncObserver *>::iterator end = timeSyncObservers->end();
        std::list<ESTimeSyncObserver *>::iterator iter = timeSyncObservers->begin();
        if (iter != end) {
            observer = *iter;
            iter++;
            while (iter != end) {
                observer->callContinuousTimeReset();  // Last one
                observer = *iter;
                iter++;
            }
            observer->callContinuousTimeReset();  // Last one
        }
    }
}

/*static*/ void 
ESTime::workingSyncValue(ESTimeInterval workingSyncValue,
                         ESTimeInterval workingSyncAccuracy) {
    // Call observers "safely" with respect to removing the observer we're on
    {
        ESTimeSyncObserver *observer;
        std::list<ESTimeSyncObserver *>::iterator end = timeSyncObservers->end();
        std::list<ESTimeSyncObserver *>::iterator iter = timeSyncObservers->begin();
        if (iter != end) {
            observer = *iter;
            iter++;
            while (iter != end) {
                observer->callWorkingSyncValueChanged(workingSyncValue, workingSyncAccuracy);  // last one
                observer = *iter;
                iter++;
            }
            observer->callWorkingSyncValueChanged(workingSyncValue, workingSyncAccuracy);  // last one
        }
    }
}

inline static bool
statusIsUsable(ESTimeSourceStatus status) {
    return
        status == ESTimeSourceStatusSynchronized ||
        status == ESTimeSourceStatusPollingSynchronized ||
        status == ESTimeSourceStatusFailedSynchronized ||
        status == ESTimeSourceStatusRefining ||
        status == ESTimeSourceStatusFailedRefining ||
        status == ESTimeSourceStatusNoNetSynchronized ||
        status == ESTimeSourceStatusNoNetRefining;
}

/*static*/ bool
ESTime::recheckForBestDriver() {
    ESTimeSourceDriver *oldBest = _bestDriver;
    if (_drivers) {
        ESAssert(oldBest);
        ESTimeInterval bestAccuracy = _bestDriver->currentTimeError();
        bool bestIsUsable = statusIsUsable(_bestDriver->currentStatus());
            
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            if (driver == oldBest) {  // Either oldBest is the same as _bestDriver or we've already determined that _bestDriver is better than oldBest
                continue;
            }
            bool thisOneUsable = statusIsUsable(driver->currentStatus());
            if (thisOneUsable) {
                ESTimeInterval thisAccuracy = driver->currentTimeError();
                if (!bestIsUsable || thisAccuracy < bestAccuracy) {
                    _bestDriver = driver;
                    bestIsUsable = true;
                    bestAccuracy = thisAccuracy;
                }
            }
        }
    }
    return oldBest != _bestDriver;
}

#define TS_VALUE_OF_NSTIME_AT_1970 -978307200.00   // obtained by asking for NSDate at 1/1/70 midnight UTC, verified to be 31 365-day years and 8 leap days exactly

/*static*/ ESTimeInterval
ESTime::gettimeofdayCont(struct timeval *tv) {
    ESTimeInterval rTime = currentContinuousTime();
    double timeSince1970 = rTime - TS_VALUE_OF_NSTIME_AT_1970;
    long intSeconds = (long)timeSince1970;
    tv->tv_sec = intSeconds;
    double fraction = timeSince1970 - intSeconds;
    tv->tv_usec = (int)(fraction * 1e6);
    return rTime;
}

/*static*/ std::string 
ESTime::currentStatusEngrString() { // Just the name of the enum, slightly more readable
    switch(currentStatus()) {
      case ESTimeSourceStatusOff:
        return "Off";
      case ESTimeSourceStatusUnsynchronized:
        return "Unsynchronized";
      case ESTimeSourceStatusPollingUnsynchronized:
        return "Polling, Unsynchronized";
      case ESTimeSourceStatusFailedUnynchronized:
        return "Failed, UnSynchronized";
      case ESTimeSourceStatusSynchronized:
        return "Synchronized";
      case ESTimeSourceStatusPollingSynchronized:
        return "Polling, Synchronized";
      case ESTimeSourceStatusFailedSynchronized:
        return "Failed, Synchronized";
      case ESTimeSourceStatusPollingRefining:
        return "Polling, Refining";
      case ESTimeSourceStatusRefining:
        return "Refining";
      case ESTimeSourceStatusFailedRefining:
        return "Failed, Refining";
      case ESTimeSourceStatusNoNetSynchronized:
        return "No Network, Synchronized";
      case ESTimeSourceStatusNoNetUnsynchronized:
        return "No Network, Unsynchronized";
      case ESTimeSourceStatusNoNetRefining:
        return "No Network, Refining";
      default:
        return "<bad value>";
    }
}

/*static*/ std::string 
ESTime::currentTimeSourceName() {  // "NTP", "XGPS150", "Fake", "Device", etc
    if (_bestDriver) {
        return _bestDriver->userName();
    }
    return "<no time source>";
}

/*static*/ bool 
ESTime::syncActive() {  // Are we looking for a sync
    switch(currentStatus()) {
      case ESTimeSourceStatusUnsynchronized:
      case ESTimeSourceStatusSynchronized:
      case ESTimeSourceStatusFailedUnynchronized:
      case ESTimeSourceStatusFailedSynchronized:
      case ESTimeSourceStatusNoNetSynchronized:
      case ESTimeSourceStatusNoNetUnsynchronized:
      case ESTimeSourceStatusOff:
        return false;
      case ESTimeSourceStatusPollingUnsynchronized:
      case ESTimeSourceStatusPollingSynchronized:
      case ESTimeSourceStatusPollingRefining:
      case ESTimeSourceStatusRefining:
      case ESTimeSourceStatusFailedRefining:
      case ESTimeSourceStatusNoNetRefining:
        return true;
    }
}

/*static*/ void 
ESTime::notifyContinuousOffsetChange() {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->notifyContinuousOffsetChange();
        }
    }
}

/*static*/ void
ESTime::resync(bool userRequested) {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->resync(userRequested);
        }
    }
}

/*static*/ void 
ESTime::stopSyncing() {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->stopSyncing();
        }
    }
}

/*static*/ void 
ESTime::disableSync() {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->disableSync();
        }
    }
}

/*static*/ void 
ESTime::enableSync() {
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->enableSync();
        }
    }
}

/*static*/ void 
ESTime::setDeviceCountryCode(const char *countryCode) {  // Called by libs/eslocation when the location of the device is known and can be translated to a country code
    if (_drivers) {
        std::list<ESTimeSourceDriver *>::iterator end = _drivers->end();
        std::list<ESTimeSourceDriver *>::iterator iter = _drivers->begin();
        while (iter != end) {
            ESTimeSourceDriver *driver = *iter++;
            driver->setDeviceCountryCode(countryCode);
        }
    }
}

ESTimeSyncObserver::ESTimeSyncObserver(ESThread *notificationThread)
:   _notificationThread(notificationThread)
{
}

static void
syncValueGlue(void *obj,
              void *param) {
    ESTimeSyncObserver *observer = (ESTimeSyncObserver *)obj;
    observer->syncValueChanged();
}

static void
syncStatusGlue(void *obj,
               void *param) {
    ESTimeSyncObserver *observer = (ESTimeSyncObserver *)obj;
    observer->syncStatusChanged();
}

static void
contTimeGlue(void *obj,
             void *param) {
    ESTimeSyncObserver *observer = (ESTimeSyncObserver *)obj;
    observer->continuousTimeReset();
}

void 
ESTimeSyncObserver::callSyncValueChanged() {
    if (_notificationThread && !_notificationThread->inThisThread()) {
        _notificationThread->callInThread(syncValueGlue, this, NULL);
    } else {
        syncValueChanged();
    }
}

void 
ESTimeSyncObserver::callSyncStatusChanged() {
    if (_notificationThread && !_notificationThread->inThisThread()) {
        _notificationThread->callInThread(syncStatusGlue, this, NULL);
    } else {
        syncStatusChanged();
    }
}

void 
ESTimeSyncObserver::callContinuousTimeReset() {
    if (_notificationThread && !_notificationThread->inThisThread()) {
        _notificationThread->callInThread(contTimeGlue, this, NULL);
    } else {
        continuousTimeReset();
    }
}

struct ESWorkingSyncMsg {
                            ESWorkingSyncMsg(ESTimeInterval val,
                                             ESTimeInterval acc)
    :   workingSyncValue(val),
        workingSyncAccuracy(acc)
    {}
    ESTimeInterval  workingSyncValue;
    ESTimeInterval  workingSyncAccuracy;
};

static void
workingSyncValueChangedGlue(void *obj,
                            void *param) {
    ESTimeSyncObserver *observer = (ESTimeSyncObserver *)obj;
    ESWorkingSyncMsg *msg = (ESWorkingSyncMsg *)param;
    observer->workingSyncValueChanged(msg->workingSyncValue,
                                      msg->workingSyncAccuracy);
    delete msg;
}

void
ESTimeSyncObserver::callWorkingSyncValueChanged(ESTimeInterval workingSyncValue,
                                                ESTimeInterval workingSyncAccuracy) {
    if (_notificationThread && !_notificationThread->inThisThread()) {
        ESWorkingSyncMsg *msg = new ESWorkingSyncMsg(workingSyncValue, workingSyncAccuracy);
        _notificationThread->callInThread(workingSyncValueChangedGlue, this, msg);
    } else {
        workingSyncValueChanged(workingSyncValue, workingSyncAccuracy);
    }
}

