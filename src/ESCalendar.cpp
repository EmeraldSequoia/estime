//
//  ESCalendar.cpp
//
//  Created by Steve Pucci 25 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

// Input time is presumed to be in Cocoa format (seconds since 1/1/2001 UTC)

#include "ESCalendar.hpp"
#include "ESCalendarPvt.hpp"
#include "ESUtil.hpp"

#include <math.h>
#include <assert.h>

// Return calendar components from an ESTimeInterval (UTC)
void
ESCalendar_UTCComponentsFromTimeInterval(ESTimeInterval timeInterval,
                                         int            *era,
					 int            *year,
					 int            *month,
					 int            *day,
					 int            *hour,
					 int            *minute,
					 double         *seconds) {
    double xRemainder;
    int signedYear;
    double x0;
    if (timeInterval < kECJulianGregorianSwitchoverTimeInterval) {
        double x1F = 730793 + timeInterval/(24 * 3600);
        double x1 = floor(x1F);  // Algorithm only works on even day boundaries?  At least that's true of Gregorian
        xRemainder = x1F - x1;
        signedYear = floor((4 * x1 + 3) / kECDaysInJulianCycle);
        x0 = x1 - floor(kECDaysInJulianCycle * signedYear / 4.0);
    } else {
        double x2F = 730791 + timeInterval/(24 * 3600);

        double x2 = floor(x2F);  // Algorithm only works on even day boundaries; else has trouble at end of year, e.g., 12/31/1997 23:59:59 and back several hours
        xRemainder = x2F - x2;

        int century = floor(4 * x2 + 3) / kECDaysInGregorianCycle;
        double x1 = x2 - floor(kECDaysInGregorianCycle * century / 4.0);
        int yearWithinCentury = floor((100 * x1 + 99) / kECDaysInNonLeapCentury);
        signedYear = (100 * century) + yearWithinCentury;
        x0 = x1 - floor(kECDaysInNonLeapCentury * yearWithinCentury / 100.0);
    }
    int monthI = floor((5 * x0 + 461) / 153);
    if (monthI > 12) {
        *month = monthI - 12;
        signedYear++;
    } else {
        *month = monthI;
    }
    if (signedYear <= 0) {
        *era = 0;
        *year = 1 - signedYear;
    } else {
        *era = 1;
        *year = signedYear;
    }
    double dayF = x0 - floor((153 * monthI - 457) / 5.0) + 1;
    *day = round(dayF);
    double hoursF = xRemainder * 24;
    int hoursI = floor(hoursF);
    *hour = hoursI;
    double minutesF = (hoursF - hoursI) * 60;
    int minutesI = floor(minutesF);
    *minute = minutesI;
    *seconds = (minutesF - minutesI) * 60;
}

void
ESCalendar_UTCDateComponentsFromTimeInterval(ESTimeInterval   timeInterval,
					     ESDateComponents *cs) {
    ESCalendar_UTCComponentsFromTimeInterval(timeInterval, &cs->era, &cs->year, &cs->month, &cs->day, &cs->hour, &cs->minute, &cs->seconds);
}

// Return an ESTimeInterval from calendar components (UTC)
ESTimeInterval
ESCalendar_timeIntervalFromUTCComponents(int    era,
					 int    year,
					 int    month,
					 int    day,
					 int    hour,
					 int    minute,
					 double seconds) {
    int signedYear = era == 0 ? 1 - year : year;
    int monthI;
    if (month < 3) {
        monthI = month + 12;
        signedYear--;
    } else {
        monthI = month;
    }
    double J;
    if (era == 0 ||
        year < 1582 ||
        (year == 1582 &&
         (month < 10 ||
          (month == 10 && day < 15)))) {  // Could be < 5 instead; 5-14 inclusive are really undefined in this convention
        J = 1721116.5 + floor(1461 * signedYear / 4.0);
    } else {
        // Gregorian
        double c = floor(signedYear/100.0);
        double x = signedYear - 100 * c;
        J = 1721118.5 + floor(146097 * c / 4.0) + floor(36525 * x / 100);
    }
    J += floor((153 * monthI - 457) / 5.0) + day;
    ESTimeInterval returnTimeInterval = (J - kECJulianDayOf1990Epoch)*24*3600 + kEC1990Epoch + hour * 3600 + minute * 60 + seconds;
    //printf("timeIntervalFromUTC %d-%04d-%02d-%02d-%02d:%02d returning %.2f\n", era, year, month, day, hour, minute, returnTimeInterval);
    return returnTimeInterval;
}

