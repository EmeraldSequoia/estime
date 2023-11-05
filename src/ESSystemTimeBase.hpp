//
//  ESSystemTimeBase.hpp
//
//  Created by Steve Pucci 18 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESSYSTEMTIMEBASE_HPP_
#define _ESSYSTEMTIMEBASE_HPP_

#include "ESPlatform.h"

#if ES_ANDROID && ES_ANDROID_USE_NTP
#define ES_TRIPLEBASE 1
#else
#define ES_TRIPLEBASE 0
#endif

/*! "Abstract" class representing the time base provided by the system. Not actually a C++ abstract class because
 *  any given system we compile for only has one implementation. */
class ESSystemTimeBase {
  public:
    static ESTimeInterval   init();  // Returns currentSystemTime() at start
    static ESTimeInterval   currentSystemTime();   // Like [NSDate timeIntervalSinceReferenceDate]
    static ESTimeInterval   currentContinuousSystemTime();  // Like EC's rDate, based on media time while awake and NSDate while asleep;
                                                            //    moves as continuously as we know how without an external time reference.
                                                            // On some systems may return the same thing as currentSystemTime().
                                                            // This is the basis to which NTP adjustments are applied.
    static ESTimeInterval   continuousOffset() { return _continuousOffset; }  // (continuousTime - systemTime) aka EC dateROffset

    // Methods called by private observer classes
    static void             systemBaseChange();
    static void             goingToSleep();
    static void             wakingUp();

    // The returned parameters are chosen carefully so they shouldn't change except when suspended.
    // Not implemented on all platforms.
    static void             getTimesSinceBoot(ESTimeInterval *bootTimeAsSystemTime, ESTimeInterval *suspendedTimeSinceBoot);

    // Whether it is safe to use stored deltas from continuous time.
    static bool             continuousTimeStableAtStart() { return _continuousTimeStableAtStart; }

    static bool             isSingleTimeBase() { return _isSingleTimeBase; }

#if ES_TRIPLEBASE
    static void             getTimes(ESTimeInterval *realtimeTime, ESTimeInterval *monotonicSeconds, ESTimeInterval *boottimeSeconds, ESTimeInterval *monotonicRawSeconds);
#endif

  private:
    // Not implemnted on all platforms
    static ESTimeInterval   currentSystemAltTime();  // CACurrentMediaTime()

    // Not implemnted on all platforms
    static ESTimeInterval   initCommonDualBase();

    static bool             _usingAltTime;
    static ESTimeInterval   _altOffset;              //  (systemTime - systemAltTime) aka EC mediaOffset
    static ESTimeInterval   _continuousOffset;       //  (continuousTime - systemTime) aka EC dateROffset
    static ESTimeInterval   _continuousAltOffset;    //  (continuousTime - systemAltTime) aka EC mediaROffset
    static bool             _continuousTimeStableAtStart;
    static bool             _isSingleTimeBase;
};

#define ESTIME_EPOCH (978307200.0)  // Amount by which ESTime epoch is after Unix time epoch
#define ESTIME_FROM_UNIX_TIME(unixTime) (unixTime/1000.0 - ESTIME_EPOCH)
#define UNIX_TIME_FROM_ESTIME(estime) llrint((estime + ESTIME_EPOCH) * 1000)

#endif  // _ESSYSTEMTIMEBASE_HPP_
