//
//  ESWatchTime_android.cpp
//
//  Created by Steve Pucci 02 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

#include "ESPlatform.h"
#include "ESErrorReporter.hpp"
#include "ESJNI.hpp"
#include "ESCalendar.hpp"

#include "ESWatchTime.hpp"
#include "ESTimeEnvironment.hpp"

#include <map>

#include "com_emeraldsequoia_estime_WatchTime.h"

class TimeAndEnv {
  public:
                            TimeAndEnv()
    :   env(ESCalendar_localTimeZoneName().c_str(), true/*observingIPhoneTime*/)
    {}

    ESWatchTime             watchTime;
    ESTimeEnvironment       env;
};

std::map<int, TimeAndEnv*> timeAndEnvsBySerialNumber;

static int lastUsedSerialNumber = 0;

static TimeAndEnv *findTimeAndEnv(int serialNumber) {
    std::map<int, TimeAndEnv *>::iterator it = timeAndEnvsBySerialNumber.find(serialNumber);
    if (it == timeAndEnvsBySerialNumber.end()) {
        return NULL;
    }
    return it->second;
}

static TimeAndEnv *findAndRemoveTimeAndEnv(int serialNumber) {
    std::map<int, TimeAndEnv *>::iterator it = timeAndEnvsBySerialNumber.find(serialNumber);
    if (it == timeAndEnvsBySerialNumber.end()) {
        return NULL;
    }
    TimeAndEnv *timeAndEnv = it->second;
    timeAndEnvsBySerialNumber.erase(it);
    return timeAndEnv;
}

static void addTimeAndEnv(int serialNumber, TimeAndEnv *theTimeAndEnv) {
    ESAssert(!findTimeAndEnv(serialNumber));
    timeAndEnvsBySerialNumber.insert(std::map<int, TimeAndEnv *>::value_type(serialNumber, theTimeAndEnv));
}

extern "C" {

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    onCreate
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_onCreate
(JNIEnv *, jobject) {
    TimeAndEnv *timeAndEnv = new TimeAndEnv();
    int serialNumber = ++lastUsedSerialNumber;
    addTimeAndEnv(serialNumber, timeAndEnv);
    return serialNumber;
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    onDestroy
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_emeraldsequoia_estime_WatchTime_onDestroy
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findAndRemoveTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        delete timeAndEnv;
    } else {
        ESErrorReporter::logError("WatchTime_onDestroy", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    latchTimeForBeatsPerSecond
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_com_emeraldsequoia_estime_WatchTime_latchTimeForBeatsPerSecond
(JNIEnv *jniEnv, jobject, jint jSerialNumber, jint jBeatsPerSecond) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        timeAndEnv->watchTime.latchTimeForBeatsPerSecond((int)jBeatsPerSecond);
    } else {
        ESErrorReporter::logError("WatchTime_latchTimeForBeatsPerSecond", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    unlatchTime
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_emeraldsequoia_estime_WatchTime_unlatchTime
(JNIEnv *jniEnv, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        timeAndEnv->watchTime.unlatchTime();
    } else {
        ESErrorReporter::logError("WatchTime_unlatchTime", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    secondNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_secondNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.secondNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_secondNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    secondValue
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_secondValue
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.secondValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_secondValue", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    minuteNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_minuteNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.minuteNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_minuteNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    minuteValue
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_minuteValue
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.minuteValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_minuteValue", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    hour12Number
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_hour12Number
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.hour12NumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_hour12Number", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    hour12Value
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_hour12Value
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.hour12ValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_hour12Value", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    hour24Number
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_hour24Number
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.hour24NumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_hour24Number", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    hour24Value
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_hour24Value
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.hour24ValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_hour24Value", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    dayNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_dayNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.dayNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_dayNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    dayValue
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_dayValue
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.dayValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_dayValue", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    monthNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_monthNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.monthNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_monthNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    monthValue
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_monthValue
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.monthValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_monthValue", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    yearNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_yearNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.yearNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_yearNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    eraNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_eraNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.eraNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_eraNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    weekdayNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_weekdayNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.weekdayNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_weekdayNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    weekdayValue
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_weekdayValue
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.weekdayValueUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_weekdayValue", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    weekdayNumberAsCalendarColumn
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_weekdayNumberAsCalendarColumn
(JNIEnv *, jobject, jint jSerialNumber, jint weekdayStart) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.weekdayNumberAsCalendarColumnUsingEnv(&timeAndEnv->env, weekdayStart);
    } else {
        ESErrorReporter::logError("WatchTime_weekdayNumberAsCalendarColumnUsingEnv", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    year366IndicatorFraction
 * Signature: (I)D
 */
JNIEXPORT jdouble JNICALL Java_com_emeraldsequoia_estime_WatchTime_year366IndicatorFraction
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.year366IndicatorFractionUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_year366IndicatorFraction", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    dayOfYearNumber
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_dayOfYearNumber
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.dayOfYearNumberUsingEnv(&timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_dayOfYearNumber", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    weekOfYearNumber
 * Signature: (IIZ)I
 */
JNIEXPORT jint JNICALL Java_com_emeraldsequoia_estime_WatchTime_weekOfYearNumberUsingEnv
(JNIEnv *, jobject, jint jSerialNumber, jint jWeekStartDay, jboolean jUseISO8601) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return timeAndEnv->watchTime.weekOfYearNumberUsingEnv((int)jWeekStartDay, ESJBool(jUseISO8601).toBool(), &timeAndEnv->env);
    } else {
        ESErrorReporter::logError("WatchTime_weekOfYearNumberUsingEnv", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return 0;
    }
}

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    isDST
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL Java_com_emeraldsequoia_estime_WatchTime_isDST
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        return ESJBool(timeAndEnv->watchTime.isDSTUsingEnv(&timeAndEnv->env), true /*isBool*/).toJBool();
    } else {
        ESErrorReporter::logError("WatchTime_isDST", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
        return JNI_FALSE;
    }
}

}  // extern "C"

/*
 * Class:     com_emeraldsequoia_estime_WatchTime
 * Method:    latchTimeOnMinuteBoundary
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_emeraldsequoia_estime_WatchTime_latchTimeOnMinuteBoundary
(JNIEnv *, jobject, jint jSerialNumber) {
    TimeAndEnv *timeAndEnv = findTimeAndEnv((int)jSerialNumber);
    if (timeAndEnv) {
        timeAndEnv->watchTime.latchTimeOnMinuteBoundary();
    } else {
        ESErrorReporter::logError("WatchTime_latchTimeOnMinuteBoundary", "Didn't find TimeAndEnv with serialNumber %d", (int)jSerialNumber);
    }
}

