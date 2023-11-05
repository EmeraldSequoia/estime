//
//  ESLeapSecond.cpp
//
//  Created by Steve Pucci 05 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTime.hpp"
#include "ESLeapSecond.hpp"
#include "assert.h"

// This table generated automatically from Wikipedia
// via the script ./getLeapSeconds.pl
// at Mon Nov  7 19:59:16 2016
/*static*/ struct ESLeapSecondEvent
ESLeapSecond::_leapSecondTable[] = {
  {    -899510400,   1,    1 },   // 1972 Jul 01 00:00:00
  {    -883612800,   1,    2 },   // 1973 Jan 01 00:00:00
  {    -852076800,   1,    3 },   // 1974 Jan 01 00:00:00
  {    -820540800,   1,    4 },   // 1975 Jan 01 00:00:00
  {    -789004800,   1,    5 },   // 1976 Jan 01 00:00:00
  {    -757382400,   1,    6 },   // 1977 Jan 01 00:00:00
  {    -725846400,   1,    7 },   // 1978 Jan 01 00:00:00
  {    -694310400,   1,    8 },   // 1979 Jan 01 00:00:00
  {    -662774400,   1,    9 },   // 1980 Jan 01 00:00:00
  {    -615513600,   1,   10 },   // 1981 Jul 01 00:00:00
  {    -583977600,   1,   11 },   // 1982 Jul 01 00:00:00
  {    -552441600,   1,   12 },   // 1983 Jul 01 00:00:00
  {    -489283200,   1,   13 },   // 1985 Jul 01 00:00:00
  {    -410313600,   1,   14 },   // 1988 Jan 01 00:00:00
  {    -347155200,   1,   15 },   // 1990 Jan 01 00:00:00
  {    -315619200,   1,   16 },   // 1991 Jan 01 00:00:00
  {    -268358400,   1,   17 },   // 1992 Jul 01 00:00:00
  {    -236822400,   1,   18 },   // 1993 Jul 01 00:00:00
  {    -205286400,   1,   19 },   // 1994 Jul 01 00:00:00
  {    -157852800,   1,   20 },   // 1996 Jan 01 00:00:00
  {    -110592000,   1,   21 },   // 1997 Jul 01 00:00:00
  {     -63158400,   1,   22 },   // 1999 Jan 01 00:00:00
  {     157766400,   1,   23 },   // 2006 Jan 01 00:00:00
  {     252460800,   1,   24 },   // 2009 Jan 01 00:00:00
  {     362793600,   1,   25 },   // 2012 Jul 01 00:00:00
  {     457401600,   1,   26 },   // 2015 Jul 01 00:00:00
  {     504921600,   1,   27 },   // 2017 Jan 01 00:00:00
};

/*static*/ ESLeapSecondEvent *
ESLeapSecond::_cacheLastEvent = ESLeapSecond::_leapSecondTable;

/*static*/ void
ESLeapSecond::findAndCacheEventPriorTo(ESTimeInterval utc) {
    // Sanity checks to make sure we copied the table correctly
    assert(_leapSecondTable[0].beginningOfChange == ESFirstLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].beginningOfChange == ESLastLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].cumulativeLeapSeconds == ESTotalLeapSeconds);
    // Ensure we don't try to call if we're before the first event or after the last -- catch outside this routine
    assert(utc > ESFirstLeapSecondTransition);
    assert(utc <= ESLastLeapSecondTransition);
    // Check on the cache...
    assert(_cacheLastEvent - _leapSecondTable < ESNumberOfLeapSecondEntries - 1 && _cacheLastEvent >= _leapSecondTable);  // Never point at the last entry
    // Binary search:  We know utc is between idx1 and idx2 transition events
    int idx1 = 0;
    int idx2 = ESNumberOfLeapSecondEntries - 1;
    while (idx2 - idx1 > 1) {
        int pivot = (idx1 + idx2) / 2;
        if (utc > _leapSecondTable[pivot].beginningOfChange) {
            idx1 = pivot;
        } else {
            idx2 = pivot;
        }
        //printf("Pivot %d => %d\n", idx1, idx2);
    }
    assert(idx2 == idx1 + 1);
    _cacheLastEvent = _leapSecondTable + idx1;
    assert(idx1 < ESNumberOfLeapSecondEntries - 1);
    assert(_cacheLastEvent - _leapSecondTable < ESNumberOfLeapSecondEntries - 1 && _cacheLastEvent >= _leapSecondTable);  // Never point at the last entry
    assert(utc > _cacheLastEvent->beginningOfChange && utc <= (_cacheLastEvent + 1)->beginningOfChange);
}

/*static*/ ESTimeInterval 
ESLeapSecond::cumulativeLeapSecondsForUTC(ESTimeInterval utc) {
    // Sanity checks to make sure we copied the table correctly
    assert(_leapSecondTable[0].beginningOfChange == ESFirstLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].beginningOfChange == ESLastLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].cumulativeLeapSeconds == ESTotalLeapSeconds);
    // Check on the cache...
    assert(_cacheLastEvent - _leapSecondTable < ESNumberOfLeapSecondEntries - 1 && _cacheLastEvent >= _leapSecondTable);  // Never point at the last entry
    if (utc > ESLastLeapSecondTransition) {  // By far the most common case
        return ESTotalLeapSeconds;   // Don't cache here -- the use of the cache looks ahead one, so it will crash if we supply the last item.  No need anyway because of this check
    } else if (utc <= ESFirstLeapSecondTransition) {  // Not so common but worth ruling out
        return 0;
    } else if (utc > _cacheLastEvent->beginningOfChange &&
               utc <= (_cacheLastEvent + 1)->beginningOfChange) {
        //printf("returning cache\n");
        return _cacheLastEvent->cumulativeLeapSeconds;
    } else {
        findAndCacheEventPriorTo(utc);
        return _cacheLastEvent->cumulativeLeapSeconds;
    }
}