// Return an ESTimeInterval from calendar components (UTC)
ESTimeInterval
ESCalendar_timeIntervalFromUTCDateComponents(ESDateComponents *cs) {
    return ESCalendar_timeIntervalFromUTCComponents(cs->era, cs->year, cs->month, cs->day, cs->hour, cs->minute, cs->seconds);
}

// Return the weekday from an ESTimeInterval (UTC)  (0 == Sunday)
extern int
ESCalendar_UTCWeekdayFromTimeInterval(ESTimeInterval timeInterval) {
    double intervalDays = timeInterval / (24 * 3600);
    double weekday = ESUtil::fmod(intervalDays + 1, 7);
    //printf("weekdayNumber is %d (%.4f)\n", (int)floor(weekday), weekday);
    return (int)floor(weekday);
}

// Return the weekday in the given timezone from an ESTimeInterval  (0 == Sunday)
extern int
ESCalendar_localWeekdayFromTimeInterval(ESTimeInterval timeInterval,
                                        ESTimeZone     *timeZone) {
    double localNow = timeInterval + ESCalendar_tzOffsetForTimeInterval(timeZone, timeInterval);
    double localNowDays = localNow / (24 * 3600);
    double weekday = ESUtil::fmod(localNowDays + 1, 7);
    //printf("weekdayNumber is %d (%.4f)\n", (int)floor(weekday), weekday);
    return (int)floor(weekday);
}

void
ESCalendar_localDateComponentsFromTimeInterval(ESTimeInterval   timeInterval,
					       ESTimeZone       *timeZone,
					       ESDateComponents *cs) {
    ESCalendar_localComponentsFromTimeInterval(timeInterval, timeZone, &cs->era, &cs->year, &cs->month, &cs->day, &cs->hour, &cs->minute, &cs->seconds);
}


ESTimeInterval
ESCalendar_timeIntervalFromLocalDateComponents(ESTimeZone       *timeZone,
					       ESDateComponents *cs) {
    return ESCalendar_timeIntervalFromLocalComponents(timeZone, cs->era, cs->year, cs->month, cs->day, cs->hour, cs->minute, cs->seconds);
}

int
ESCalendar_daysInYear(ESTimeZone     *estz,
		      ESTimeInterval forTime)
{
    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(forTime, estz, &cs);
    cs.day = 1;
    cs.month = 1;
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 0;
    double d1 = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
    cs.day = 31;
    cs.month = 12;
    cs.hour = 23;
    cs.minute = 23;
    cs.seconds = 59;
    double d2 = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
    return rint((d2 - d1) / (3600.0 * 24.0));
}

ESTimeInterval
ESCalendar_addDaysToTimeInterval(ESTimeInterval now,
				 ESTimeZone     *estz,
				 int            days) {
    int eraNow;
    int yearNow;
    int monthNow;
    int dayNow;
    int hourNow;
    int minuteNow;
    double secondsNow;
    ESCalendar_localComponentsFromTimeInterval(now, estz, &eraNow, &yearNow, &monthNow, &dayNow, &hourNow, &minuteNow, &secondsNow);
    double timeSinceMidnightNow = hourNow * 3600 + minuteNow * 60 + secondsNow;

    // Go the given number of exact 24-hour segments.  Then find the closest time which reproduces hour, minute, second of current time
    ESTimeInterval then = now + days * 86400.0;
    int eraThen;
    int yearThen;
    int monthThen;
    int dayThen;
    int hourThen;
    int minuteThen;
    double secondsThen;
    ESCalendar_localComponentsFromTimeInterval(then, estz, &eraThen, &yearThen, &monthThen, &dayThen, &hourThen, &minuteThen, &secondsThen);
    double timeSinceMidnightThen = hourThen * 3600 + minuteThen * 60 + secondsThen;
    
    double deltaError = timeSinceMidnightThen - timeSinceMidnightNow;
    if (fabs(deltaError) < 0.001) {
	return then;
    } else { // must have been a DST change
	if (deltaError > 0) {
	    if (deltaError > 12 * 3600) {   // e.g., now 12:30am, then 11:30p
		deltaError -= 24 * 3600;      //  => logically we moved backwards 1 hour
	    }
	} else {  // deltaError <= 0
	    if (deltaError < -12 * 3600) {  // e.g., now 11:30p, then 12:30am
		deltaError += 24 * 3600;       // => logically we moved forwards 1 hour
	    }
	}
	return then - deltaError;   // If now 2pm, then 3pm, delta +1hr, need to move back by 1 hour to stay at 2pm per spec
    }
}

