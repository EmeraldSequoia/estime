//
//  ESWatchTime.hpp
//  Created by Steve Pucci 29 May 2011.

//  Based on EC-WatchTime.h in Emerald Chronometer created by Steve Pucci 5/2008.
//  
//  Copyright Emerald Sequoia LLC 2008-2011. All rights reserved.
//

#ifndef _ESWATCHTIME_HPP_
#define _ESWATCHTIME_HPP_

#include "ESPlatform.h"
#include "ESTimeEnvironment.hpp"

typedef enum _ESWatchTimeCycle {
    ESWatchCycleReverseFF6,
    ESWatchCycleFirst = ESWatchCycleReverseFF6,
    ESWatchCycleReverseFF5,
    ESWatchCycleReverseFF4,
    ESWatchCycleReverseFF3,
    ESWatchCycleReverseFF2,
    ESWatchCycleReverseFF1,
    ESWatchCycleReverse,
    ESWatchCycleReverseSlow1,
    ESWatchCycleSlow1,
    ESWatchCycleNormal,
    ESWatchCycleFF1,
    ESWatchCycleFF2,
    ESWatchCycleFF3,
    ESWatchCycleFF4,
    ESWatchCycleFF5,
    ESWatchCycleFF6,
    ESWatchCycleLastForward = ESWatchCycleFF6,
    ESWatchCyclePaused,
    ESWatchCycleOther,
    ESWatchCycleLast = ESWatchCycleOther,
    ECNumWatchCycles,
} ESWatchTimeCycle;

class ESTimeEnvironment;
ES_OPAQUE_OBJC(NSDate);

// The primary keeper of time in all apps except Chronometer, which uses its own variant.
class ESWatchTime {
  public:
                            ESWatchTime();
                            ESWatchTime(ESTimeInterval frozenTimeInterval);
    virtual                 ~ESWatchTime();

    double                  warp() { return _warp; }

    double                  ourTimeAtIPhoneZeroIgnoringLeapSeconds();
    double                  ourTimeAtCorrectedZero() { return _ourTimeAtCorrectedZero; }
    bool                    frozen();
    bool                    isCorrect();
    bool                    lastMotionWasInReverse();

// Convention:  names which end with "Number" (e.g., dayNumber) are integer values,
//                 with specific definitions described below
//              names which end with "Value" (e.g., dayValue), are continuous double
//                 values, which are typically equal to the Number value immediately after
//                 the Number value changes.  That is, the Value value is always >= the Number value.

