//
//  ESTime.hpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESTIME_HPP_
#define _ESTIME_HPP_

#include <string>

#include <list>

#define ES_ANDROID_USE_NTP 0

class ESTimeSourceDriver;
class ESThread;
class ESTimeZone;

typedef double ESTimeInterval;

enum ESTimeSourceStatus {
    ESTimeSourceStatusOff,                   // No synchronization is being done
    ESTimeSourceStatusUnsynchronized,        // We know nothing
    ESTimeSourceStatusSynchronized,          // We're good
    ESTimeSourceStatusPollingUnsynchronized, // We know nothing but we're trying
    ESTimeSourceStatusPollingRefining,       // We were kind of good (failedRefining) and the user has asked for a resync and we're trying
    ESTimeSourceStatusPollingSynchronized,   // We're good but the user has asked for a resync and we're trying
    ESTimeSourceStatusRefining,              // We've gotten some packets but they aren't good enough to say we're good
    ESTimeSourceStatusFailedSynchronized,    // We were synchronized before but we failed when the user asked for a resync
    ESTimeSourceStatusFailedUnynchronized,   // We were unsynchronized before and we failed the last time we tried
    ESTimeSourceStatusFailedRefining,        // We got some packets so we're kind of synchronized but not completely good
    ESTimeSourceStatusNoNetSynchronized,     // We were synchronized before but the net wasn't available when the user asked for a resync
    ESTimeSourceStatusNoNetUnsynchronized,   // We were unsynchronized before but the net wasn't available when the user asked for a resync
    ESTimeSourceStatusNoNetRefining,         // We had some packets before but the net wasn't available when we tried to refine
};

typedef enum ESTimeBaseKind {
    ESTimeBaseKindLT,
    ESTimeBaseKindUT,
    ESTimeBaseKindLST
} ESTimeBaseKind;

#define ESMinimumSupportedAstroDate     (-189344476800.0)  // Jan 1 4000 BC
#define ESGregorianStartDate		(- 13197600000.0)  // Oct 15 1582
#define ESMaximumSupportedAstroDate     (  25245561600.0)  // Jan 1 2801 AD
#define ESFarInTheFuture                (1E100)
#define ESFarFarInTheFuture             (1E150)
#define ESFarInThePast                  (-1E100)
#define ESFarFarInThePast               (-1E150)
#define ESLeapSecondHoldShortInterval   (.000001)  // During a positive leap second, we hold short of midnight by this much to keep the date from advancing

#define ESNTPMakerFlag        0x0001   // Use NTP
#define ESFakeMakerFlag       0x0002   // Make a fake time
#define ESSystemTimeMakerFlag 0x0004   // Use the uncorrected device time
#define ESXGPS150MakerFlag    0x0010   // Use the Dual XGPS150 GPS dongle if available
#define ESBestMakerFlags      (ESNTPMakerFlag | ESXGPS150MakerFlag)

/*! A pointer to a function (or static method) that creates an ESTimeSourceDriver* */
typedef ESTimeSourceDriver *(*ESTimeSourceDriverMaker)(void);

/*! Abstract class for observers of the time synchronization */
class ESTimeSyncObserver {
  public:
                            ESTimeSyncObserver(ESThread *notificationThread = NULL);  // NULL means deliver immediately, don't care what thread
    virtual                 ~ESTimeSyncObserver() {}
    virtual void            syncValueChanged() {}    // There was a (potential) jump in the absolute time
    virtual void            syncStatusChanged() {}   // There was a change in the time source status (useful for indicators)
    virtual void            continuousTimeReset() {} // There was a (potential) jump in the continuous time

    // The following is useful for geeky or debug/test apps
    virtual void            workingSyncValueChanged(ESTimeInterval workingSyncValue,
                                                    ESTimeInterval workingSyncAccuracy) {}  // The driver is working on a sync but hasn't reported anything yet

    ESThread                *notificationThread();

  protected:
    ESThread                *_notificationThread;

  private:
    void                    callSyncValueChanged();
    void                    callSyncStatusChanged();
    void                    callContinuousTimeReset();
    void                    callWorkingSyncValueChanged(ESTimeInterval workingSyncValue,
                                                        ESTimeInterval workingSyncAccuracy);

friend class ESTime;
};