/*static*/ ESTimeInterval 
ESLeapSecond::nextLeapSecondAfter(ESTimeInterval utc,
                                  ESTimeInterval *leapSecondDelta) {
    // Sanity checks to make sure we copied the table correctly
    assert(_leapSecondTable[0].beginningOfChange == ESFirstLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].beginningOfChange == ESLastLeapSecondTransition);
    assert(_leapSecondTable[ESNumberOfLeapSecondEntries - 1].cumulativeLeapSeconds == ESTotalLeapSeconds);
    // Check on the cache...
    assert(_cacheLastEvent - _leapSecondTable < ESNumberOfLeapSecondEntries - 1 && _cacheLastEvent >= _leapSecondTable);  // Never point at the last entry
    assert(leapSecondDelta);
    ESLeapSecondEvent *nextEvent;
    if (utc > ESLastLeapSecondTransition) {  // The most common case (?)
        *leapSecondDelta = 0;
        return ESFarFarInTheFuture;   // (meaning "never").  Don't cache here -- the use of the cache looks ahead one, so it will crash if we supply the last item.
    } else if (utc <= ESFirstLeapSecondTransition) {  // Not so common but worth ruling out because binary search is easier without this case
        *leapSecondDelta = _leapSecondTable[0].leapSeconds;
        return ESFirstLeapSecondTransition;
    } else if (utc > _cacheLastEvent->beginningOfChange &&
               utc <= (nextEvent = _cacheLastEvent + 1)->beginningOfChange) {
        //printf("returning cache\n");
        *leapSecondDelta = nextEvent->leapSeconds;
        return nextEvent->beginningOfChange;
    } else {
        findAndCacheEventPriorTo(utc);
        nextEvent = _cacheLastEvent + 1;
        *leapSecondDelta = nextEvent->leapSeconds;
        return nextEvent->beginningOfChange;
    }
}

// Convenience function
/*static*/ ESTimeInterval 
ESLeapSecond::intervalBetweenUTCValues(ESTimeInterval utc1,
                                       ESTimeInterval utc2) {
    // We can certainly do better than this:  Find the table entry for utc1 and move forward until you hit utc2, counting leap seconds encountered.
    // That saves doing two searches.  But... meh.  We have the cache anyway...
    return utc2 + cumulativeLeapSecondsForUTC(utc2) - (utc1 + cumulativeLeapSecondsForUTC(utc1));
}

/*static*/ ESTimeInterval 
ESLeapSecond::leapSecondsDuringInterval(ESTimeInterval utc1,
                                        ESTimeInterval utc2) {
    return cumulativeLeapSecondsForUTC(utc2) - cumulativeLeapSecondsForUTC(utc1);
}

/*static*/ ESTimeInterval
ESLeapSecond::intervalBetweenUTCValuesWithLiveLeapCorrections(ESTimeInterval utc1,
                                                              ESTimeInterval liveLeapSecondCorrection1,
                                                              ESTimeInterval utc2,
                                                              ESTimeInterval liveLeapSecondCorrection2) {
    ESTimeInterval delta = utc2 - utc1;
    delta += ESLeapSecond::leapSecondsDuringInterval(utc1, utc2);
    if (liveLeapSecondCorrection1 > 1) {
        delta -= (liveLeapSecondCorrection1 - 1);
    }
    if (liveLeapSecondCorrection2 > 1) {
        delta += (liveLeapSecondCorrection2 - 1);
    }
    return delta;
}


#ifdef ESLEAPSECOND_TEST
#include "ESCalendar.hpp"

void
ESTestLeapSecond() {
    ESDateComponents dc;
    dc.year = 1993;
    dc.month = 5;
    dc.day = 1;
    dc.hour = 13;
    dc.minute = 15;
    dc.seconds = 0;
    ESTimeInterval t = ESCalendar_timeIntervalFromUTCDateComponents(&dc);
    printf("The answer is %.1f\n", ESLeapSecond::cumulativeLeapSecondsForUTC(t));
    t = ESCalendar_timeIntervalFromUTCDateComponents(&dc);
    printf("The answer(2) is %.1f\n", ESLeapSecond::cumulativeLeapSecondsForUTC(t));
    dc.year = 2008;
    t = ESCalendar_timeIntervalFromUTCDateComponents(&dc);
    printf("The answer(3) is %.1f\n", ESLeapSecond::cumulativeLeapSecondsForUTC(t));
    dc.year = 2009;
    t = ESCalendar_timeIntervalFromUTCDateComponents(&dc);
    printf("The answer(4) is %.1f\n", ESLeapSecond::cumulativeLeapSecondsForUTC(t));
}
#endif // ESLEAPSECOND_TEST
