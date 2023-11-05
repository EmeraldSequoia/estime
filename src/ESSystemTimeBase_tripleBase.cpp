//
//  ESSystemTimeBase_tripleBase.cpp
//
//  Created by Steve Pucci 28 Jun 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

// Code common to platforms (e.g., iOS and android) which have three underlying time bases.
// Note that this has nothing to do with NTP, just the device itself (though it may in fact
// use NTP for one or all of the three bases).
//
// The REALTIME time is the time typically displayed to the user, as in a clock app.  It is kept
// up to date by the OS via cell towers, GPS, or NTP, and may jump forwards and backwards.

// The MONOTONIC time is the time used for animating.  It is always monotonically increasing,
// and should not jump forward or backward.  However, it *might* stop altogether if the system
// is "suspended".

// The BOOTTIME time (so called because the corresponding constant in time.h is CLOCK_BOOTTIME)
// is like MONOTONIC except "it also includes any time that the system is suspended."

// The hope is that we can use BOOTTIME as the base for NTP skewing, in one of two ways:
// 1.  If the OS guarantees that BOOTTIME deltas across a suspension of the OS are of the same
//     accuracy as BOOTTIME or MONOTONIC are when the OS is active, then we can rely on this
//     being a stable time base indefinitely.
// 2.  If the OS doesn't guarantee that, then at least we can rely on knowing when an actual
//     suspension has occurred (by tracking the delta between BOOTTIME and MONOTONIC and noting
//     when that changes) in order to know when to call out to NTP.
// Note that in either case, we are always using BOOTTIME as the basis for continuous time, unlike
// with the dualBase solution, where we switched back and forth between system time and "alt" time
// upon waking and sleeping.
// We *do* observe waking, however, as this is our opportunity to note if a suspension has occurred
// (and hence that continuous time might have "moved" with respect to NTP, so an NTP trigger should
// be done).

#include "ESTime.hpp"
#include "ESSystemTimeBase.hpp"
#if ES_TRIPLEBASE

#include "ESUtil.hpp"
#include "ESErrorReporter.hpp"
#include "ESTimeCalibrator.hpp"
#include "ESUserPrefs.hpp"

#include <time.h>

#undef ESTRACE
#include "ESTrace.hpp"

// Class static variables
/*static*/ bool            ESSystemTimeBase::_usingAltTime;
/*static*/ ESTimeInterval  ESSystemTimeBase::_altOffset;              //  (systemTime - systemAltTime)
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousOffset;       //  (continuousTime - systemTime)
/*static*/ ESTimeInterval  ESSystemTimeBase::_continuousAltOffset;    //  (continuousTime - systemAltTime)
/*static*/ bool            ESSystemTimeBase::_continuousTimeStableAtStart = false;
/*static*/ bool            ESSystemTimeBase::_isSingleTimeBase = false;

// NOTE Steve 2017/06/30: interval timers can't rely on continuous time across a process start.  I'm not sure
// if we're handling this properly, even in iOS, though I certainly remember thinking a lot about it.  Here
// we're better off in that case, but in the case of a reboot we have the same problem.  I think the way it works
// is we just "back date" the interval start after a reboot, since we've recorded the NTP-correct time of the start.

// How long does it take to boot.  We use this to determine whether a change in boot time encountered by the
// initialization code (vs what is stored in prefs) is the result of an actual reboot or is the result of a
// system time skew.  It should be long enough that we don't accidentally treat a skew as a reboot (and thus
// force a sync) but short enough that an *actual* reboot hasn't occurred.  Note that the former is a soft error,
// easily recovered, but the latter is a hard error, since the display will be wrong by the difference in boot
// times  until the next "naturally occurring" sync.  So we err on the side of making this fairly short.
// Note also that this is *boot* time, not restart time (the shutdown time might not be present if the device crashed).
// My LG Watch Sport and Huawei both take about 80 seconds, so 30 seconds seems a safe threshold for now.
static const ESTimeInterval ES_MAX_BOOT_TIME = 30.0;

static double
estimeFromSeconds(double t) {
    return t - ESTIME_EPOCH;
}

static double 
secondsFromTimeSpec(const struct timespec &ts) {
    return ts.tv_sec + ts.tv_nsec / 1E9;
}

static double 
estimeFromTimeSpec(const struct timespec &ts) {
    return estimeFromSeconds(secondsFromTimeSpec(ts));
}

