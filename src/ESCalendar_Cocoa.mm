//
//  ESCalendar_Cocoa.mm
//
//  Created by Steve Pucci 26 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESPlatform.h"
#include "ESCalendar.hpp"
#include "ESCalendarPvt.hpp"

#include "ESTime.hpp"  // for +[TSTime currentTime] only

#ifndef NDEBUG  // Testing only
//#define ESCALENDAR_TEST
#undef ESCALENDAR_TEST
#endif

#ifdef ESCALENDAR_TEST
#include "ECGeoNames.h"
#include <pthread.h>
#endif

// Opaque definitions
#ifdef __OBJC__
@class NSTimeZone;
@class NSCalendar;
#else
class NSTimeZone;  // Wrong but doesn't matter -- it's an opaque pointer in either case
class NSCalendar;
#endif

struct ESTimeZone {
    NSTimeZone *nsTimeZone;
    NSCalendar *nsCalendar;
    NSCalendar *utcCalendar;
    int refCount;
};

// Allocate and init ("new") a time zone
ESTimeZone *
ESCalendar_initTimeZoneFromOlsonID(const char *olsonID) {
    // NSArray *allTimeZones = [NSTimeZone knownTimeZoneNames];
    // for (NSString *tzx in allTimeZones) {
    //     printf("%s\n", [tzx UTF8String]);
    // }
    ESTimeZone *tz = (ESTimeZone *)malloc(sizeof(ESTimeZone));
    tz->nsTimeZone = [[NSTimeZone alloc] initWithName:[NSString stringWithCString:olsonID encoding:NSUTF8StringEncoding]];
    assert(tz->nsTimeZone);
    tz->nsCalendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    assert(tz->nsCalendar);
    [tz->nsCalendar setTimeZone:tz->nsTimeZone];
    tz->utcCalendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    assert(tz->utcCalendar);
    [tz->utcCalendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
    tz->refCount = 1;
    return tz;
}

// Clean up and Free the time zone
static void
ESCalendar_freeTimeZone(ESTimeZone *tz) {
    assert(tz->refCount == 0);
    [tz->nsTimeZone release];
    tz->nsTimeZone = nil;
    [tz->nsCalendar release];
    tz->nsCalendar = nil;
    [tz->utcCalendar release];
    tz->utcCalendar = nil;
    free(tz);
}

extern ESTimeZone *
ESCalendar_retainTimeZone(ESTimeZone *estz) {
    ++estz->refCount;
    return estz;
}

extern void
ESCalendar_releaseTimeZone(ESTimeZone *estz) {
    if (estz) {
	if (--estz->refCount == 0) {
	    ESCalendar_freeTimeZone(estz);
	}
    }
}


extern std::string
ESCalendar_timeZoneName(ESTimeZone *estz) {
    return [[estz->nsTimeZone name] UTF8String];
}

const char *
ESCalendar_timeZoneLocalizedName(ESTimeZone *estz,
				 bool       duringDST) {
    return [[estz->nsTimeZone localizedName:(duringDST ? NSTimeZoneNameStyleDaylightSaving : NSTimeZoneNameStyleStandard) locale:[NSLocale autoupdatingCurrentLocale]] UTF8String];
}

std::string
ESCalendar_abbrevName(ESTimeZone *estz) {
    return [[estz->nsTimeZone abbreviation] UTF8String];
}

#ifdef ESCALENDAR_NS
extern NSTimeZone *
ESCalendar_nsTimeZone(ESTimeZone *estz) {
    return estz->nsTimeZone;
}
#endif

static bool ESCalendar_nscalendarIsPureGregorian = false;
static bool ESCalendar_initialized = false;

static NSDateFormatter *dateFormatter = nil;
static NSDateFormatter *timeFormatter = nil;

static ESTimeZone *localTimeZone;

void
ESCalendar_init() {
    NSTimeInterval testTimeInterval = kECJulianGregorianSwitchoverTimeInterval - 10;
    NSCalendar *utcCalendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    [utcCalendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
    NSDateComponents *dc = [utcCalendar components:(NSCalendarUnitDay)
                                          fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:testTimeInterval]];
    [utcCalendar release];
    if (dc.day == 14) {
	ESCalendar_nscalendarIsPureGregorian = true;
    } else {
	assert(dc.day == 4);
	ESCalendar_nscalendarIsPureGregorian = false;
    }

    dateFormatter = [[NSDateFormatter alloc] init];
    timeFormatter = [[NSDateFormatter alloc] init];
    [dateFormatter setDateStyle:NSDateFormatterMediumStyle];
    [timeFormatter setDateStyle:NSDateFormatterNoStyle];
    [timeFormatter setDateFormat:@"h:mm:ss a"];

    assert(!localTimeZone);
    localTimeZone = ESCalendar_initTimeZoneFromOlsonID([[[NSTimeZone localTimeZone] name] UTF8String]);

    ESCalendar_initialized = true;
#ifndef NDEBUG
    //printf("Calendar is%s pure Gregorian\n", ESCalendar_nscalendarIsPureGregorian ? "" : " NOT");
#ifdef ESCALENDAR_TEST
    testHybridConversion();
#endif
#endif
}

extern void printADateWithTimeZone(NSTimeInterval dt, ESTimeZone *estz);

// Number of seconds ahead of UTC at the given time in the given time zone
double
ESCalendar_tzOffsetForTimeInterval(ESTimeZone     *estz,
				   NSTimeInterval dt) {
    if (!estz) {
	return 0;  // Bogus but reproduces prior behavior
    }
    NSCalendar *ltCalendar = estz->nsCalendar;
    NSCalendar *utcCalendar = estz->utcCalendar;

    // This should really be [[calendar timeZone] secondsFromGMTForDate:[self currentDate]]
    // but NSCalendar and NSTimeZone developers don't seem to talk to each other

    NSDate *date = [NSDate dateWithTimeIntervalSinceReferenceDate:dt];
    double fractionalSeconds = dt - floor(dt);

    // Note:  Even if ltCalendar and utcCalendar are pure Gregorian, the time zone is not going to be different before 1583 on different days, since no DST was present then.

    // The LT representation of the given date.  We need to do it this way because GMT is unambiguous going back from CS to date, but LT isn't
    NSDateComponents *ltCS = [ltCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
					   fromDate:date];
    //printf("ltCalendar reports %.0f as %d-%04d-%02d-%02d %02d:%02d:%02d\n", dt, ltCS.era, ltCS.year, ltCS.month, ltCS.day, ltCS.hour, ltCS.minute, ltCS.second);
    NSTimeInterval utcForDate = [[utcCalendar dateFromComponents:ltCS] timeIntervalSinceReferenceDate];
    //printf("...which, when interpreted as a UTC date, returns the time %.0f\n", utcForDate);
    // Assume PST, -8h.  UTC for a given *calendar date* will be 8 hours *behind* the corresponding local time for the same calendar date (because UTC gets to a given calendar date first).
    // So, counter-intuitively, we must return utc - lt rather than lt - utc here:
    double offset = utcForDate + fractionalSeconds - dt;
#undef TEST_NSCALENDAR_TZOFFSET_BUG
#ifdef TEST_NSCALENDAR_TZOFFSET_BUG
    double nsOffset = [[ltCalendar timeZone] secondsFromGMTForDate:date];
    if (fabs(offset - nsOffset) > .00001) {
	printf("%s %04d-%02d-%02d %02d:%02d:%02d lt: calculated offset %3.1f hours, ns offset %3.1f hours\n",
	       ltCS.era ? " CE" : "BCE",
	       ltCS.year, ltCS.month, ltCS.day, ltCS.hour, ltCS.minute, ltCS.second,
	       offset/3600, nsOffset/3600);
    }
#endif
    return offset;
}

// Return calendar components in the given time zone from an NSTimeInterval
void
ESCalendar_localComponentsFromTimeInterval(NSTimeInterval timeInterval,
                                           ESTimeZone     *timeZone,
                                           int            *era,
                                           int            *year,
                                           int            *month,
                                           int            *day,
                                           int            *hour,
                                           int            *minute,
                                           double         *seconds) {
    assert(ESCalendar_initialized);  // Call ESCalendar_init() before calling this function
    double fractionalSeconds = timeInterval - floor(timeInterval);
    //printf("localComponentsFromTimeInterval starting with %.2f\n", timeInterval);
    NSDateComponents *ltCS = [timeZone->nsCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
						     fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:timeInterval]];
    *era = (int)ltCS.era;
    *year = (int)ltCS.year;
    *month = (int)ltCS.month;
    *day = (int)ltCS.day;
    if (ESCalendar_nscalendarIsPureGregorian) {
	//printf("gregorianToHybrid translating %d-%04d-%02d-%02d to ",
	//       *era, *year, *month, *day);
	ESCalendar_gregorianToHybrid(era, year, month, day);
	//printf("%d-%04d-%02d-%02d\n",
	//       *era, *year, *month, *day);
    }
    *hour = (int)ltCS.hour;
    *minute = (int)ltCS.minute;
    *seconds = (int)ltCS.second + fractionalSeconds;
}