int
ESCalendar_daysInMonth(int eraNumber, int yearNumber, int monthNumber) {
    switch(monthNumber - 1) {
      case  0: return 31;	// Jan
      case  1: 
	{
	    ESDateComponents cs;
	    cs.era = eraNumber;
	    cs.year = yearNumber;
	    cs.month = 2;
	    cs.day = 1;
	    cs.hour = 0;
	    cs.minute = 0;
	    cs.seconds = 0;
	    ESTimeInterval firstOfFeb = ESCalendar_timeIntervalFromUTCDateComponents(&cs);
	    cs.month = 3;
	    ESTimeInterval firstOfMar = ESCalendar_timeIntervalFromUTCDateComponents(&cs);
	    return (int)(rint((firstOfMar - firstOfFeb)/(24 * 3600)));
	}
      case  2: return 31;	// Mar
      case  3: return 30;	// Apr
      case  4: return 31;	// May
      case  5: return 30;	// Jun
      case  6: return 31;	// Jul
      case  7: return 31;	// Aug
      case  8: return 30;	// Sep
      case  9: return 31;	// Oct
      case 10: return 30;	// Nov
      case 11: return 31;	// Dec
    }
    assert(0);
    return 0;
}

ESTimeInterval
ESCalendar_addMonthsToTimeInterval(ESTimeInterval now,
				   ESTimeZone     *estz,
				   int            months) {
    int eraNow;
    int yearNow;
    int monthNow;
    int dayNow;
    int hourNow;
    int minuteNow;
    double secondsNow;
    ESCalendar_localComponentsFromTimeInterval(now, estz, &eraNow, &yearNow, &monthNow, &dayNow, &hourNow, &minuteNow, &secondsNow);
    int signedYearNow = eraNow == 0 ? 1 - yearNow : yearNow;
    int zeroMonthNow = monthNow - 1;
    double yearMonthThen = signedYearNow + (zeroMonthNow + months) / 12.0;
    int signedYearThen = floor(yearMonthThen);
    int zeroMonthThen = rint((yearMonthThen - signedYearThen) * 12);
    int monthThen = zeroMonthThen + 1;
    int eraThen;
    int yearThen;
    if (signedYearThen <= 0) {
	eraThen = 0;
	yearThen = 1 - signedYearThen;
    } else {
	eraThen = 1;
	yearThen = signedYearThen;
    }
    int daysInMonthThen = ESCalendar_daysInMonth(eraThen, yearThen, monthThen);
    int dayThen = dayNow;
    if (dayThen > daysInMonthThen) {
	dayThen = daysInMonthThen;
    }
    return ESCalendar_timeIntervalFromLocalComponents(estz, eraThen, yearThen, monthThen, dayThen, hourNow, minuteNow, secondsNow);
}