/*static*/ void
ESSystemTimeBase::getTimes(ESTimeInterval *realtimeTime, ESTimeInterval *monotonicSeconds, ESTimeInterval *boottimeSeconds, ESTimeInterval *monotonicRawSeconds) {
    struct timespec realtimeTs;
    struct timespec boottimeTs;
    struct timespec monotonicTs;
    struct timespec monotonicRawTs;

    // Get all times at once, to minimize any error in calculating the offsets (so we can detect non-noise
    // changes more easily).
    int realtimeSt = clock_gettime(CLOCK_REALTIME, &realtimeTs);
    int boottimeSt = clock_gettime(CLOCK_BOOTTIME, &boottimeTs);
    int monotonicSt = clock_gettime(CLOCK_MONOTONIC, &monotonicTs);
    int monotonicRawSt = clock_gettime(CLOCK_MONOTONIC_RAW, &monotonicRawTs);

    ESErrorReporter::checkAndLogSystemError("ESSystemTimeBase", realtimeSt, "Error getting CLOCK_REALTIME");
    ESErrorReporter::checkAndLogSystemError("ESSystemTimeBase", boottimeSt, "Error getting CLOCK_BOOTTIME");
    ESErrorReporter::checkAndLogSystemError("ESSystemTimeBase", monotonicSt, "Error getting CLOCK_MONOTONIC");
    ESErrorReporter::checkAndLogSystemError("ESSystemTimeBase", monotonicRawSt, "Error getting CLOCK_MONOTONIC_RAW");

    *realtimeTime = estimeFromTimeSpec(realtimeTs);
    *monotonicSeconds = secondsFromTimeSpec(monotonicTs);
    *boottimeSeconds = secondsFromTimeSpec(boottimeTs);
    *monotonicRawSeconds = secondsFromTimeSpec(monotonicRawTs);
}

/*static*/ void
ESSystemTimeBase::wakingUp() {
    ESTimeInterval realtimeTime;
    ESTimeInterval boottimeSeconds;
    ESTimeInterval monotonicSeconds;
    ESTimeInterval monotonicRawSeconds;

    getTimes(&realtimeTime, &monotonicSeconds, &boottimeSeconds, &monotonicRawSeconds);

    // Here we're not sure what we know, except that BOOTTIME is *close* to being unchanged, according to the OS.
    // Since BOOTTIME is our reference, we just recalculate other things in terms of it.

    // In particular, continuousAltOffset shouldn't change: We want continuousTime to be consistent, and altTime
    // (aka boottimeTime) is the best new value we have (in particular, the system time, which we rely on in the
    // dualbase solution, is less likely to be correct than the boottime).

    _altOffset = realtimeTime - estimeFromSeconds(boottimeSeconds);
    _continuousOffset = _continuousAltOffset - _altOffset;

    ESErrorReporter::logInfo("TripleBase::wakingUp", "altOffset %.1f, continuousOffset %.1f, continuousAltOffset %.1f",
                             _altOffset, _continuousOffset, _continuousAltOffset);

    ESErrorReporter::logOffline(ESUtil::stringWithFormat(
                                    "Wake: altOffset %.4f, continuousOffset %.4f, continuousAltOffset %.4f, bootSeconds %.4f, monotonicSeconds %.4f, monotonicRawSeconds %.4f",
                                    _altOffset, _continuousOffset, _continuousAltOffset, boottimeSeconds, monotonicSeconds, monotonicRawSeconds));

    ESTime::notifyContinuousOffsetChange();

    // FOR NOW, we go ahead and get a sync here, so we can verify our various theories.  We *don't* advertise that continuous time has been
    // reset (as we do in the dualbase case), because it hasn't.  And when we resync, we specify "userRequested" == true (a misnamed parameter
    // that means we can still use the old value), so our status watch doesn't go to infinite accuracy when we start resyncing, and so we do
    // the right thing when a very-low-accuracy sync comes in (specifically, we ignore it if the prior sync accuracy is ok).
    ESTime::resync(true /* userRequested */);
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
    // Check the boot time recorded in prefs.  If it's ok, we can use the continuous time skew we find in prefs as well.
    // If it's not ok, then we can start with the system-based skew, but we need to do a sync.  But we can't do a sync now,
    // because we're called before the driver is set up.

    std::string prefsPrefix = ESUtil::deviceID();
    std::string bootTimePrefsName =   prefsPrefix + "-bootTime";
    std::string contAltOffsetPrefsName = prefsPrefix + "-contAltOff";
        
    ESTimeInterval storedTimeOfBoot = ESUserPrefs::doublePref(bootTimePrefsName);

    ESTimeInterval realtimeTime;
    ESTimeInterval boottimeSeconds;
    ESTimeInterval monotonicSeconds;
    ESTimeInterval monotonicRawSeconds;

    getTimes(&realtimeTime, &monotonicSeconds, &boottimeSeconds, &monotonicRawSeconds);

    _usingAltTime = true;   // Always true for triplebase
    _altOffset = realtimeTime - estimeFromSeconds(boottimeSeconds);

    ESTimeInterval newTimeOfBoot = realtimeTime - boottimeSeconds;

    if (fabs(newTimeOfBoot - storedTimeOfBoot) < ES_MAX_BOOT_TIME) {
        // The difference in boot times is less than the time it takes to boot, so we presume the difference
        // is because of a system time slew since we were last active.  We can go ahead and re-use the old
        // offset values.
        _continuousTimeStableAtStart = true;
        _continuousAltOffset = ESUserPrefs::doublePref(contAltOffsetPrefsName);
        _continuousOffset = _continuousAltOffset - _altOffset;
    } else {
        _continuousTimeStableAtStart = false;
        _continuousOffset = 0;  // Start out continuous time at system time
        _continuousAltOffset = _altOffset + _continuousOffset;
        // Conveniently, once set we never change either value, so this is the only prefs storing we need to do.
        ESUserPrefs::setPref(contAltOffsetPrefsName, _continuousAltOffset);
        ESUserPrefs::setPref(bootTimePrefsName, newTimeOfBoot);
    }
    ESTimeCalibrator::init(!_continuousTimeStableAtStart /* rebootDetected */);

    ESErrorReporter::logInfo("ESSystemTimeBase::init", "%s altOffset %.1f, continuousOffset %.1f, continuousAltOffset %.1f",
                             _continuousTimeStableAtStart ? "no reboot" : "REBOOT", _altOffset, _continuousOffset, _continuousAltOffset);

    ESErrorReporter::logOffline(ESUtil::stringWithFormat(
                                    "Init %s: altOffset %.4f, continuousOffset %.4f, continuousAltOffset %.4f, bootSeconds %.4f, monotonicSeconds %.4f, monotonicRawSeconds %.4f",
                                    _continuousTimeStableAtStart ? "no reboot" : "REBOOT", 
                                    _altOffset, _continuousOffset, _continuousAltOffset, boottimeSeconds, monotonicSeconds, monotonicRawSeconds));

    // Set up notifications for sleep/wake and sig time change
    ESUtil::registerSleepWakeObserver(new SleepWakeObserver);
    ESUtil::registerSignificantTimeChangeObserver(new TimeChangeObserver);

    return realtimeTime;;
}

