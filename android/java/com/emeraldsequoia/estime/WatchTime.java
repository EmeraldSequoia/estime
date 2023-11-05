//
//  WatchTime.java
//
//  Created by Steve Pucci 02 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

package com.emeraldsequoia.estime;

import android.util.Log;

// Wrapper for the C++ ESWatchTime class.
// It differs from the C++ class in that it locally stores the environment
// necessary to evaluate the calendar functions (this is to avoid having a
// separate wrapper for ESTimeEnvironment).
public class WatchTime {
    
    private static final String TAG = "ESWatchTime";

    private int mSerialNumber;

    // Construct a watchTime using current time at current locale.
    // TODO:  Add other constructors for other locales (aka environments).
    public WatchTime() {
        mSerialNumber = onCreate();
    }

    public void Destroy() {
        onDestroy(mSerialNumber);
        mSerialNumber = -1;
    }

    public void latchTimeForBeatsPerSecond(int beatsPerSecond) {
        latchTimeForBeatsPerSecond(mSerialNumber, beatsPerSecond);
    }

    public void latchTimeOnMinuteBoundary() {
        latchTimeOnMinuteBoundary(mSerialNumber);
    }

    public void unlatchTime() {
        unlatchTime(mSerialNumber);
    }

    // 12:35:45.9 => 45
    int                     secondNumber() {
        return secondNumber(mSerialNumber);
    }

    // 12:35:45.9 => 45.9
    double                  secondValue() {
        return secondValue(mSerialNumber);
    }

    // 12:35:45 => 35
    int                     minuteNumber() {
        return minuteNumber(mSerialNumber);
    }

    // 12:35:45 => 35.75
    double                  minuteValue() {
        return minuteValue(mSerialNumber);
    }

    // 13:45:00 => 1
    int                     hour12Number() {
        return hour12Number(mSerialNumber);
    }

    // 13:45:00 => 1.75
    double                  hour12Value() {
        return hour12Value(mSerialNumber);
    }

    // 13:45:00 => 13
    int                     hour24Number() {
        return hour24Number(mSerialNumber);
    }

    // 13:45:00 => 13.75
    double                  hour24Value() {
        return hour24Value(mSerialNumber);
    }

    // March 1 => 0  (n.b, not 1; useful for angles and for arrays of images, and consistent with double form below)
    int                     dayNumber() {
        return dayNumber(mSerialNumber);
    }

    // March 1 at 6pm  =>  0.75;  useful for continuous hands displaying day
    double                  dayValue() {
        return dayValue(mSerialNumber);
    }

    // March 1 => 2  (n.b., not 3)
    int                     monthNumber() {
        return monthNumber(mSerialNumber);
    }

    // March 1 at noon  =>  12 / (31 * 24);  useful for continuous hands displaying month
    double                  monthValue() {
        return monthValue(mSerialNumber);
    }

    // March 1 1999 => 1999
    int                     yearNumber() {
        return yearNumber(mSerialNumber);
    }

    // BCE => 0; CE => 1
    int                     eraNumber() {
        return eraNumber(mSerialNumber);
    }

    // Sunday => 0
    int                     weekdayNumber() {
        return weekdayNumber(mSerialNumber);
    }

    // Tuesday at 6pm => 2.75
    double                  weekdayValue() {
        return weekdayValue(mSerialNumber);
    }

    // This function incorporates the value of the weekdayStart passed in
    int                     weekdayNumberAsCalendarColumn(int weekdayStart) {
        return weekdayNumberAsCalendarColumn(mSerialNumber, weekdayStart);
    }

    // Leap year: fraction of 366 days since Jan 1
    // Non-leap year: fraction of 366 days since Jan 1 through Feb 28, then that plus 24hrs starting Mar 1
    // Result is indicator value on 366-year dial
    double                  year366IndicatorFraction() {
        return year366IndicatorFraction(mSerialNumber);
    }

    // Jan 1 => 0    
    int                     dayOfYearNumber() {
        return dayOfYearNumber(mSerialNumber);
    }

    // First week => 0    
    int                     weekOfYearNumber(int     weekStartDay,  // weekStartDay == 0 means weeks start on Sunday
                                             boolean useISO8601) {  // use only when weekStartDay == 1 (Monday)
        return weekOfYearNumber(mSerialNumber, weekStartDay, useISO8601);
    }

    // daylight => 1; standard => 0
    boolean                 isDST() {
        return isDST(mSerialNumber);
    }

    ///////////////////////////
    // Private deletate methods
    ///////////////////////////
    private native int  onCreate();
    private native void onDestroy(int serialNumber);

    private native void latchTimeForBeatsPerSecond(int serialNumber, int beatsPerSecond);
    private native void latchTimeOnMinuteBoundary(int serialNumber);
    private native void unlatchTime(int serialNumber);

    private native int secondNumber(int serialNumber);
    private native double secondValue(int serialNumber);
    private native int minuteNumber(int serialNumber);
    private native double minuteValue(int serialNumber);
    private native int hour12Number(int serialNumber);
    private native double hour12Value(int serialNumber);
    private native int hour24Number(int serialNumber);
    private native double hour24Value(int serialNumber);
    private native int dayNumber(int serialNumber);
    private native double dayValue(int serialNumber);
    private native int monthNumber(int serialNumber);
    private native double monthValue(int serialNumber);
    private native int yearNumber(int serialNumber);
    private native int eraNumber(int serialNumber);
    private native int weekdayNumber(int serialNumber);
    private native double weekdayValue(int serialNumber);
    private native int weekdayNumberAsCalendarColumn(int serialNumber,
                                                     int weekdayStart);
    private native double year366IndicatorFraction(int serialNumber);
    private native int dayOfYearNumber(int serialNumber);
    private native int weekOfYearNumber(int serialNumbewr,
                                        int  weekStartDay,  // weekStartDay == 0 means weeks start on Sunday
                                        boolean useISO8601);
    private native boolean isDST(int serialNumber);
}
