//
//  ESTime_android.cpp
//
//  Created by Steve Pucci 02 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

#include "com_emeraldsequoia_estime_LeapSecondAwareDate.h"
#include "com_emeraldsequoia_estime_TimeSyncData.h"

#include "ESTime.hpp"
#include "ESSystemTimeBase.hpp"
#include "ESNTPDriver.hpp"
#include "ESJNI.hpp"
#include "ESJNIDefs.hpp"

#include <math.h>

/*
 * Class:     com_emeraldsequoia_estime_LeapSecondAwareDate
 * Method:    get
 * Signature: ()Lcom/emeraldsequoia/estime/LeapSecondAwareDate;
 */
JNIEXPORT jobject JNICALL Java_com_emeraldsequoia_estime_LeapSecondAwareDate_get
(JNIEnv *jniEnv, jclass) {
    double liveLeapSecondCorrection;
    jlong currentTimeMillis = UNIX_TIME_FROM_ESTIME(ESTime::currentTimeWithLiveLeapSecondCorrection(&liveLeapSecondCorrection));
    ESJNI_java_util_Date date = ESJNI_java_util_Date::CreateJavaObject(jniEnv, currentTimeMillis);
    ESJNI_com_emeraldsequoia_estime_LeapSecondAwareDate leapAwareDate = 
        ESJNI_com_emeraldsequoia_estime_LeapSecondAwareDate::CreateJavaObject(jniEnv, date.toJObject(), liveLeapSecondCorrection);
    date.DeleteLocalRef(jniEnv);
    return leapAwareDate.toJObject();
}

/*
 * Class:     com_emeraldsequoia_estime_TimeSyncData
 * Method:    get
 * Signature: ()Lcom/emeraldsequoia/estime/TimeSyncData;
 */
JNIEXPORT jobject JNICALL Java_com_emeraldsequoia_estime_TimeSyncData_get
(JNIEnv *jniEnv, jclass) {
    ESJBool ntpAvailable(ESNTPDriver::isRunning(), true);
    ESTimeInterval ntpSkew = ESTime::skewForReportingPurposesOnly();
    ESTimeInterval ntpAccuracySeconds = ESTime::skewAccuracy();
    ESTimeInterval continuousSkewSeconds = ESTime::continuousSkewForReportingPurposesOnly();
    ESTimeInterval continuousOffsetSeconds = ESTime::continuousOffset();
    ESTimeInterval bootTimeAsSystemTime;
    ESTimeInterval timeSuspendedSinceBootSeconds;
    ESSystemTimeBase::getTimesSinceBoot(&bootTimeAsSystemTime, &timeSuspendedSinceBootSeconds);
    jlong bootTimeMillis = UNIX_TIME_FROM_ESTIME(bootTimeAsSystemTime);
    ESJNI_java_util_Date bootTimeDate = ESJNI_java_util_Date::CreateJavaObject(jniEnv, bootTimeMillis);
    jlong syncTimeMillis = UNIX_TIME_FROM_ESTIME(ESNTPDriver::timeOfLastSuccessfulSync());
    ESJNI_java_util_Date syncDate = ESJNI_java_util_Date::CreateJavaObject(jniEnv, syncTimeMillis);
    jlong attemptTimeMillis = UNIX_TIME_FROM_ESTIME(ESNTPDriver::timeOfLastSyncAttempt());
    ESJNI_java_util_Date lastAttemptDate = ESJNI_java_util_Date::CreateJavaObject(jniEnv, attemptTimeMillis);
    ESJNI_com_emeraldsequoia_estime_TimeSyncData timeSyncData = 
        ESJNI_com_emeraldsequoia_estime_TimeSyncData::CreateJavaObject(jniEnv, ntpAvailable, ntpSkew, ntpAccuracySeconds, 
                                                                       continuousSkewSeconds, continuousOffsetSeconds,
                                                                       bootTimeDate.toJObject(), timeSuspendedSinceBootSeconds,
                                                                       syncDate.toJObject(), lastAttemptDate.toJObject());
    syncDate.DeleteLocalRef(jniEnv);
    lastAttemptDate.DeleteLocalRef(jniEnv);
    bootTimeDate.DeleteLocalRef(jniEnv);
    return timeSyncData.toJObject();
}