ESTimeInterval
ESCalendar_addYearsToTimeInterval(ESTimeInterval now,
				  ESTimeZone     *estz,
				  int            years) {
    int eraNow;
    int yearNow;
    int monthNow;
    int dayNow;
    int hourNow;
    int minuteNow;
    double secondsNow;
    ESCalendar_localComponentsFromTimeInterval(now, estz, &eraNow, &yearNow, &monthNow, &dayNow, &hourNow, &minuteNow, &secondsNow);
    int signedYearNow = eraNow == 0 ? 1 - yearNow : yearNow;
    int signedYearThen = signedYearNow + years;
    int eraThen;
    int yearThen;
    if (signedYearThen <= 0) {
	eraThen = 0;
	yearThen = 1 - signedYearThen;
    } else {
	eraThen = 1;
	yearThen = signedYearThen;
    }
    return ESCalendar_timeIntervalFromLocalComponents(estz, eraThen, yearThen, monthNow, dayNow, hourNow, minuteNow, secondsNow);
}

void
ESCalendar_gregorianToHybrid(int *era,
			     int *year,
			     int *month,
			     int *day) {
    // If the specified date is in the hybrid's Gregorian section, there's nothing to do
    if (*era == 1 &&
	(*year > 1582 ||
	 (*year == 1582 &&
	  (*month > 10 ||
	   (*month == 10 &&
	    *day >= 15))))) {
	return;
    }
    //ESTimeInterval testInterval = ESCalendar_timeIntervalFromUTCComponents(*era, *year, *month, *day, 12, 0, 0);
    //printf("gTH input timeInterval (assuming UTC) of %.2f\n", testInterval);
    // Convert from Gregorian date to Julian day number
    int signedYear = *era == 0 ? 1 - *year : *year;
    int monthI;
    if (*month < 3) {
        monthI = *month + 12;
        signedYear--;
    } else {
        monthI = *month;
    }
    //printf("\nyear %d\n", *year);
    //printf("signedYear %d\n", signedYear);
    double c = floor(signedYear/100.0);
    //printf("c %.1f\n", c);
    double x = signedYear - 100 * c;
    //printf("x %.1f\n", x);
    double J = 1721118.5 + floor(146097 * c / 4) + floor(36525 * x / 100);
    J += floor((153 * monthI - 457) / 5.0) + *day;
    //printf("Julian gTH %.1f\n", J);

    // Then from Julian day number to Julian date
    double x1F = J - 1721117.5;
    double x1 = floor(x1F);  // Algorithm only works on even day boundaries?  At least that's true of Gregorian
    signedYear = floor((4 * x1 + 3) / kECDaysInJulianCycle);
    double x0 = x1 - floor(kECDaysInJulianCycle * signedYear / 4.0);
    monthI = floor((5 * x0 + 461) / 153);
    if (monthI > 12) {
        *month = monthI - 12;
        signedYear++;
    } else {
        *month = monthI;
    }
    if (signedYear <= 0) {
        *era = 0;
        *year = 1 - signedYear;
    } else {
        *era = 1;
        *year = signedYear;
    }
    double dayF = x0 - floor((153 * monthI - 457) / 5.0) + 1;
    *day = round(dayF);
    //testInterval = ESCalendar_timeIntervalFromUTCComponents(*era, *year, *month, *day, 12, 0, 0);
    //printf("gTH output timeInterval (assuming UTC) of %.2f\n", testInterval);
}