/*static*/ void
ESSystemTimeBase::goingToSleep() {
    // Do nothing
}

/*static*/ void
ESSystemTimeBase::systemBaseChange() {  // Getting a new cell-tower (or os-NTP/GPS) time; system time changes but alt doesn't
    ESTimeInterval realtimeTime;
    ESTimeInterval boottimeSeconds;
    ESTimeInterval monotonicSeconds;
    ESTimeInterval monotonicRawSeconds;

    getTimes(&realtimeTime, &monotonicSeconds, &boottimeSeconds, &monotonicRawSeconds);

    // No change in continuous time base.  Just recalculate system offsets.
    _altOffset = realtimeTime - estimeFromSeconds(boottimeSeconds);
    _continuousOffset = _continuousAltOffset - _altOffset;

    ESErrorReporter::logInfo("ESSystemTimeBase::systemBaseChange", "altOffset %.1f, continuousOffset %.1f, continuousAltOffset %.1f",
                             _altOffset, _continuousOffset, _continuousAltOffset);
    ESErrorReporter::logOffline(ESUtil::stringWithFormat(
                                    "Tower: altOffset %.4f, continuousOffset %.4f, continuousAltOffset %.4f, bootSeconds %.4f, monotonicSeconds %.4f, monotonicRawSeconds %.4f",
                                    _altOffset, _continuousOffset, _continuousAltOffset, boottimeSeconds, monotonicSeconds, monotonicRawSeconds));

    ESTime::notifyContinuousOffsetChange();
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
    struct timespec ts;
    int st = clock_gettime(CLOCK_BOOTTIME, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1E9;
    //ESErr::logInfo("ESSystemTimeBase", "The monotonic time is %.9f\n", t);
    return t - ESTIME_EPOCH;
}

/*static*/ ESTimeInterval
ESSystemTimeBase::currentContinuousSystemTime() {
    return currentSystemAltTime() + _continuousAltOffset;
}

/*static*/ void 
ESSystemTimeBase::getTimesSinceBoot(ESTimeInterval *bootTimeAsSystemTime, ESTimeInterval *suspendedTimeSinceBoot) {
    ESTimeInterval realtimeTime;
    ESTimeInterval boottimeSeconds;
    ESTimeInterval monotonicSeconds;
    ESTimeInterval monotonicRawSeconds;

    getTimes(&realtimeTime, &monotonicSeconds, &boottimeSeconds, &monotonicRawSeconds);

    *bootTimeAsSystemTime = realtimeTime - boottimeSeconds;
    *suspendedTimeSinceBoot = boottimeSeconds - monotonicSeconds;
}

#endif  // ES_TRIPLEBASE