// Return an NSTimeInterval from the calendar components in the given time zone
NSTimeInterval
ESCalendar_timeIntervalFromLocalComponents(ESTimeZone *timeZone,
                                           int         era,
                                           int         year,
                                           int         month,
                                           int         day,
                                           int         hour,
                                           int         minute,
                                           double      seconds) {
    if (ESCalendar_nscalendarIsPureGregorian) {
	//printf("hybridToGregorian translating %d-%04d-%02d-%02d to ",
	//       era, year, month, day);
	ESCalendar_hybridToGregorian(&era, &year, &month, &day);
	//printf("%d-%04d-%02d-%02d\n",
	//       era, year, month, day);
    }
    NSDateComponents *dc = [[NSDateComponents alloc] init];
    dc.era = era;
    dc.year = year;
    dc.month = month;
    dc.day = day;
    dc.hour = hour;
    dc.minute = minute;
    int secondsI = floor(seconds);
    double secondsF = seconds - secondsI;
    dc.second = secondsI;
    NSTimeInterval timeInterval = [[timeZone->nsCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + secondsF;
    [dc release];
    //printf("timeIntervalFromLocalComponents returning %.2f\n", timeInterval);
    return timeInterval;
}

void
ESCalendar_localDateComponentsFromDeltaTimeInterval(ESTimeZone     *estz,
						    NSTimeInterval time1,
						    NSTimeInterval time2,
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

    NSDateComponents *nscs1 = [[NSDateComponents alloc] init];
    nscs1.era = 1;
    nscs1.month = cs1.month;
    nscs1.day = cs1.day;
    nscs1.hour = cs1.hour;
    nscs1.minute = cs1.minute;
    nscs1.second = floor(cs1.seconds);
    NSDateComponents *nscs2 = [[NSDateComponents alloc] init];
    nscs2.era = 1;
    nscs2.month = cs2.month;
    nscs2.day = cs2.day;
    nscs2.hour = cs2.hour;
    nscs2.minute = cs2.minute;
    nscs2.second = floor(cs2.seconds);

    // Pick two years always 4 years apart
    if (year1IsLeap) {  // 1900
	if (year2IsLeap) {
	    nscs1.year = 2004;  // leap
	    nscs2.year = 2008;  // leap
	} else {
	    nscs1.year = 1896;  // leap
	    nscs2.year = 1900;  // not leap
	}
    } else {
	if (year2IsLeap) {
	    nscs1.year = 1900;  // not leap
	    nscs2.year = 1904;  // leap
	} else {
	    nscs1.year = 2005;  // not leap
	    nscs2.year = 2009;  // not leap
	}
    }

    NSDateComponents *nscs
	= [estz->nsCalendar components:(NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond | NSCalendarUnitDay | NSCalendarUnitYear | NSCalendarUnitEra)
			      fromDate:[estz->nsCalendar dateFromComponents:nscs1]
				toDate:[estz->nsCalendar dateFromComponents:nscs2]
			       options:0];
    [nscs1 release];
    [nscs2 release];
    cs->era = 0;
    cs->year = (int)nscs.year + deltaYear - 4;
//    cs->month = nscs.month;
    cs->day = (int)nscs.day;
    cs->hour = (int)nscs.hour;
    cs->minute = (int)nscs.minute;
    cs->seconds = (int)nscs.second;
}

NSTimeInterval
ESCalendar_nextDSTChangeAfterTimeInterval(ESTimeZone     *estz,
					  NSTimeInterval fromDateInterval) {
    NSDate *fromDate = [NSDate dateWithTimeIntervalSinceReferenceDate:fromDateInterval];
    NSDate *transitionDate = [estz->nsTimeZone nextDaylightSavingTimeTransitionAfterDate:fromDate];
    NSTimeInterval transitionDateInterval = [transitionDate timeIntervalSinceReferenceDate];
    if (transitionDateInterval == 0) {
	return 0;
    }
    if (transitionDateInterval <= fromDateInterval) {
	if (transitionDateInterval <= fromDateInterval - 1) {
	    //printf("bad date %.1f from nextDST of %.1f (delta %.1f): %s\n", transitionDateInterval, fromDateInterval, transitionDateInterval - fromDateInterval, [[transitionDate description] UTF8String]);
	    return 0;
	}
	transitionDate = [estz->nsTimeZone nextDaylightSavingTimeTransitionAfterDate:[NSDate dateWithTimeIntervalSinceReferenceDate:(fromDateInterval + 1)]];
	transitionDateInterval = [transitionDate timeIntervalSinceReferenceDate];
	if (transitionDateInterval == 0) {
	    return 0;
	}
	assert(transitionDateInterval > fromDateInterval);
    }
    return transitionDateInterval;
}

bool
ESCalendar_isDSTAtTimeInterval(ESTimeZone     *estz,
			       NSTimeInterval timeInterval) {
    return [estz->nsTimeZone isDaylightSavingTimeForDate:[NSDate dateWithTimeIntervalSinceReferenceDate:timeInterval]] ? true : false;
}

extern ESTimeZone *
ESCalendar_localTimeZone() {
    assert(ESCalendar_initialized);
    assert(localTimeZone);
    return localTimeZone;
}

extern void
ESCalendar_localTimeZoneChanged() {
    assert(localTimeZone);
    [NSTimeZone resetSystemTimeZone];	// resets the cached version of it
    ESCalendar_releaseTimeZone(localTimeZone);
    localTimeZone = ESCalendar_initTimeZoneFromOlsonID([[[NSTimeZone localTimeZone] name] UTF8String]);
    [dateFormatter setTimeZone:localTimeZone->nsTimeZone];
    [timeFormatter setTimeZone:localTimeZone->nsTimeZone];
}

extern void
ESCalendar_setLocalTimeZone(const char *olsonID) {
    assert(localTimeZone);
    ESCalendar_releaseTimeZone(localTimeZone);
    localTimeZone = ESCalendar_initTimeZoneFromOlsonID(olsonID);
    [dateFormatter setTimeZone:localTimeZone->nsTimeZone];
    [timeFormatter setTimeZone:localTimeZone->nsTimeZone];
}

std::string
ESCalendar_localTimeZoneName() {
    return [[localTimeZone->nsTimeZone name] UTF8String];
}

std::string
ESCalendar_formatTZOffset(float off) {
    if (off == 0) {
	return "";
    }
    int hr = trunc(off);
    int mn = (off-hr)*60;
    if (mn == 0) {
	return [[NSString stringWithFormat:@"%+2d", hr] UTF8String];
    } else {
	return [[NSString stringWithFormat:NSLocalizedString(@"%+2d:%2d", @"format for hours:minutes in timezone offset"), hr, abs(mn)] UTF8String];
    }
}

std::string
ESCalendar_formatInfoForTZ(ESTimeZone *estz,
			   int typ) {                     	    // typ == 1: "CHAST = UTC+12:45"  or  "CHADT = UTC+13:45 (Daylight)"
    NSTimeZone *tz = estz->nsTimeZone;
    if ([[tz name] compare:@"UTC"] == NSOrderedSame) {		    // typ == 2: "CHAST = UTC+12:45 : CHADT = UTC+13:45"
	return "UTC";						    // typ == 3: "CHAST = UTC+12:45; (CHADT = UTC+13:45 on Nov 22, 2009)"
    }								    // typ == 4: "-10: -9" or "-10:-10"
    [dateFormatter setTimeZone:tz];				    // typ == 5: "CHAST"
    [timeFormatter setTimeZone:tz];
    NSTimeInterval nowTime = ESTime::currentTime();
    NSDate *now = [NSDate dateWithTimeIntervalSinceReferenceDate:nowTime];
    NSString *abbrevNow = [tz abbreviationForDate:now];
    double offsetNow = ESCalendar_tzOffsetForTimeInterval(estz, nowTime)/3600.0;
    NSTimeInterval nextDSTTransition = ESCalendar_nextDSTChangeAfterTimeInterval(estz, nowTime);
    NSString *abbrevThen = nextDSTTransition ? [tz abbreviationForDate:[NSDate dateWithTimeIntervalSinceReferenceDate:(nextDSTTransition + 1)]] : abbrevNow;
    double offsetThen = nextDSTTransition ? ESCalendar_tzOffsetForTimeInterval(estz,nextDSTTransition+1)/3600.0 : offsetNow;
    if (typ == 4) {
	return [[NSString stringWithFormat:@"%+3d:%+3d", (int)trunc(fmin(offsetNow, offsetThen)), (int)trunc(fmax(offsetNow, offsetThen))] UTF8String];
    }
    NSString *transitionDate = nil;
    if (!nextDSTTransition) {
	if ([abbrevNow caseInsensitiveCompare:abbrevThen] == NSOrderedSame) {
	    if (typ != 5) {
		typ = 1;	    // no DST
	    }
	} else {
	    transitionDate = @"?";	// permanent DST?
	}
    } else {
	transitionDate = [dateFormatter stringFromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:nextDSTTransition]];
    }
    NSString *ret = nil;
    if (NSEqualRanges([abbrevNow rangeOfString:@"GMT"], NSMakeRange(0,3))) {
	switch (typ) {
	    case 1:
		if ([tz isDaylightSavingTime]) {
		    ret = [NSString stringWithFormat:NSLocalizedString(@"%@ (Daylight)", @"short format for timezone description in DST with no tz TLA"), abbrevNow];
		} else {
		    ret = abbrevNow;
		}
		break;
	    case 2:
		ret = [NSString stringWithFormat:NSLocalizedString(@"%@ : %@", @"format for partial timezone description with no tz TLA"), abbrevNow, abbrevThen];
		break;
	    case 3:
		ret = [NSString stringWithFormat:NSLocalizedString(@"%@; %@ on %@", @"format for full timezone description with no tz TLA"), abbrevNow, abbrevThen, transitionDate];
		break;
	    case 5:
		ret = abbrevNow;
		break;
	    default:
		assert(false);
	}
    } else {
	switch (typ) {
	    case 1:
		if ([tz isDaylightSavingTime]) {
		    ret = [NSString stringWithFormat:NSLocalizedString(@"%@ = UTC%s (Daylight)", @"short format for timezone description in DST"), abbrevNow, ESCalendar_formatTZOffset(offsetNow).c_str()];
		} else {
		    ret = [NSString stringWithFormat:NSLocalizedString(@"%@ = UTC%s", @"short format for timezone description in standard time"), abbrevNow, ESCalendar_formatTZOffset(offsetNow).c_str()];
		}
		break;
	    case 2:
              ret = [NSString stringWithFormat:NSLocalizedString(@"%@ = UTC%s : %@ = UTC%s", @"format for partial timezone description"), abbrevNow, ESCalendar_formatTZOffset(offsetNow).c_str(), abbrevThen, ESCalendar_formatTZOffset(offsetThen).c_str()];
		break;
	    case 3:
              ret = [NSString stringWithFormat:NSLocalizedString(@"%@ = UTC%s; %@ = UTC%s on %@", @"format for full timezone description"), abbrevNow, ESCalendar_formatTZOffset(offsetNow).c_str(), abbrevThen, ESCalendar_formatTZOffset(offsetThen).c_str(), transitionDate];
		break;
	    case 5:
		ret = abbrevNow;
		break;
	    default:
		assert(false);
	}
    }
    return [ret UTF8String];
}

std::string
ESCalendar_version() {
#if __IPHONE_4_0
    if ([NSTimeZone respondsToSelector:@selector(timeZoneDataVersion)]) {
#if __IPHONE_4_0
	return [[NSTimeZone timeZoneDataVersion] UTF8String];
#else
	assert(false);
	return "??";
#endif
    } else {
	return "3x";
    }
#else
    return "3x";
#endif
}

std::string
ESCalendar_formatTime() {
    NSDate *now = [NSDate dateWithTimeIntervalSinceReferenceDate:ESTime::currentTime()];
    return [[NSString stringWithFormat:NSLocalizedString(@"%@ %@", @"date time"),[dateFormatter stringFromDate:now], [timeFormatter stringFromDate:now]] UTF8String];
}

std::string
ESCalendar_formatTimeInterval(NSTimeInterval timeInterval,
			      ESTimeZone     *estz,
			      const char     *formatString) {
    NSDateFormatter *dateFormatterHere = [[[NSDateFormatter alloc] init] autorelease];
    [dateFormatterHere setDateFormat:[NSString stringWithCString:formatString encoding:NSUTF8StringEncoding]];
    assert(estz);
    [dateFormatterHere setTimeZone:estz->nsTimeZone];
    return [[dateFormatterHere stringFromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:timeInterval]] UTF8String];
}