void
ESCalendar_hybridToGregorian(int *era,
			     int *year,
			     int *month,
			     int *day) {
    // If the specified date is in the hybrid's Gregorian section, there's nothing to do
    if (*era == 1 &&
	(*year > 1582 ||
	 (*year == 1582 &&
	  (*month > 10 ||
	   (*month == 10 &&
	    *day >= 15))))) {
	return;
    }
    //ESTimeInterval testInterval = ESCalendar_timeIntervalFromUTCComponents(*era, *year, *month, *day, 12, 0, 0);
    //printf("hTG input timeInterval (assuming UTC) of %.2f\n", testInterval);
    // Convert from Julian date to Julian day number
    int signedYear = *era == 0 ? 1 - *year : *year;
    int monthI;
    if (*month < 3) {
        monthI = *month + 12;
        signedYear--;
    } else {
        monthI = *month;
    }
    double J = 1721116.5 + floor(1461 * signedYear / 4.0);
    J += floor((153 * monthI - 457) / 5.0) + *day;
    //printf("\nJulian %.1f\n", J);

    // Then from Julian day number to Gregorian date
    double x2F = J - 1721119.5;
    int x2 = floor(x2F);  // Algorithm only works on even day boundaries; else has trouble at end of year, e.g., 12/31/1997 23:59:59 and back several hours

    int century = floor((4 * x2 + 3) / kECDaysInGregorianCycle);
    double x1 = x2 - floor(kECDaysInGregorianCycle * century / 4.0);
    int yearWithinCentury = floor((100 * x1 + 99) / kECDaysInNonLeapCentury);
    signedYear = (100 * century) + yearWithinCentury;
    double x0 = x1 - floor(kECDaysInNonLeapCentury * yearWithinCentury / 100.0);
    monthI = floor((5 * x0 + 461) / 153);
    if (monthI > 12) {
        *month = monthI - 12;
        signedYear++;
    } else {
        *month = monthI;
    }
    if (signedYear <= 0) {
        *era = 0;
        *year = 1 - signedYear;
    } else {
        *era = 1;
        *year = signedYear;
    }
    double dayF = x0 - floor((153 * monthI - 457) / 5.0) + 1;
    *day = round(dayF);
    //testInterval = ESCalendar_timeIntervalFromUTCComponents(*era, *year, *month, *day, 12, 0, 0);
    //printf("hTG output timeInterval (assuming UTC) of %.2f\n", testInterval);
}

// Find the first index for which that multiple of 'searchByInterval' from 'now' results in 
// an isDST...() value different from isDSTNow, with the given bounds knownLikeNowIndex and
// knownLikeThenIndex.
static int
binarySearch(ESTimeZone     *estz,
             bool           isDSTNow,
             ESTimeInterval now,
             ESTimeInterval searchByInterval,
             int            knownLikeNowIndex,
             int            knownLikeThenIndex) {
    ESAssert(knownLikeNowIndex < knownLikeThenIndex);
    ESAssert(isDSTNow == ESCalendar_isDSTAtTimeInterval(estz, now + searchByInterval * knownLikeNowIndex));
    ESAssert(isDSTNow != ESCalendar_isDSTAtTimeInterval(estz, now + searchByInterval * knownLikeThenIndex));
    int count = 0;
    while (true) {
        // ESErrorReporter::logInfo("binarySearch", "%d -> %d", knownLikeNowIndex, knownLikeThenIndex);
        if (++count > 100) {
            ESAssert(false);
        }
        if (knownLikeNowIndex == knownLikeThenIndex) {
            ESAssert(false);  // Can't be both like then and like now.
        }
        int tryIndex = (knownLikeNowIndex + knownLikeThenIndex + 1) / 2;
        if (tryIndex == knownLikeThenIndex) {
            ESAssert(tryIndex != knownLikeNowIndex);
            return knownLikeThenIndex;
        }
        ESAssert(tryIndex >= knownLikeNowIndex);
        ESAssert(tryIndex <= knownLikeThenIndex);
        ESTimeInterval tryInterval = now + tryIndex * searchByInterval;
        bool isDSTThen = ESCalendar_isDSTAtTimeInterval(estz, tryInterval);
        if (isDSTThen == isDSTNow) {
            knownLikeNowIndex = tryIndex;
        } else {
            knownLikeThenIndex = tryIndex;
        }
        ESAssert(isDSTNow == ESCalendar_isDSTAtTimeInterval(estz, now + searchByInterval * knownLikeNowIndex));
        ESAssert(isDSTNow != ESCalendar_isDSTAtTimeInterval(estz, now + searchByInterval * knownLikeThenIndex));
    }
    ESAssert(false);
    return 0;
}

// Return true iff isDST changes between one second before and one second after the given interval.
static bool
tryAtInterval(ESTimeZone     *estz,
              ESTimeInterval timeInterval) {
    bool isDSTJustBefore = ESCalendar_isDSTAtTimeInterval(estz, timeInterval - 1);
    bool isDSTJustAfter = ESCalendar_isDSTAtTimeInterval(estz, timeInterval + 1);
    return isDSTJustAfter != isDSTJustBefore;
}

