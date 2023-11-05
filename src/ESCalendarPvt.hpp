//
//  ESCalendarPvt.hpp
//
//  Created by Steve Pucci 25 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESCALENDARPVT_HPP_
#define _ESCALENDARPVT_HPP_

#define kECJulianDayOf1990Epoch (2447891.5)
#define kEC1990Epoch (-347241600.0)  // 12/31/1989 GMT - 1/1/2001 GMT, calculated as 24 * 3600 * (365 * 8 + 366 * 3 + 1) /*1992, 1996, 2000*/ and verified with NSCalendar
#define kECAverageDaysInGregorianYear (365.2425)
#define kECDaysInGregorianCycle (kECAverageDaysInGregorianYear * 400)
#define kECDaysInJulianCycle (365.25 * 4)
#define kECDaysInNonLeapCentury (36525)

#define kECJulianGregorianSwitchoverTimeInterval (-13197600000.0)

extern void
ESCalendar_gregorianToHybrid(int *era,
			     int *year,
			     int *month,
			     int *day);

extern void
ESCalendar_hybridToGregorian(int *era,
			     int *year,
			     int *month,
			     int *day);

void
ESCalendar_localComponentsFromTimeInterval(ESTimeInterval timeInterval,
                                           ESTimeZone     *timeZone,
                                           int            *era,
                                           int            *year,
                                           int            *month,
                                           int            *day,
                                           int            *hour,
                                           int            *minute,
                                           double         *seconds);

ESTimeInterval
ESCalendar_timeIntervalFromLocalComponents(ESTimeZone *timeZone,
                                           int         era,
                                           int         year,
                                           int         month,
                                           int         day,
                                           int         hour,
                                           int         minute,
                                           double      seconds);

void
ESCalendar_UTCComponentsFromTimeInterval(ESTimeInterval timeInterval,
                                         int            *era,
					 int            *year,
					 int            *month,
					 int            *day,
					 int            *hour,
					 int            *minute,
					 double         *seconds);

ESTimeInterval
ESCalendar_timeIntervalFromUTCComponents(int    era,
					 int    year,
					 int    month,
					 int    day,
					 int    hour,
					 int    minute,
					 double seconds);

#endif  // _ESCALENDARPVT_HPP_