std::string
ESCalendar_dateIntervalDescription(NSTimeInterval dt) {
    ESDateComponents ltcs;
    ESCalendar_localDateComponentsFromTimeInterval(dt, localTimeZone, &ltcs);
    int second = floor(ltcs.seconds);
    int microseconds = round((ltcs.seconds - second) * 1000000);  // ugliness one half of a microsecond before a second boundary
    return [[NSString stringWithFormat:@"%s %04d/%02d/%02d %02d:%02d:%02d.%06d LT",
		   ltcs.era ? " CE" : "BCE", ltcs.year, ltcs.month, ltcs.day, ltcs.hour, ltcs.minute, second, microseconds] UTF8String];
}


#ifdef ESCALENDAR_TEST
NSLock *printLock = nil;

static void printOne(NSTimeInterval timeInterval,
		     ESTimeZone     *estz,
		     bool           doLocal) {
    double secondsF = timeInterval - floor(timeInterval);
    NSDateComponents *dc = [estz->utcCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
						fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:timeInterval]];
    int eraESCal;
    int yearESCal;
    int monthESCal;
    int dayESCal;
    int hourESCal;
    int minuteESCal;
    double secondsESCal;
    ESCalendar_UTCComponentsFromTimeInterval(timeInterval, &eraESCal, &yearESCal, &monthESCal, &dayESCal, &hourESCal, &minuteESCal, &secondsESCal);

    // UTC time first (we know this works, so don't bother with NSDate)
    printf("%15.2f %3s %04d/%02d/%02d %02d:%02d:%05.2f ",
	   timeInterval, eraESCal ? " CE" : "BCE", yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);

    if (doLocal) {
	// Now ESCalendar's idea of the local time
	ESCalendar_localComponentsFromTimeInterval(timeInterval, estz, &eraESCal, &yearESCal, &monthESCal, &dayESCal, &hourESCal, &minuteESCal, &secondsESCal);
	printf("%5.3f (%7.1f) %3s %04d/%02d/%02d %02d:%02d:%05.2f ",
	       ESCalendar_tzOffsetForTimeInterval(estz, timeInterval) / 3600, ESCalendar_tzOffsetForTimeInterval(estz, timeInterval), eraESCal ? " CE" : "BCE", yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);

	// And NSDate's idea of the local time
	dc = [estz->nsCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
			   fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:timeInterval]];
    }
    
    printf("%3s %04d/%02d/%02d %02d:%02d:%05.2f\n",
	   dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);

}

