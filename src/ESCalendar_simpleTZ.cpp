//
//  ESCalendar_simpleTZ.cpp
//
//  Created by Steve Pucci 14 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESCalendar.hpp"
#include "ESCalendarPvt.hpp"
#include "ESCalendar_simpleTZPvt.hpp"
#include "ESErrorReporter.hpp"

#include "assert.h"
#include <cstdlib>

struct ESTimeZone {
    ESTimeZoneImplementation *impl;
    int                      refCount;
};

static ESTimeZone *localTimeZone;
static bool assertOnRelease = false;

void ESCalendar_assertOnRelease() {
    assertOnRelease = true;
}

// Allocate and init ("new") a time zone
ESTimeZone *
ESCalendar_initTimeZoneFromOlsonID(const char *olsonID) {
    
    ESTimeZone *tz = (ESTimeZone *)malloc(sizeof(ESTimeZone));
    tz->impl = new ESTimeZoneImplementation(olsonID);
    tz->refCount = 1;
    // ESErrorReporter::logInfo("ESCalendar_initTimeZoneFromOlsonID", "%s => 0x%016llx", olsonID, (unsigned long long)tz);
    return tz;
}

// Clean up and Free the time zone
static void
ESCalendar_freeTimeZone(ESTimeZone *tz) {
    // ESErrorReporter::logInfo("ESCalendar_freeTimeZone", "0x%016llx => DELETE %s", (unsigned long long)tz, tz->impl->name().c_str());
    ESAssert(tz->refCount == 0);
    delete tz->impl;
    free(tz);
}

extern ESTimeZone *
ESCalendar_retainTimeZone(ESTimeZone *estz) {
    // ESErrorReporter::logInfo("ESCalendar_retainTimeZone", "0x%016llx => RETAIN, refCount %d", (unsigned long long)estz, estz->refCount);
    ESAssert(estz->refCount > 0);
    ++estz->refCount;
    return estz;
}

extern void
ESCalendar_releaseTimeZone(ESTimeZone *estz) {
    // ESErrorReporter::logInfo("ESCalendar_releaseTimeZone", "0x%016llx => RELEASE, refCount %d", (unsigned long long)estz, estz->refCount);
    if (assertOnRelease) {
        ESAssert(false);
    }
    ESAssert(estz->refCount > 0);
    if (estz) {
	if (--estz->refCount == 0) {
	    ESCalendar_freeTimeZone(estz);
	}
    }
}

extern std::string
ESCalendar_timeZoneName(ESTimeZone *estz) {
    return estz->impl->name();
}

void
ESCalendar_init() {
    localTimeZone = ESCalendar_initTimeZoneFromOlsonID(ESTimeZoneImplementation::olsonNameOfLocalTimeZone().c_str());
}

double
ESCalendar_tzOffsetForTimeInterval(ESTimeZone     *estz,
				   ESTimeInterval dt) {
    if (!estz) {
	return 0;  // Bogus but reproduces prior behavior
    }
    // ESErrorReporter::logInfo("ESCalendar_tzOffsetForTimeInterval", "0x%016llx", (unsigned long long)estz);
    return estz->impl->offsetFromUTCForTime(dt);
}

// Return calendar components in the given time zone from an ESTimeInterval
void
ESCalendar_localComponentsFromTimeInterval(ESTimeInterval timeInterval,
                                           ESTimeZone     *timeZone,
                                           int            *era,
                                           int            *year,
                                           int            *month,
                                           int            *day,
                                           int            *hour,
                                           int            *minute,
                                           double         *seconds) {
    ESCalendar_UTCComponentsFromTimeInterval(timeInterval + timeZone->impl->offsetFromUTCForTime(timeInterval),
                                             era, year, month, day, hour, minute, seconds);
}