ESTimeInterval
ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay(ESTimeZone     *estz,
                                                    ESTimeInterval timeInterval) {
    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "start %s", ESCalendar_timeZoneName(estz).c_str());
    bool isDSTNow = ESCalendar_isDSTAtTimeInterval(estz, timeInterval);

    const ESTimeInterval ONE_DAY = 60 * 60 * 24;  // modulo DST changes :-)
    const int numDaysInFirstSearchPeriod = 60;  // 2 months, approximately
    const int numFirstSearchPeriods = 9;  // 18 months, approximately
    
    int period;
    for(period = 1; period < numFirstSearchPeriods; period++) {
        ESTimeInterval tryInterval = timeInterval + (period * numDaysInFirstSearchPeriod) * ONE_DAY;
        bool isDSTThen = ESCalendar_isDSTAtTimeInterval(estz, tryInterval);
        if (isDSTThen != isDSTNow) {
            break;
        }
    }
    if (period == numFirstSearchPeriods) {
        // Never found a change.  Return 0, no change.
        // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "no DST change found after %d %d-day periods (%d day)", period, numDaysInFirstSearchPeriod, period * numDaysInFirstSearchPeriod);
        return 0;
    }

    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "checkpoint 1 after %d %d-day period(s)", period, numDaysInFirstSearchPeriod);

    // Now search by days 
    int firstChangedDayIndex = binarySearch(estz, isDSTNow, timeInterval /*now*/, ONE_DAY /* searchByInterval */, 
                                            (period - 1) * numDaysInFirstSearchPeriod /* knownLikeNowIndex */,
                                            period * numDaysInFirstSearchPeriod /* knownLikeThenIndex*/);
    ESTimeInterval changedDay = timeInterval + firstChangedDayIndex * ONE_DAY;

    ESDateComponents cs;
    ESCalendar_localDateComponentsFromTimeInterval(changedDay, estz, &cs);
    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "checkpoint 2 found changeDay at %04d/%02d/%02d", cs.year, cs.month, cs.day);
    cs.hour = 0;
    cs.minute = 0;
    cs.seconds = 1;  // 1 second past midnight
    ESTimeInterval justAfterMidnightOnChangedDay = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "justAfterMidnightOnChangedDay %s", ESTime::timeAsString(justAfterMidnightOnChangedDay, 0, estz).c_str());
    bool isDSTJustAfterMidnight = ESCalendar_isDSTAtTimeInterval(estz, justAfterMidnightOnChangedDay);
    int count = 0;
    while (isDSTJustAfterMidnight != isDSTNow) {
        ESTimeInterval justBeforeMidnight = justAfterMidnightOnChangedDay - 2;  // 1 second before midnight
        // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "justBeforeMidnight %s", ESTime::timeAsString(justBeforeMidnight, 0, estz).c_str());
        bool isDSTJustBeforeMidnight = ESCalendar_isDSTAtTimeInterval(estz, justBeforeMidnight);
        if (isDSTJustBeforeMidnight != isDSTJustAfterMidnight) {
            // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "finish at midnight");
            return justBeforeMidnight + 1;  // exactly midnight
        }
        // Hmm.  The previous day at 11:59 is also changed.  Back up a day.
        ESDateComponents cs;
        ESCalendar_localDateComponentsFromTimeInterval(justBeforeMidnight, estz, &cs);
        cs.hour = 0;
        cs.minute = 0;
        cs.seconds = 1;  // 1 second past midnight
        justAfterMidnightOnChangedDay = ESCalendar_timeIntervalFromLocalDateComponents(estz, &cs);
        // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "new justAfterMidnightOnChangedDay %s", ESTime::timeAsString(justAfterMidnightOnChangedDay, 0, estz).c_str());
        isDSTJustAfterMidnight = ESCalendar_isDSTAtTimeInterval(estz, justAfterMidnightOnChangedDay);
        if (++count > 2) {
            ESAssert(false);  // we supposedly had the day it changed when we started.
        }
    }
    ESTimeInterval midnightOnChangedDay = justAfterMidnightOnChangedDay - 1;
    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "final midnightOnChangedDay %s", ESTime::timeAsString(midnightOnChangedDay, 0, estz).c_str());

    // Try at most likely time:
    ESTimeInterval tryInterval;
    if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + 2 * 60 * 60))) {
        // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "finish at 2am");
        return tryInterval;
    }
    // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "tryInterval 2am %s", ESTime::timeAsString(tryInterval, 0, estz).c_str());

    // Do exhaustive search by hours.  We go to 24 here in case we've jumped back to the previous day because we had a negative DST shift at midnight of the following day.
    for (int i = 1; i <= 24; i++) {
        if (i == 2) continue;
        if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + i * 60 * 60))) {
            // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "finish at %02d:00", i);
            return tryInterval;
        }
        // ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "tryInterval hourly %s", ESTime::timeAsString(tryInterval, 0, estz).c_str());
    }
    
    // Do exahustive search by 15-minutes.  Also going to 24 here (see previous loop)
    for (int i = 0; i <= 24; i++) {
        int m = 30;
        if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + i * 60 * 60 + m * 60))) {
            ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "30-minute finish at %02d:%02d", i, m);
            return tryInterval;
        }
        m = 15;
        if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + i * 60 * 60 + m * 60))) {
            ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "15-minute finish at %02d:%02d", i, m);
            return tryInterval;
        }
        m = 45;
        if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + i * 60 * 60 + m * 60))) {
            ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "45-minute finish at %02d:%02d", i, m);
            return tryInterval;
        }
    }

    // Do really exhaustive search
    for (int h = 0; h <= 24; h++) {
        for (int m = 1; m < 60; m++) {
            if (m == 30) {
                continue;
            }
            if (tryAtInterval(estz, (tryInterval = midnightOnChangedDay + h * 60 * 60 + m * 60))) {
                ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "really slow finish at %02d:%02d", h, m);
                return tryInterval;
            }
        }
    }

    // OK, We shouldn't get here
    ESErrorReporter::logInfo("ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay", "failed to find with exhaustive search");
    ESAssert(false);
    return 0;
}