    bool                    checkAndConstrainAbsoluteTime();

// When updating, we can presume all uses of watchTime can legitimately use the same currentTime,
// so latch it when starting the update for a watch and unlatch it when done
    ESTimeInterval          latchTimeForBeatsPerSecond(int beatsPerSecond);
    ESTimeInterval          latchTimeOnMinuteBoundary();  // Useful for ambient mode in Android
    void                    unlatchTime();

// *****************
// Methods useful for watch hands and moving dials:
// *****************

// 12:35:45.9 => 45
    int                     secondNumberUsingEnv(ESTimeEnvironment *env);

// 12:35:45.9 => 45.9
    double                  secondValueUsingEnv(ESTimeEnvironment *env);

// 12:35:45 => 35
    int                     minuteNumberUsingEnv(ESTimeEnvironment *env);

// 12:35:45 => 35.75
    double                  minuteValueUsingEnv(ESTimeEnvironment *env);

// 13:45:00 => 1
    int                     hour12NumberUsingEnv(ESTimeEnvironment *env);

// 13:45:00 => 1.75
    double                  hour12ValueUsingEnv(ESTimeEnvironment *env);

// 13:45:00 => 13
    int                     hour24NumberUsingEnv(ESTimeEnvironment *env);

// 13:45:00 => 13.75
    double                  hour24ValueUsingEnv(ESTimeEnvironment *env);

// March 1 => 0  (n.b, not 1; useful for angles and for arrays of images, and consistent with double form below)
    int                     dayNumberUsingEnv(ESTimeEnvironment *env);

// March 1 at 6pm  =>  0.75;  useful for continuous hands displaying day
    double                  dayValueUsingEnv(ESTimeEnvironment *env);

// March 1 => 2  (n.b., not 3)
    int                     monthNumberUsingEnv(ESTimeEnvironment *env);

// March 1 at noon  =>  12 / (31 * 24);  useful for continuous hands displaying month
    double                  monthValueUsingEnv(ESTimeEnvironment *env);

// March 1 1999 => 1999
    int                     yearNumberUsingEnv(ESTimeEnvironment *env);

// BCE => 0; CE => 1
    int                     eraNumberUsingEnv(ESTimeEnvironment *env);

// Sunday => 0
    int                     weekdayNumberUsingEnv(ESTimeEnvironment *env);

// Tuesday at 6pm => 2.75
    double                  weekdayValueUsingEnv(ESTimeEnvironment *env);

// This function incorporates the value of the weekdayStart passed in
    int                     weekdayNumberAsCalendarColumnUsingEnv(ESTimeEnvironment *env, int weekdayStart);

// Leap year: fraction of 366 days since Jan 1
// Non-leap year: fraction of 366 days since Jan 1 through Feb 28, then that plus 24hrs starting Mar 1
// Result is indicator value on 366-year dial
    double                  year366IndicatorFractionUsingEnv(ESTimeEnvironment *env);

// Jan 1 => 0    
    int                     dayOfYearNumberUsingEnv(ESTimeEnvironment *env);

// First week => 0    
    int                     weekOfYearNumberUsingEnv(int               weekStartDay,  // weekStartDay == 0 means weeks start on Sunday
                                                     bool              useISO8601,    // use only when weekStartDay == 1 (Monday)
                                                     ESTimeEnvironment *env);

// daylight => 1; standard => 0
    bool                    isDSTUsingEnv(ESTimeEnvironment *env);

// leapYear
    bool                    leapYearUsingEnv(ESTimeEnvironment *env);

// The date for the next DST change in this watch
    ESTimeInterval          nextDSTChangeUsingEnv(ESTimeEnvironment *env);

// The date for the prev DST change in this watch
    ESTimeInterval          prevDSTChangePrecisely(bool              precise,
                                                   ESTimeEnvironment *env);

    int                     secondsSinceMidnightNumberUsingEnv(ESTimeEnvironment *env);
    double                  secondsSinceMidnightValueUsingEnv(ESTimeEnvironment *env);

// *****************
// Stopwatch methods:  The y for a stopwatch watch is zero at reset
// *****************

// Set value to 0
    void                    stopwatchReset();

// Initialize and set value according to recorded defaults
    void                    stopwatchInitStoppedReading(double interval);
    void                    stopwatchInitRunningFromZeroTime(double zeroTime);

// Copy lap time
    void                    copyLapTimeFromOtherTimer(ESWatchTime *otherTimer);

// Clone time
    void                    makeTimeIdenticalToOtherTimer(ESWatchTime *otherTimer);

// Check for clone
    bool                    isIdenticalTo(ESWatchTime *otherTimer);

// 00:04:03.9 => 3
    int                     stopwatchSecondNumber();

// 00:04:03.9 => 3.9
    double                  stopwatchSecondValue();

// 01:04:45 => 4
    int                     stopwatchMinuteNumber();

// 01:04:45 => 4.75
    double                  stopwatchMinuteValue();

// 5d 01:30:00 => 1
    int                     stopwatchHour12Number();

// 5d 01:30:00 => 1.5
    double                  stopwatchHour12Value();

// 5d 01:30:00 => 1
    int                     stopwatchHour24Number();

// 5d 01:30:00 => 1.5
    double                  stopwatchHour24Value();

// 5d 18:00:00 => 5
    int                     stopwatchDayNumber();

// 5d 18:00:00 => 5.75
    double                  stopwatchDayValue();

// *****************
// Alarm methods
// *****************

// Clone time + interval
    void                    makeTimeIdenticalToOtherTimer(ESWatchTime    *otherTimer,
                                                          ESTimeInterval delta);

