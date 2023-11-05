//
//  ESLeapSecond.hpp
//
//  Created by Steve Pucci 05 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESLEAPSECOND_HPP_
#define _ESLEAPSECOND_HPP_

/*! Internal data structure and constants */
struct ESLeapSecondEvent {
    ESTimeInterval          beginningOfChange;
    ESTimeInterval          leapSeconds;
    ESTimeInterval          cumulativeLeapSeconds;
};
#define ESNumberOfLeapSecondEntries 27
#define ESTotalLeapSeconds 27
#define ESFirstLeapSecondTransition -899510400
#define ESLastLeapSecondTransition 504921600

/*! Leap-second data needed for interval timing */
class ESLeapSecond {
  public:
    // The official data
    static ESTimeInterval   cumulativeLeapSecondsForUTC(ESTimeInterval utc);

    // Convenience functions
    static ESTimeInterval   intervalBetweenUTCValues(ESTimeInterval utc1,
                                                     ESTimeInterval utc2);
    static ESTimeInterval   leapSecondsDuringInterval(ESTimeInterval utc1,
                                                      ESTimeInterval utc2);
    static ESTimeInterval   nextLeapSecondAfter(ESTimeInterval utc,
                                                ESTimeInterval *leapSecondDelta);

    static ESTimeInterval   intervalBetweenUTCValuesWithLiveLeapCorrections(ESTimeInterval utc1,
                                                                            ESTimeInterval liveLeapSecondCorrection1,
                                                                            ESTimeInterval utc2,
                                                                            ESTimeInterval liveLeapSecondCorrection2);

  private:
    // shh ... this should be private but then we couldn't use it as the static initialization of _cacheLastEvent (or can we...)
    static ESLeapSecondEvent _leapSecondTable[ESNumberOfLeapSecondEntries];

    static void             findAndCacheEventPriorTo(ESTimeInterval utc);

    // Caching for nextLeapSecondAfter
    static ESTimeInterval   _lastCheckNextLeapSecond;
    static ESTimeInterval   _lastAnswerNextLeapSecond;

    // General caching
    static ESLeapSecondEvent *_cacheLastEvent;
};

#undef ESLEAPSECOND_TEST
#ifdef ESLEAPSECOND_TEST
extern void ESTestLeapSecond();
#endif

#endif  // _ESLEAPSECOND_HPP_