#if 0
static void testHybridConversion() {
    ESTimeInterval timeInterval = ESCalendar_timeIntervalFromUTCComponents(0, 3999, 1, 1, 12, 0, 0);
    for (int i = 0; i < 1000; i++) {
	int eraReturn;
	int yearReturn;
	int monthReturn;
	int dayReturn;
	int hourReturn;
	int minuteReturn;
	double secondsReturn;
	ESCalendar_UTCComponentsFromTimeInterval(timeInterval, &eraReturn, &yearReturn, &monthReturn, &dayReturn, &hourReturn, &minuteReturn, &secondsReturn);
	//printf("\nHybrid %d-%04d-%02d-%02d-%02d:%02d => ", eraReturn, yearReturn, monthReturn, dayReturn, hourReturn, minuteReturn);
	int eraReturn2 = eraReturn;
	int yearReturn2 = yearReturn;
	int monthReturn2 = monthReturn;
	int dayReturn2 = dayReturn;
	int hourReturn2 = hourReturn;
	int minuteReturn2 = minuteReturn;
	double secondsReturn2 = secondsReturn;
	ESCalendar_hybridToGregorian(&eraReturn2, &yearReturn2, &monthReturn2, &dayReturn2);
	//printf("hTG => %d-%04d-%02d-%02d-%02d:%02d => ", eraReturn2, yearReturn2, monthReturn2, dayReturn2, hourReturn2, minuteReturn2);
	//assert(dayReturn != dayReturn2);
	ESCalendar_gregorianToHybrid(&eraReturn2, &yearReturn2, &monthReturn2, &dayReturn2);
	//printf("gTH => %d-%04d-%02d-%02d-%02d:%02d\n", eraReturn2, yearReturn2, monthReturn2, dayReturn2, hourReturn2, minuteReturn2);
	assert(eraReturn == eraReturn2);
	assert(yearReturn == yearReturn2);
	assert(monthReturn == monthReturn2);
	assert(dayReturn == dayReturn2);
	assert(hourReturn == hourReturn2);
	assert(minuteReturn == minuteReturn2);
	assert(fabs(secondsReturn - secondsReturn2) < 0.001);
	timeInterval -= 24*3600;
    }
}
#endif