class ESTime {
  public:
    static ESTimeInterval   currentTime();           // For clients who want to know what time it is, best we know how (aka NTP for ntp implementation)
    static ESTimeInterval   currentTimeWithLiveLeapSecondCorrection(ESTimeInterval *liveCorrection);  // like currentTime but also returns correction for seconds digit
    static ESTimeInterval   currentContinuousTime(); // For clients who just want a consistent time base from app startup (e.g., for timing intervals)
    static ESTimeSourceStatus currentStatus();
    static std::string      currentStatusEngrString(); // Just the name of the enum
    static std::string      currentTimeSourceName();  // "NTP", "XGPS150", "Fake", "Device", etc
    static bool             syncActive();  // Are we looking for a sync
    static ESTimeInterval   skewForReportingPurposesOnly(); // delta of NTP - SysTime   // Reporting purposes only because it doesn't account for live leap
    static ESTimeInterval   continuousOffset();      // delta of CTime - SysTime
    static ESTimeInterval   continuousSkewForReportingPurposesOnly();  // delta of NTP - CTime  // Reporting purposes only because it doesn't account for live leap
    static ESTimeInterval   skewAccuracy();     // What is our confidence in skew()
    static ESTimeInterval   ntpTimeForCTime(ESTimeInterval  cTime);
    static ESTimeInterval   ntpTimeForCTimeWithLiveCorrection(ESTimeInterval  cTime,
                                                              ESTimeInterval  *liveCorrection);
    static ESTimeInterval   cTimeForNTPTime(ESTimeInterval  ntpTime);
    static ESTimeInterval   sysTimeForCTime(ESTimeInterval cTime);
    static ESTimeInterval   cTimeForSysTime(ESTimeInterval sysTime);
    static ESTimeInterval   sysTimeForNTPTime(ESTimeInterval ntpTime);
    static float            currentTimeError();
    static void             noteTimeAtPhase(const char  *phaseName);
    static void             noteTimeAtPhase(const std::string phaseName) { noteTimeAtPhase(phaseName.c_str()); }
    static void             printTimes(const char     *phaseName,
                                       ESTimeInterval whenNTP     = -1,
                                       int            framesPerSecond = 0);
    static void             printContTimes(const char     *phaseName,
                                           ESTimeInterval whenCont,
                                           int            framesPerSecond = 0);
    static std::string      timeAsString(ESTimeInterval whenNTP         = -1,
                                         int            framesPerSecond = 0,
                                         ESTimeZone     *estz           = NULL,
                                         const char     *tzAbbrev       = NULL);
    //static void             noteTimeAtPhaseWithString(NSString  *phaseName);
    //static void             printTimes(NSString *who);
    static void             printADateWithTimeZone(ESTimeInterval dt,
                                                   ESTimeZone *estz,
                                                   int framesPerSecond = 0);
    static void             resync(bool userRequested);
    static void             stopSyncing();
    static void             disableSync();
    static void             enableSync();
    static void             setDeviceCountryCode(const char *countryCode);  // Called by libs/eslocation when the location of the device is known and can be translated to a country code

    /** This method can be used by add-on libraries and devices.  The given data is used if the skewAccuracy given
     *  is better than the current state. */
    static void             reportSkewFromExternalDevice(ESTimeInterval continuousSkew,
                                                         ESTimeInterval skewAccuracy);
    
// Debug
#ifndef NDEBUG
    static void             reportAllSkewsAndOffset(const char  *description);
#endif

// Public registration method to be informed when the sync changes
    static void             registerTimeSyncObserver(ESTimeSyncObserver *observer);
    static void             unregisterTimeSyncObserver(ESTimeSyncObserver *observer);

// Methods to be called by app delegate
    static void             startOfMain(const char *fourByteAppSig);
    static void             init(unsigned int makerFlags,
                                 ESTimeInterval fakeTimeForSync = 0,  // Ignored (but must be zero) unless using fake time source
                                 ESTimeInterval fakeTimeError = 0);   // Ignored (but must be zero) unless using fake time source
    static void             shutdown();


// Methods to be called by time source driver
    static void             syncValueChangedByDriver(ESTimeSourceDriver *driver);
    static void             syncStatusChangedByDriver(ESTimeSourceDriver *driver);
    static void             continuousTimeReset();
    static void             workingSyncValue(ESTimeInterval workingSyncValue,
                                             ESTimeInterval workingSyncAccuracy);
    static bool             recheckForBestDriver();  // Return true iff the driver is changed by this method

    static ESTimeInterval   gettimeofdayCont(struct timeval *tv);

// Useful for geeky apps
    static ESTimeInterval   adjustForLeapSecond(ESTimeInterval rawUTC);
    static ESTimeInterval   adjustForLeapSecondWithLiveCorrection(ESTimeInterval rawUTC,
                                                                  ESTimeInterval *liveCorrection);
    static ESTimeInterval   notificationTimeForDST(ESTimeInterval secondsSinceMidnight,
                                                   bool           beforeNotAfter);

  protected:

    static ESTimeInterval   _nextLeapSecondDate;
    static ESTimeInterval   _nextLeapSecondDelta;

  private:
    static void             notifyContinuousOffsetChange();

    static void             syncValueReallyChanged();
    static void             syncStatusReallyChanged();

    static void             setupNextLeapSecondData();
    static ESTimeInterval   adjustForLeapSecondGuts(ESTimeInterval rawUTC);
    static ESTimeInterval   adjustForLeapSecondGutsWithLiveCorrection(ESTimeInterval rawUTC,
                                                                      ESTimeInterval *liveCorrection);

    static ESTimeSourceDriver *_bestDriver;
    static std::list<ESTimeSourceDriver *> *_drivers;

friend class ESSystemTimeBase;
};

#define ESTIME_EPOCH (978307200.0)  // Conversion between ESTimeInterval and Unix time.  It's the Unix time at the ESTimeInterval epoch (1/1/2001 UTC)

#include "ESTimeSourceDriver.hpp"
#include "ESTimeInl.hpp"

#endif  // _ESTIME_HPP_