    double                  offsetFromOtherTimer(ESWatchTime *otherTimer);

// *****************
// Get methods for internal use
// *****************

// return the current (warped) time
    double                  currentTime();

#ifdef ES_COCOA
  // Don't use this unless you have to!  It's not portable!
    NSDate                  *currentDate();
#endif

// return the current time ignoring any latch (useful for things like finding the next DST transition in the midst of a time motion)
    double                  currentTimeIgnoringLatch();

// Return the iPhone time corresponding to the given watch time
    double                  convertFromWatchToSystem(double t);

// Return the watch time corresponding to the given iPhone time
    double                  convertFromSystemToWatch(double t);

    ESTimeInterval          contTimeForWatchTime(ESTimeInterval watchTime);
    ESTimeInterval          contTimeForWatchTime();
    ESTimeInterval          watchTimeForContTime(ESTimeInterval contTime);

// warp == 0
    bool                    isStopped();

// warp < 0
    bool                    runningBackward();

// LT - GMT in seconds
    ESTimeInterval          tzOffsetUsingEnv(ESTimeEnvironment *env);

// 1d 4:00:00 (localized (not yet))
    std::string             representationOfDeltaOffsetUsingEnv(ESTimeEnvironment *env);

// 1 day/s
    std::string             representationOfWarp();

// Number of days between two times (if this is Wed and other's Thu, answer is -1)
    int                     numberOfDaysOffsetFrom(ESWatchTime       *other,
                                                   ESTimeEnvironment *env);;
    int                     numberOfDaysOffsetFrom(ESWatchTime       *other,
                                                   ESTimeEnvironment *env1,
                                                   ESTimeEnvironment *env2);
// *****************
// Set methods
// *****************

// "Smooth time" clients don't care about absolute time, just about having a smooth time reference from app startup
    void                    setUseSmoothTime(bool useSmoothTime);

// Set the time to the given one, but freeze it there
    void                    setToFrozenDateInterval(ESTimeInterval date);

// If we're moving at a high rate, it's less distracting if the time hands don't move.
// So we ensure that the time is latched such that the time hands are the same as when
// showing fastWarpReferenceTime.  This is used in latchTimeForBeatsPerSecond().
    void                    setFastWarpReferenceTime(ESTimeInterval fastWarpReferenceTime);

// save/restore current state to userDefaults
    void                    saveStateForWatch(const std::string &nam);
    void                    restoreStateForWatch(const std::string &nam);

// Freeze at current time; does nothing if already frozen
    void                    stop();

// Unfreeze; does nothing if not frozen
    void                    start();

// Freeze or unfreeze
    void                    toggleStop();

// Reverse direction
    void                    reverse();

// Like toggleStop, but if stopping then round value to nearest rounding value if stopping
// (useful for stopwatches, so the restart time is exactly where it purports to be)
    void                    toggleStopWithRounding(double rounding);

// Reset to following actual iPhone time (subject to calendar installed)
    void                    resetToLocal();

// Change warp speed, recalculating internal data as necessary
    void                    setWarp(ESTimeInterval newWarp);

// If pause, then resume from current position at normal speed (warp == 1); if not paused, then pause
    void                    pausePlay();

// Cycle through speeds
    void                    cycleFastForward();

// Cycle through speeds
    void                    cycleRewind();

// Advance to next point (context sensitive)
    void                    advanceToNextUsingEnv(ESTimeEnvironment *env);

// Advance to previous point (context sensitive)
    void                    advanceToPreviousUsingEnv(ESTimeEnvironment *env);

// Specific advance next/prev methods
    void                    advanceToNextInterval(ESTimeInterval    interval,
                                                  ESTimeEnvironment *env);
    void                    advanceToPrevInterval(ESTimeInterval    interval,
                                                  ESTimeEnvironment *env);
    void                    advanceToNextMonthUsingEnv(ESTimeEnvironment *env);
    void                    advanceToPrevMonthUsingEnv(ESTimeEnvironment *env);
    void                    advanceToNextYearUsingEnv(ESTimeEnvironment *env);
    void                    advanceToPrevYearUsingEnv(ESTimeEnvironment *env);

// Advance by exactly this amount
    void                    advanceBySeconds(double         numSeconds,
                                             ESTimeInterval startTime);
    void                    advanceBySeconds(double numSeconds);
    void                    advanceByDays(int               numDays,
                                          ESTimeInterval    startTime,
                                          ESTimeEnvironment *env); // works on DST days
    void                    advanceByDays(int               numDays,
                                          ESTimeEnvironment *env); // works on DST days
    void                    advanceOneDayUsingEnv(ESTimeEnvironment *env);
    void                    advanceByMonths(int               numMonths,
                                            ESTimeInterval    startTime,
                                            ESTimeEnvironment *env);  // keeping day the same, unless there's no such day, in which case go to last day of month
    void                    advanceByMonths(int               numMonths,
                                            ESTimeEnvironment *env);  // keeping day the same, unless there's no such day, in which case go to last day of month
    void                    advanceOneMonthUsingEnv(ESTimeEnvironment *env);
    void                    advanceByYears(int               numYears,
                                           ESTimeInterval    startTime,
                                           ESTimeEnvironment *env);   // keeping month and day the same, unless it was Feb 29, in which case move back to Feb 28
    void                    advanceByYears(int               numYears,
                                           ESTimeEnvironment *env);   // keeping month and day the same, unless it was Feb 29, in which case move back to Feb 28
    void                    advanceOneYearUsingEnv(ESTimeEnvironment *env);
    void                    retardOneDayUsingEnv(ESTimeEnvironment *env); // works on DST days
    void                    retardOneMonthUsingEnv(ESTimeEnvironment *env);
    void                    retardOneYearUsingEnv(ESTimeEnvironment *env);
    void                    advanceToQuarterHourUsingEnv(ESTimeEnvironment *env);
    void                    retreatToQuarterHourUsingEnv(ESTimeEnvironment *env);

// *****************
// Debug methods
// *****************
// Note: The following routine adds one to the returned month and day, for readability
    std::string             dumpAllUsingEnv(ESTimeEnvironment *env);

  private:
    void                    commonInit();

    double                   midnightForDateInterval(double            dateInterval,
                                                     ESTimeEnvironment *env)
    { return env->midnightForTimeInterval(dateInterval); }

    ESTimeInterval          gapJumpBeforeInterval();
    ESTimeInterval          offsetAfterJump();
    ESTimeInterval          preAdvanceForNextAtTime(ESTimeInterval currentLocal,
                                                    ESTimeInterval interval);
    ESTimeInterval          preAdvanceForPrevAtTime(ESTimeInterval currentLocal,
                                                    ESTimeInterval interval);

    // In a plot of our time (y) vs actual time (x), our time is y = mx + b

    // The factor by which we are running faster (or slower) than actual time (m)
    double                  _warp;

    // What our time would read when the actual corrected time (as we know it) was zero, extrapolated backwards (b)
    double                  _ourTimeAtCorrectedZero;

    // Internal use only:
    ESTimeInterval          _fastWarpReferenceTime;
    double                  _warpBeforeFreeze;
    ESWatchTimeCycle        _cycle;
    bool                    _useSmoothTime;
    int                     _latched;
    ESTimeInterval          _latchCorrectedTime;

};

#endif  // _ESWATCHTIME_HPP_