static void printSpread(NSTimeInterval timeInterval,
			ESTimeZone     *estz,
			bool           doLocal) {
    if (doLocal) {
	printf("%15s %26s %5s %26s %26s\n",
	       "time interval", "UTC time", "offs", "ESCalendar local", "NSDate local");
    } else {
	printf("%15s %26s %26s\n",
	       "time interval", "ESCalendar UTC time", "NSDate UTC time");
    }
    printOne(timeInterval - 2.0, estz, doLocal);
    printOne(timeInterval - 1.5, estz, doLocal);
    printOne(timeInterval - 1.0, estz, doLocal);
    printOne(timeInterval - 0.5, estz, doLocal);
    printOne(timeInterval - 0.0, estz, doLocal);
    printOne(timeInterval + 0.5, estz, doLocal);
    printOne(timeInterval + 1.0, estz, doLocal);
    printOne(timeInterval + 1.5, estz, doLocal);
    printOne(timeInterval + 2.0, estz, doLocal);
}

static void
pushUpComponents(int *era1,
		 int *year1,
		 int *month1,
		 int *day1,
		 int *hour1,
		 int *minute1,
		 double *seconds1) {
    if (*seconds1 > 59.999) {
	*seconds1 -= 60;
	(*minute1)++;
    }
    if (*minute1 > 59) {
	*minute1 -= 60;
	(*hour1)++;
    }
    if (*hour1 > 23) {
	*hour1 -= 24;
	(*day1)++;
    }
    // Let's assume, for now, that we don't care about day number overflow...
}

static bool
componentsAreEquivalent(int era1,
			int year1,
			int month1,
			int day1,
			int hour1,
			int minute1,
			double seconds1,
			int era2,
			int year2,
			int month2,
			int day2,
			int hour2,
			int minute2,
			double seconds2) {
    pushUpComponents(&era1, &year1, &month1, &day1, &hour1, &minute1, &seconds1);
    pushUpComponents(&era2, &year2, &month2, &day2, &hour2, &minute2, &seconds2);
    return (era2 == era1 &&
	    year2 == year1 &&
	    month2 == month1 &&
	    day2 == day1 &&
	    hour2 == hour1 &&
	    minute2 == minute1 &&
	    fabs(seconds2 - seconds1) < 0.0001);
}

