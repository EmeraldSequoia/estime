//
//  ESCalendar.hpp
//  Emerald Sequoia LLC
//
//  Created by Steve Pucci 5/2010.
//  Copyright Emerald Sequoia LLC 2010. All rights reserved.
//

#ifndef ESCALENDAR_HPP
#define ESCALENDAR_HPP

#if ES_COCOA
#ifdef __OBJC__
#define ESCALENDAR_NS  // Special interface available only when using NS-TimeZone and NS-Calendar as the implementation
#endif
#endif

#include <string>

#include "ESTime.hpp"

// Opaque reference to time zone structure
struct ESTimeZone;

// Mimic (kind of) the NS-DateComponents object, but this one is a plain struct owned by the caller (and presumably almost always stack storage)
// Note: to find weekday, use the separate API for that
struct ESDateComponents {
    int era;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    double seconds;  // plural to remind that it's a double here
};

// Static initialization -- call this once before calling anything else
extern void
ESCalendar_init();

//********** Time Zone **********

// Allocate and init ("new") a time zone, return with refCount == 1
extern ESTimeZone *
ESCalendar_initTimeZoneFromOlsonID(const char *olsonID);

// Allocate and init ("new") a time zone with shared calendars suitable for use in a single thread only; only one at a time may exist; return with refCount == 1
extern ESTimeZone *
ESCalendar_initSingleTimeZoneFromOlsonID(const char *olsonID);

// Increment refCount
extern ESTimeZone *
ESCalendar_retainTimeZone(ESTimeZone *estz);

// Decrement refCount and free if refCount == 0
extern void
ESCalendar_releaseTimeZone(ESTimeZone *estz);
extern void
ESCalendar_releaseSingleTimeZone(ESTimeZone *estz);

// Number of seconds ahead of UTC at the given time in the given time zone
extern double
ESCalendar_tzOffsetForTimeInterval(ESTimeZone     *estz,
				   ESTimeInterval timeInterval);

extern ESTimeInterval
ESCalendar_nextDSTChangeAfterTimeInterval(ESTimeZone     *estz,
					  ESTimeInterval timeInterval);

extern ESTimeInterval
ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay(ESTimeZone     *estz,
                                                    ESTimeInterval timeInterval);

extern ESTimeInterval
ESCalendar_tzOffsetAfterNextDSTChange(ESTimeZone     *estz,
                                      ESTimeInterval timeInterval);

extern bool
ESCalendar_isDSTAtTimeInterval(ESTimeZone     *estz,
			       ESTimeInterval timeInterval);

extern std::string
ESCalendar_localTimeZoneName();

extern ESTimeZone *
ESCalendar_localTimeZone();

extern void
ESCalendar_localTimeZoneChanged();

extern void
ESCalendar_setLocalTimeZone(const char *olsonID);

#ifdef ESCALENDAR_NS
@class NSTimeZone;
extern NSTimeZone *
ESCalendar_nsTimeZone(ESTimeZone *estz);
#endif

//********** Conversions between ESTimeInterval and ESDateComponents **************

// Return calendar components from an ESTimeInterval (UTC)
extern void
ESCalendar_UTCDateComponentsFromTimeInterval(ESTimeInterval   timeInterval,
					     ESDateComponents *dateComponents);

// Return an ESTimeInterval from calendar components (UTC)
extern ESTimeInterval
ESCalendar_timeIntervalFromUTCDateComponents(ESDateComponents *dateComponents);

// Return calendar components in the given time zone from an ESTimeInterval
extern void
ESCalendar_localDateComponentsFromTimeInterval(ESTimeInterval   timeInterval,
					       ESTimeZone       *timeZone,
					       ESDateComponents *dateComponents);

// Return an ESTimeInterval from the calendar components in the given time zone
extern ESTimeInterval
ESCalendar_timeIntervalFromLocalDateComponents(ESTimeZone       *timeZone,
					       ESDateComponents *dateComponents);

// Return the weekday from an ESTimeInterval (UTC)
extern int
ESCalendar_UTCWeekdayFromTimeInterval(ESTimeInterval);

// Return the weekday in the given timezone from an ESTimeInterval
extern int
ESCalendar_localWeekdayFromTimeInterval(ESTimeInterval,
                                        ESTimeZone *);

//********** Adding ESDateComponents to ESTimeInterval **************

extern ESTimeInterval
ESCalendar_addDaysToTimeInterval(ESTimeInterval timeInterval,
				 ESTimeZone     *estz,
				 int            days);

extern ESTimeInterval
ESCalendar_addMonthsToTimeInterval(ESTimeInterval timeInterval,
				   ESTimeZone     *estz,
				   int            months);

extern ESTimeInterval
ESCalendar_addYearsToTimeInterval(ESTimeInterval timeInterval,
				  ESTimeZone     *estz,
				  int            years);

extern void
ESCalendar_localDateComponentsFromDeltaTimeInterval(ESTimeZone       *estz,
						    ESTimeInterval   time1,
						    ESTimeInterval   time2,
						    ESDateComponents *cs);

extern int
ESCalendar_daysInYear(ESTimeZone     *estz,
		      ESTimeInterval forTime);

//*********** Printing and formatting ******************

extern std::string
ESCalendar_dateIntervalDescription(ESTimeInterval dt);

extern std::string
ESCalendar_timeZoneName(ESTimeZone *estz);

extern const char *
ESCalendar_timeZoneLocalizedName(ESTimeZone *estz,
				 bool       duringDST);

extern std::string
ESCalendar_abbrevName(ESTimeZone *estz);

extern std::string
ESCalendar_formatInfoForTZ(ESTimeZone *estz,
			   int typ);

extern std::string
ESCalendar_formatTZOffset(float off);

extern std::string
ESCalendar_formatTime();

extern std::string
ESCalendar_version();

extern std::string
ESCalendar_formatTimeInterval(ESTimeInterval timeInterval,
			      ESTimeZone     *estz,
			      const char     *formatString);

//*********** Testing ************

#ifndef NDEBUG
extern void ESTestCalendar();
#endif

#endif  // ESCALENDAR_HPP