ESTimeInterval
ESCalendar_timeIntervalFromLocalComponents(ESTimeZone *timeZone,
                                           int         era,
                                           int         year,
                                           int         month,
                                           int         day,
                                           int         hour,
                                           int         minute,
                                           double      seconds) {
    ESTimeInterval localT = ESCalendar_timeIntervalFromUTCComponents(era, year, month, day, hour, minute, seconds);
    ESTimeInterval offsetAtLocalT = timeZone->impl->offsetFromUTCForTime(localT);
    ESTimeInterval tryUTC = localT - offsetAtLocalT;
    ESTimeInterval offsetAtTryUTC = timeZone->impl->offsetFromUTCForTime(tryUTC);
    if (offsetAtTryUTC == offsetAtLocalT) {
        return tryUTC;
    }
    return localT - offsetAtTryUTC;  // Will return first of two possible answers during Fall DST
}

void
ESCalendar_localDateComponentsFromDeltaTimeInterval(ESTimeZone     *estz,
						    ESTimeInterval time1,
						    ESTimeInterval time2,
    						    ESDateComponents *cs) {
// The NSCalendar routines have a bug attempting to find the delta between two dates when one is BCE and one is CE.  Possibly other BCE-related bugs too.
    // So we do our own math.  First we find the number of years, then we pick two years with the proper leap/noleap relationship in CE territory, and let the
    // NSCalendar routine do those, then we add the two together.
    ESDateComponents cs1;
    ESCalendar_localDateComponentsFromTimeInterval(time1, estz, &cs1);
    ESDateComponents cs2;
    ESCalendar_localDateComponentsFromTimeInterval(time2, estz, &cs2);
    int year1 = cs1.era ? cs1.year : 1 - cs1.year;  // 1 BCE => 0, 2 BCE => -1
    int year2 = cs2.era ? cs2.year : 1 - cs2.year;  // 1 BCE => 0, 2 BCE => -1
    int deltaYear = year2 - year1;
    assert(deltaYear >= 0);
    bool year1IsLeap = ESCalendar_daysInYear(estz, time1) > 365.25;
    bool year2IsLeap = ESCalendar_daysInYear(estz, time2) > 365.25;

    cs->era = 0;
    // FIX: Put some code here...
    cs->year = 0;
    cs->month = 0;  // Cocoa doesn't set the month...
    cs->day = 0;
    cs->hour = 0;
    cs->minute = 0;
    cs->seconds = 0.0;  // Cocoa doesn't handle fractional seconds properly
}

ESTimeInterval
ESCalendar_nextDSTChangeAfterTimeInterval(ESTimeZone     *estz,
					  ESTimeInterval fromDateInterval) {
    return estz->impl->nextDSTChangeAfterTime(fromDateInterval, estz);
}

bool
ESCalendar_isDSTAtTimeInterval(ESTimeZone     *estz,
			       ESTimeInterval timeInterval) {
    return estz->impl->isDSTAtTime(timeInterval);
}

extern ESTimeZone *
ESCalendar_localTimeZone() {
    assert(localTimeZone);
    return localTimeZone;
}

extern void
ESCalendar_localTimeZoneChanged() {
    assert(localTimeZone);
    ESCalendar_releaseTimeZone(localTimeZone);
    std::string newTZName = ESTimeZoneImplementation::olsonNameOfLocalTimeZone();
    ESErrorReporter::logInfo("ESCalendar_localTimeZoneChanged", "setting localTimeZone to '%s'",
                             newTZName.c_str());
    localTimeZone = ESCalendar_initTimeZoneFromOlsonID(newTZName.c_str());
}

std::string
ESCalendar_localTimeZoneName() {
    return ESTimeZoneImplementation::olsonNameOfLocalTimeZone();
}

std::string
ESCalendar_formatTZOffset(float off) {
    if (off == 0) {
	return "";
    }
    return "??";
#if 0
    int hr = trunc(off);
    int mn = (off-hr)*60;
    if (mn == 0) {
	return [[NSString stringWithFormat:@"%+2d", hr] UTF8String];
    } else {
	return [[NSString stringWithFormat:NSLocalizedString(@"%+2d:%2d", @"format for hours:minutes in timezone offset"), hr, abs(mn)] UTF8String];
    }
#endif
}


std::string
ESCalendar_abbrevName(ESTimeZone *estz) {
    return estz->impl->abbrevName(estz);
}