// First, compare the components returned by NSCalendar (set to UTC) and ESCalendar_UTCComponentsFromTimeInterval to ensure they are the same for this time interval
// Then, using those components (specifically, the ones from ESCalendar), go back using ESCalendar_timeIntervalFromUTCComponents and ensure we get the same time interval we started from
// Next, do the two steps above using local time instead of UTC
static bool testTimeInterval(bool shortError,
			     NSTimeInterval timeInterval,
			     ESTimeZone     *estz) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    double timeIntervalI = floor(timeInterval);
    double secondsF = timeInterval - timeIntervalI;
    NSDate *date = [NSDate dateWithTimeIntervalSinceReferenceDate:timeIntervalI];

    // UTC ***************
    NSDateComponents *dc = [estz->utcCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
						fromDate:date];
    //printf("dc.day is %d\n", dc.day);
    int eraESCal;
    int yearESCal;
    int monthESCal;
    int dayESCal;
    int hourESCal;
    int minuteESCal;
    double secondsESCal;
    ESCalendar_UTCComponentsFromTimeInterval(timeInterval, &eraESCal, &yearESCal, &monthESCal, &dayESCal, &hourESCal, &minuteESCal, &secondsESCal);
    bool success = componentsAreEquivalent(dc.era, dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF,
					   eraESCal, yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
    bool alwaysPrint = false;
    if (alwaysPrint || !success) {
	[printLock lock];
	if (!shortError) {
	    printf("\n");
	}
	printf("testTimeInterval %s for UTC timeInterval %.9f --\n", alwaysPrint ? "report" : "mismatch", timeInterval);
	printf("...NSDate got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
	       dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
	printf("...ESCal  got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
	       eraESCal ? " CE" : "BCE", yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
	if (!shortError) {
	    printSpread(timeInterval, estz, false);
	}
	[printLock unlock];
	assert(0);
    }
    NSTimeInterval roundTrip = ESCalendar_timeIntervalFromUTCComponents(eraESCal, yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
    alwaysPrint = false;
    bool roundTripSuccess = fabs(roundTrip - timeInterval) < 0.0001;
    if (alwaysPrint || !roundTripSuccess) {
	[printLock lock];
        printf("\ntestTimeInterval roundTrip %s for UTC timeInterval %.9f => %.9f (%.4f) --\n", alwaysPrint ? "report" : "mismatch", timeInterval, roundTrip, roundTrip - timeInterval);
        printf("... input got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
               dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
        dc = [estz->utcCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
				  fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:roundTrip]];
        printf("...output got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
               dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
	printSpread(timeInterval, estz, false);
	[printLock unlock];
    }

    // LOCAL ***************

    dc = [estz->nsCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
                       fromDate:date];
    ESCalendar_localComponentsFromTimeInterval(timeInterval, estz, &eraESCal, &yearESCal, &monthESCal, &dayESCal, &hourESCal, &minuteESCal, &secondsESCal);
    success = componentsAreEquivalent(dc.era, dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF,
				      eraESCal, yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
    alwaysPrint = false;
    if (alwaysPrint || !success) {
	if (eraESCal != 1 || yearESCal < 1918 || (yearESCal > 1919 && yearESCal < 2038)) {
	    [printLock lock];
	    if (!shortError) {
		printf("\n");
	    }
	    printf("testTimeInterval %s for LOCAL (%s, %.2f hours from UTC) timeInterval %.9f --\n",
		   alwaysPrint ? "report" : "mismatch",
		   [[estz->nsTimeZone name] UTF8String],
		   ESCalendar_tzOffsetForTimeInterval(estz, timeInterval) / 3600,
		   timeInterval);
	    printf("...NSDate got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		   dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
	    printf("...ESCal  got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		   eraESCal ? " CE" : "BCE", yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
	    if (!shortError) {
		printSpread(timeInterval, estz, true);
	    }
	    [printLock unlock];
	} else {
	    static bool messageGiven = false;
	    if (!messageGiven) {
		printf("Problem with 1918-1919 or > 2038 detected and ignored\n");
		messageGiven = true;
	    }
	    success = true;
	}
    }
    roundTrip = ESCalendar_timeIntervalFromLocalComponents(estz, eraESCal, yearESCal, monthESCal, dayESCal, hourESCal, minuteESCal, secondsESCal);
    alwaysPrint = false;
    roundTripSuccess = fabs(roundTrip - timeInterval) < 0.0001;
    if (alwaysPrint || !roundTripSuccess) {
        NSDateComponents *dc2 = [estz->nsCalendar components:(NSCalendarUnitEra | NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
					      fromDate:[NSDate dateWithTimeIntervalSinceReferenceDate:roundTrip]];
	// Could be DST thingie around fall transition -- the same date components can represent two different time intervals
	if (//fabs(fabs(roundTrip-timeInterval) - 3600) > 0.0001 ||  // could also be switching to a different UTC offset interval; just make sure both intervals go to the same date rep
	    !componentsAreEquivalent(dc.era, dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second,
				     dc2.era, dc2.year, dc2.month, dc2.day, dc2.hour, dc2.minute, dc2.second)) {
	    [printLock lock];
	    printf("\ntestTimeInterval roundTrip %s for LOCAL (%s, %.2f hours from UTC) timeInterval %.9f => %.9f (%.2f) --\n",
		   alwaysPrint ? "report" : "mismatch",
		   [[estz->nsTimeZone name] UTF8String],
		   ESCalendar_tzOffsetForTimeInterval(estz, timeInterval) / 3600,
		   timeInterval,
		   roundTrip,
		   roundTrip - timeInterval);
	    printf("... input got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		   dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
	    printf("...output got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		   dc2.era ? " CE" : "BCE", dc2.year, dc2.month, dc2.day, dc2.hour, dc2.minute, dc2.second + secondsF);
	    printSpread(timeInterval, estz, true);
	    printSpread(roundTrip, estz, true);
	    [printLock unlock];
	} else {
	    // Ensure that the *other* time interval also resolves to the same 
	    // We already know that 

	    static bool messageGiven = true;
	    if (!messageGiven) {
		printf("DST offset roundtrip 'error' detected and ignored:\n");
		[printLock lock];
		printf("\ntestTimeInterval roundTrip %s for LOCAL (%s, %.2f hours from UTC) timeInterval %.9f => %.9f (%.2f) --\n",
		       alwaysPrint ? "report" : "mismatch",
		       [[estz->nsTimeZone name] UTF8String],
		       ESCalendar_tzOffsetForTimeInterval(estz, timeInterval) / 3600,
		       timeInterval,
		       roundTrip,
		       roundTrip - timeInterval);
		printf("... input got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		       dc.era ? " CE" : "BCE", dc.year, dc.month, dc.day, dc.hour, dc.minute, dc.second + secondsF);
		printf("...output got %3s %4d/%02d/%02d %02d:%02d:%05.20f\n",
		       dc2.era ? " CE" : "BCE", dc2.year, dc2.month, dc2.day, dc2.hour, dc2.minute, dc2.second + secondsF);
		printSpread(timeInterval, estz, true);
		printSpread(roundTrip, estz, true);
		[printLock unlock];
		messageGiven = true;
	    }
	    roundTripSuccess = true;
	}
    }

    [pool release];
    return success && roundTripSuccess;
}

static bool testDate(ESTimeZone *estz, NSDateComponents *dc, int era, int year, int month, int day, int hour, int minute, double seconds) {
    assert(estz);
    dc.era = era;
    dc.year = year;
    dc.month = month;
    dc.day = day;
    dc.hour = hour;
    dc.minute = minute;
    int secondsI = floor(seconds);
    dc.second = secondsI;
    NSTimeInterval timeInterval = [[estz->nsCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
    int eraReturn;
    int yearReturn;
    int monthReturn;
    int dayReturn;
    int hourReturn;
    int minuteReturn;
    double secondsReturn;
    ESCalendar_localComponentsFromTimeInterval(timeInterval, estz, &eraReturn, &yearReturn, &monthReturn, &dayReturn, &hourReturn, &minuteReturn, &secondsReturn);
    bool success = 
        era == eraReturn &&
        year == yearReturn &&
        month == monthReturn &&
        day == dayReturn &&
        hour == hourReturn &&
        minute == minuteReturn &&
        fabs(seconds - secondsReturn) < 0.0001;
    bool alwaysPrint = false;
    if (alwaysPrint || !success) {
        printf("\ntestDate %s --\n", alwaysPrint ? "report" : "mismatch");
        printf("...NSDate got %3s %4d/%02d/%02d %02d:%02d:%05.2f\n",
               era ? " CE" : "BCE", year, month, day, hour, minute, seconds);
        printf("...ESCal  got %3s %4d/%02d/%02d %02d:%02d:%05.2f\n",
               eraReturn ? " CE" : "BCE", yearReturn, monthReturn, dayReturn, hourReturn, minuteReturn, secondsReturn);
    }
    return testTimeInterval(false, timeInterval, estz) && success;
}

NSTimeInterval startTimeInterval;
NSTimeInterval endTimeInterval;

void ESTestCalendarAtESTZ(ESTimeZone *estz) {
    NSDateComponents *dc = [[[NSDateComponents alloc] init] autorelease];

    testDate(estz, dc, 1, 2010, 3, 14, 7, 59, 59.01);
    testDate(estz, dc, 1, 2010, 3, 14, 8, 59, 59.01);
    testDate(estz, dc, 1, 2010, 3, 14, 9, 59, 59.01);
    testDate(estz, dc, 1, 2010, 3, 14, 10, 0, 1.01);
    testDate(estz, dc, 1, 2010, 3, 14, 11, 0, 1.01);

    testDate(estz, dc, 1, 2010, 11, 7, 7, 59, 59.01);
    testDate(estz, dc, 1, 2010, 11, 7, 8, 59, 59.01);
    testDate(estz, dc, 1, 2010, 11, 7, 9, 59, 59.01);
    testDate(estz, dc, 1, 2010, 11, 7, 10, 0, 1.01);
    testDate(estz, dc, 1, 2010, 11, 7, 11, 0, 1.01);

    testTimeInterval(false, -1743692400, estz);
    testTimeInterval(false, -1743688800, estz);
    testTimeInterval(false, -63140518800, estz);

    testTimeInterval(false, -1743692399.99, estz);
    testTimeInterval(false, -1743688799.99, estz);
    testTimeInterval(false, -63140518799.99, estz);

    testTimeInterval(false, kECJulianGregorianSwitchoverTimeInterval - 0.05, estz);
    testTimeInterval(false, kECJulianGregorianSwitchoverTimeInterval + 0.05, estz);

    testDate(estz, dc, 1, 1997, 12, 31, 0, 0, 0.01);
    testDate(estz, dc, 1, 2006, 12, 31, 0, 0, 0.01);
    testDate(estz, dc, 1, 2000, 12, 31, 0, 0, 0.01);
    testDate(estz, dc, 1, 1997, 12, 31, 23, 59, 59.01);
    testDate(estz, dc, 1, 2000, 12, 31, 23, 59, 59.01);
    testDate(estz, dc, 1, 1999, 12, 31, 23, 59, 59.01);
    testDate(estz, dc, 1, 2006, 12, 31, 23, 59, 59.01);
    testDate(estz, dc, 1, 2000, 2, 28, 23, 59, 59.01);
    testDate(estz, dc, 1, 2000, 2, 29, 0, 0, 0.01);
    testDate(estz, dc, 1, 2000, 3, 1, 0, 0, 0.01);
    testDate(estz, dc, 1, 2001, 2, 28, 0, 0, 0.01);
    testDate(estz, dc, 1, 2001, 3, 1, 0, 0, 0.01);
    testDate(estz, dc, 1, 2010, 2, 28, 0, 0, 0.01);
    testDate(estz, dc, 1, 2010, 3, 1, 0, 0, 0.01);
    testTimeInterval(false, 1.01, estz);
    testTimeInterval(false, 0.01, estz);
    testTimeInterval(false, -0.99, estz);

    // Test every second around timeInterval = 0 for 24 hours in each direction
    bool fail = false;
    for (int h = -24; h < 24; h++) {
        for (int m = 0; m < 60; m++) {
            for (int s = 0; s < 60; s++) {
                if (!testTimeInterval(false, h * 3600 + m * 60 + s, estz)) {
		    fail = true;
		    break;
		}
                if (!testTimeInterval(false, h * 3600 + m * 60 + s + 0.5, estz)) {
		    fail = true;
		    break;
		}
                if (!testTimeInterval(false, h * 3600 + m * 60 + s - 0.5, estz)) {
		    fail = true;
		    break;
		}
            }
        }
    }
    if (fail) {
	printf("every-second test failed (see above)\n");
    }

    int testNumber = 0;
    int failureCount = 0;
    for (NSTimeInterval t = startTimeInterval; t <= endTimeInterval; t += 3600) {
        if (!testTimeInterval(false, t, estz)) {
	    if (++failureCount > 10) {
		break;
	    }
	}
        if (!testTimeInterval(false, t + 0.5, estz)) {
	    if (++failureCount > 10) {
		break;
	    }
	}
        if (!testTimeInterval(false, t - 0.5, estz)) {
	    if (++failureCount > 10) {
		break;
	    }
	}
        if ((testNumber++ % 1000000) == 0) {
            printf("...%s\n", [[[NSDate dateWithTimeIntervalSinceReferenceDate:t] description] UTF8String]);
        }
    }
}

void ESTestCalendarWithTZWithOlsonID(const char *olsonID) {
    printf("Testing calendar with timezone with Olson id %s\n", olsonID);
    ESTimeZone *estz = ESCalendar_initTimeZoneFromOlsonID(olsonID);
    assert(estz);
    ESTestCalendarAtESTZ(estz);
    ESCalendar_releaseTimeZone(estz);
}

static void *testThreadBody(void *arg) {
    const char *olsonID = (const char *)arg;
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    ESTestCalendarWithTZWithOlsonID(olsonID);
    [pool release];
    return NULL;
}

static pthread_t
startThreadTestingOlsonID(const char *olsonID) {
    pthread_t thread;
    pthread_create(&thread, NULL, testThreadBody, (char *)olsonID);
    return thread;
}

static void
waitForThreadTestCompletion(pthread_t thread) {
    void *returnValue;
    printf("waiting for thread completion...\n");
    /*int errorNumber = */ pthread_join(thread, &returnValue);
    printf("... thread is complete\n");
}

static void testAllOlsonIDs() {
    ECGeoNames *geoNames = [[ECGeoNames alloc] init];
    NSArray *tzNames = [[geoNames tzNames] retain];
    printf("There are %d timezones to test\n", [tzNames count]);

    int arrayIndex = 0;

#define NUM_THREADS 8
    pthread_t threads[NUM_THREADS];
    while (arrayIndex < [tzNames count]) {
	int threadsToWaitFor = 0;
	for (int i = 0; i < NUM_THREADS; i++) {
	    if (arrayIndex < [tzNames count]) {
		if (i == 0) {
		    threads[threadsToWaitFor++] = startThreadTestingOlsonID("America/Curacao");
		} else if (i == 1) {
		    threads[threadsToWaitFor++] = startThreadTestingOlsonID("America/Anguilla");
		} else {
		    threads[threadsToWaitFor++] = startThreadTestingOlsonID([[tzNames objectAtIndex:arrayIndex++] UTF8String]);
		}
	    }
	}
	for (int i = 0; i < threadsToWaitFor; i++) {
	    waitForThreadTestCompletion(threads[i]);
	}
    }

    [tzNames release];
    [geoNames release];
}

static void sampleAllOlsonIDs() {
    ECGeoNames *geoNames = [[ECGeoNames alloc] init];
    NSArray *tzNames = [[geoNames tzNames] retain];
    printf("There are %d timezones to sample\n", [tzNames count]);

    NSDateComponents *dc = [[NSDateComponents alloc] init];

    for (int i = 0; i < [tzNames count]; i++) {
	const char *olsonID = [[tzNames objectAtIndex:i] UTF8String];
	printf("\nSampling %s\n", olsonID);
	ESTimeZone *estz = ESCalendar_initTimeZoneFromOlsonID(olsonID);
	assert(estz);

	bool doLocal = true;

	dc.era = 0;
	dc.year = 4000;
	dc.month = 1;
	dc.day = 1;
	dc.hour = 0;
	dc.minute = 0;
	double seconds = 0;
	int secondsI = 0;
	dc.second = 0;
	NSTimeInterval timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	//testTimeInterval(true, timeInterval, estz, doLocal);

	dc.era = 1;
	dc.year = 1700;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	dc.year = 1800;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	dc.year = 1900;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	dc.year = 2000;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	dc.year = 2010;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	dc.year = 2262;
	dc.month = 11;
	dc.day = 1;
	dc.hour = 7;
	seconds = 0.5;
	secondsI = 0;
	dc.second = 0;
	timeInterval = [[estz->utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate] + (seconds - secondsI);
	printOne(timeInterval, estz, doLocal);
	testTimeInterval(true, timeInterval, estz);

	ESCalendar_releaseTimeZone(estz);
    }


    [dc release];

    [tzNames release];
    [geoNames release];
}

void ESTestCalendar() {
    if (!printLock) {
	printLock = [[NSLock alloc] init];
    }

    NSCalendar *utcCalendar = [[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] autorelease];
    [utcCalendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];

    // Test every hour from 4000 BCE to 2800 CE
    NSDateComponents *dc = [[NSDateComponents alloc] init];
    dc.era = 1; // 0;
    dc.year = 1900; // 4000;
    dc.month = 1;
    dc.day = 1;
    dc.hour = 0;
    dc.minute = 0;
    dc.second = 0;
    startTimeInterval = [[utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate];
    dc.era = 1;
    dc.year = 2800;
    dc.month = 12;
    dc.day = 31;
    dc.hour = 23;
    dc.minute = 59;
    dc.second = 59;
    endTimeInterval = [[utcCalendar dateFromComponents:dc] timeIntervalSinceReferenceDate];
    [dc release];
    dc = nil;

    printf("ECTestCalendar performance test beginning\n");
    double startTime = CACurrentMediaTime();
    int testNumber = 0;
#if 0
    if (!ESCalendar_nscalendarIsPureGregorian) {
	printf("Running calendar tests on pre-4.0 os\n");
	for (NSTimeInterval t = startTimeInterval; t <= endTimeInterval; t += 3600) {
	    int eraESCal;
	    int yearESCal;
	    int monthESCal;
	    int dayESCal;
	    int hourESCal;
	    int minuteESCal;
	    double secondsESCal;
	    ESCalendar_UTCComponentsFromTimeInterval(t + 0.01, &eraESCal, &yearESCal, &monthESCal, &dayESCal, &hourESCal, &minuteESCal, &secondsESCal);
	    if ((testNumber++ % 1000000) == 0) {
//          printf("...%s\n", [[[NSDate dateWithTimeIntervalSinceReferenceDate:t] description] UTF8String]);
	    }
	}
    }
#endif
    double endTime = CACurrentMediaTime();
    printf("ECTestCalendar performance test (%d tests) end after %.4f seconds, %.3f microseconds per test\n", testNumber, endTime - startTime, 1000000.0 * (endTime - startTime)/testNumber);
    startTime = CACurrentMediaTime();
    printf("ECTestCalendar verification test start\n");
    sampleAllOlsonIDs();
    testAllOlsonIDs();
    endTime = CACurrentMediaTime();
    printf("ECTestCalendar verification test end after %.4f seconds\n", endTime - startTime);
}

@interface FooInterface : NSObject {
}
@end
@implementation FooInterface
-(NSDate *)dateByAddingTimeInterval:(NSTimeInterval)timeInterval {  // Dummy method to fake out compiler
    return nil;
}
@end

NSDate *addTimeIntervalToDate(NSDate         *date,
			      NSTimeInterval timeInterval) {
    if ([NSDate instancesRespondToSelector:@selector(dateByAddingTimeInterval:)]) {   // 4.0 or later
	return [(id)date dateByAddingTimeInterval:timeInterval];
    } else {  // earlier than 4.0
#if __IPHONE_4_0
	return [(id)date addTimeInterval:timeInterval];
#else
	return [date addTimeInterval:timeInterval];
#endif
    }
}

// Doesn't look like we need this.  But if we do it needs to be rewritten using new interfaces
#if 0

NSDate *earliest;
#define MAXDAYSBACK 365.25*100
NSString *tzCompare(NSTimeZone *t1, NSTimeZone *t2)
{
    double inc = 0;		    // days
    while (inc<MAXDAYSBACK) {
	NSDate *d = addTimeIntervalToDate([NSDate date], -86400*inc);	    // inc days ago
	if (([t1 secondsFromGMTForDate:d] != [t2 secondsFromGMTForDate:d]) ||
	    (![[t1 nextDaylightSavingTimeTransitionAfterDate:d] isEqualToDate:[t2 nextDaylightSavingTimeTransitionAfterDate:d]]) ||
	    ([t1 isDaylightSavingTimeForDate:d] != [t2 isDaylightSavingTimeForDate:d])) {
	    if (inc == 0) {
		return nil;
	    } else {
		while (([t1 secondsFromGMTForDate:d] != [t2 secondsFromGMTForDate:d]) ||
		       (![[t1 nextDaylightSavingTimeTransitionAfterDate:d] isEqualToDate:[t2 nextDaylightSavingTimeTransitionAfterDate:d]]) ||
		       ([t1 isDaylightSavingTimeForDate:d] != [t2 isDaylightSavingTimeForDate:d])) {
		    d = addTimeIntervalToDate(d, 14*60);
		}
		[dateFormatter setTimeZone:t1];
		earliest = [d earlierDate:earliest];
		return [NSString stringWithFormat:@"\t\t\t\t =  %-30s since %@\n",[[t2 name] UTF8String],[dateFormatter stringFromDate:d]];
	    }
	}
	inc += 365.25/12;	    // about a month
    }
    return [NSString stringWithFormat:@"\t\t\t\t %s %@\n",[[t1 data] isEqualToData:[t2 data]] ? "==" : "= ",[t2 name]];
}

static NSString *
ESCalendar_formatTZInfo2(NSTimeZone *tz) {
    [dateFormatter setTimeZone:tz];
    [timeFormatter setTimeZone:tz];
    NSDate *now = [NSDate date];
    NSString *abbrevNow = [tz abbreviationForDate:now];
    double offsetNow = [tz secondsFromGMTForDate:now]/3600.0;
    NSDate *nextDSTTransition = ECNextDSTDateAfterDate(tz, [NSDate date]);
    if (!nextDSTTransition) {
	return [NSString stringWithFormat:@"%6s = UTC%6s %33s", [abbrevNow UTF8String], ESCalendar_formatTZOffset(offsetNow),"no DST"];
    } else {
	NSString *abbrevThen = [tz abbreviationForDate:addTimeIntervalToDate(nextDSTTransition,1)];
	double offsetThen = [tz secondsFromGMTForDate:addTimeIntervalToDate(nextDSTTransition,1)]/3600.0;
	NSString *transitionDate = [dateFormatter stringFromDate:nextDSTTransition];
	NSString *nextTransitionDate = [NSString stringWithFormat:@"%@ = UTC%@ on %@", abbrevThen, ESCalendar_formatTZOffset(offsetThen), transitionDate];
	return [NSString stringWithFormat:@"%6s = UTC%6s %33s", [abbrevNow UTF8String], ESCalendar_formatTZOffset(offsetNow), [nextTransitionDate UTF8String]];
    }
    [dateFormatter setTimeZone:currentTZ->nsTimeZone];
    [timeFormatter setTimeZone:currentTZ->nsTimeZone];
}

static NSString *
dumpTZInfo(NSTimeZone *tz) {
    [dateFormatter setTimeZone:tz];
    NSDate *dat = [NSDate dateWithTimeIntervalSinceReferenceDate:-86400*MAXDAYSBACK];
    NSString *ret = [NSString stringWithFormat:@"\t\t\t    DST transitions:\n\t\t\t\t%12s\t%6s  \t",
		     [[dateFormatter stringFromDate:dat] UTF8String],
		     [[self formatTZOffset:[tz secondsFromGMTForDate:dat]/3600.0] UTF8String]];
    NSDate *last = addTimeIntervalToDate([NSDate date], 86400*365.25);
    int cnt = 1;
    char *tabnl = "\t";
    while ([dat compare:last] == NSOrderedAscending) {
	if (!ECNextDSTDateAfterDate(tz,dat)) {
	    ret = [NSString stringWithFormat:@"%@     no DST \t%6s\n", ret, [[self formatTZOffset:[tz secondsFromGMTForDate:dat]/3600.0] UTF8String]];
	    break;
	} else {
	    if (++cnt % 6 == 0) {
		tabnl = "\n\t\t\t\t";
	    } else {
		tabnl = "\t";
	    }

	    dat = addTimeIntervalToDate(ECNextDSTDateAfterDate(tz,dat),1);
	    ret = [NSString stringWithFormat:@"%@%12s\t%6s %s%s",ret,
		   [[dateFormatter stringFromDate:dat] UTF8String],
		   [[self formatTZOffset:[tz secondsFromGMTForDate:dat]/3600.0] UTF8String],
		   [tz isDaylightSavingTimeForDate:dat] ? "D" : " ",
		   tabnl];
	}
    }
    if (tabnl == "\t") {
	ret = [ret stringByAppendingString:@"\n"];
    }
#if 0
    double daysBack = MAXDAYSBACK;
    double inc = 0.25;
    double lastoff = 99;
    ret = [ret stringByAppendingString:@"\t\t\t    UTC Offset transitions:\n"];
    while (daysBack > -366*2) {
	NSDate *d = addTimeIntervalToDate([NSDate date], -86400*daysBack);
	double off = [tz secondsFromGMTForDate:d]/3600.0;
	if (off != lastoff) {
	    if (inc < 1) {
		ret = [NSString stringWithFormat:@"%@\t\t\t\t\t%12s\t%6s %s\n",
		       ret,
		       [[dateFormatter stringFromDate:d] UTF8String],
		       [[self formatTZOffset:off] UTF8String],
		       [tz isDaylightSavingTimeForDate:d] ? "D" : "" ];
		inc = 365.25/12;
		lastoff = off;
	    } else {
		daysBack += 365.24/12;	// back up one month
		inc = 1.0/12;		// and search more finely
	    }
	}
	daysBack = daysBack - inc;
    }
#endif
    return ret;
}

+ (NSString *)TZMaxMinOffsets:(NSTimeZone *)tz {
    double daysBack = -800;
    double inc = 0.25;
    double minOff = 99999;
    double delta;
    double maxOff = -99999;
    NSDate *d2 = nil;
    while (daysBack < MAXDAYSBACK) {
	NSDate *d = addTimeIntervalToDate([NSDate date], -86400*daysBack);
	double off = [tz secondsFromGMTForDate:d]/3600.0;
	maxOff = fmax(maxOff, off);
	minOff = fmin(minOff, off);
	if (d2 == nil && ((maxOff - minOff) > 1)) {
	    d2 = d;
	}
	daysBack = daysBack + inc;
    }
    delta = maxOff - minOff;
    NSString *ret = [NSString stringWithFormat:@"min Offset = %6.2f   maxOffset = %6.2f    delta=%6.2f", minOff, maxOff, delta];
    if (d2) {
	ret = [ret stringByAppendingFormat:@"   %@", [d2 description]];
    }
    if (true || delta > 1) {
	ret = [ret stringByAppendingFormat:@"\n %@", [ECOptions dumpTZInfo:tz]];
    }
    return ret;
}
#endif

#ifndef NDEBUG
NSDate *earliest;
NSInteger tzSort(NSTimeZone *t1, NSTimeZone *t2, void *context)
{
    int v1 = [t1 secondsFromGMT];
    int v2 = [t2 secondsFromGMT];
    if (v1 < v2)
        return NSOrderedAscending;
    else if (v1 > v2)
        return NSOrderedDescending;
    else {
	NSString *tn1 = [t1 name];
	NSString *tn2 = [t2 name];
	if ([tn1 compare:@"UTC"] == NSOrderedSame && ![tn2 compare:@"UTC"] == NSOrderedSame) {
	    return NSOrderedAscending;
	} else 	if ([tn2 compare:@"UTC"] == NSOrderedSame && ![tn1 compare:@"UTC"] == NSOrderedSame) {
	    return NSOrderedDescending;
	} else 	if ([tn1 hasPrefix:@"Etc/GMT"] && ![tn2 hasPrefix:@"Etc/GMT"]) {
	    return NSOrderedAscending;
	} else 	if ([tn2 hasPrefix:@"Etc/GMT"] && ![tn1 hasPrefix:@"Etc/GMT"]) {
	    return NSOrderedDescending;
	} else {
	    return [[t1 name] compare:[t2 name]];
	}
    }
}
#endif

#if 0
void ESCalendar_printAllTimeZones() {
#ifndef NDEBUG
    NSMutableArray *tzs2 = [[NSMutableArray alloc] initWithCapacity:510];
    for (NSString *tzn in [NSTimeZone knownTimeZoneNames]) {
	[tzs2 addObject:[NSTimeZone timeZoneWithName:tzn]];
    }
    NSString **others = [ECOptionsTZRoot otherTZs];
    for (int i=0; i<NUMOTHERS; i++) {
	[tzs2 addObject:[NSTimeZone timeZoneWithName:others[i]]];
    }
    NSArray *tzs = [tzs2 sortedArrayUsingFunction:tzSort context:NULL];
    [tzs2 release];
    for (int i=0; i<[tzs count]; i++) {
	NSTimeZone *tzi = [tzs objectAtIndex:i];
	printf("* %30s : %s\n",[[tzi name] UTF8String], "Foo"/*[[ECOptions TZMaxMinOffsets:tzi] UTF8String]*/);
    }
#undef PRINTALLZONEINFO
#ifdef PRINTALLZONEINFO
    for (int i=0; i<[tzs count]; i++) {
	NSTimeZone *tzi = [tzs objectAtIndex:i];
	printf("* %30s = %s\n",[[tzi name] UTF8String], [[ECOptions formatInfoForTZ:tzi type:3] UTF8String]);
	printf("%s", [[ECOptions dumpTZInfo:tzi] UTF8String]);
	printf("\t\t\t    Equivalent zones:\n");
	for (int j=0; j<[tzs count]; j++) {
	    if (i != j) {
		NSTimeZone *tzj = [tzs objectAtIndex:j];
		NSString *dif = tzCompare(tzi,tzj);
		if ([dif compare:@""] != NSOrderedSame) {
		    printf("%s", [dif UTF8String]);
		}
	    }
	}
	printf("\n");
    }
#endif
    [ChronometerAppDelegate noteTimeAtPhaseWithString:[NSString stringWithFormat:@"Earliest diff at %@; system zone: %s", [earliest description], @"Foo"/*[ECOptions formatTZInfo2:[NSTimeZone systemTimeZone]]*/]];	    // restores dateFormatter as a side effect

    NSDictionary *abd = [NSTimeZone abbreviationDictionary];
    for (id key in abd) {
	printf("%s = %s\n",[key UTF8String],[[abd objectForKey:key]UTF8String]);
    }
    printf("%s\n",[[abd description]UTF8String]);
#endif
}
#endif

#endif  // ESCALENDAR_TEST
