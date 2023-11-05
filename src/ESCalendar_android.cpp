//
//  ESCalendar_android.cpp
//
//  Created by Steve Pucci 14 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESUtil.hpp"
#include "ESCalendar.hpp"
#include "ESCalendarPvt.hpp"
#include "ESCalendar_simpleTZPvt.hpp"
#include "ESSystemTimeBase.hpp"
#include "ESErrorReporter.hpp"
#include "ESJNI.hpp"

#include "jni.h"

#include <assert.h>

static bool methodIDsInitialized = false;

static jclass TimeZone_class = NULL;
//static jclass Date_class = NULL;
static jmethodID getDefaultMethod = NULL;
static jmethodID getTimeZoneMethod = NULL;
static jmethodID getOffsetMethod = NULL;
static jmethodID getRawOffsetMethod = NULL;
static jmethodID getIDMethod = NULL;
//static jmethodID inDaylightTimeMethod = NULL;
//static jmethodID dateCtorMethod = NULL;

static void initializeMethodIDs(JNIEnv *jniEnv) {
    assert(jniEnv);  // Make sure ESUtil::init is called first
    TimeZone_class = (jclass)jniEnv->NewGlobalRef(jniEnv->FindClass("java/util/TimeZone"));
    assert(TimeZone_class);
    //Date_class = (jclass)jniEnv->NewGlobalRef(jniEnv->FindClass("java/util/Date"));
    //assert(Date_class);
    getTimeZoneMethod = jniEnv->GetStaticMethodID(TimeZone_class, "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
    assert(getTimeZoneMethod);
    getDefaultMethod = jniEnv->GetStaticMethodID(TimeZone_class, "getDefault", "()Ljava/util/TimeZone;");
    assert(getDefaultMethod);
    getIDMethod = jniEnv->GetMethodID(TimeZone_class, "getID", "()Ljava/lang/String;");
    assert(getIDMethod);
    getOffsetMethod = jniEnv->GetMethodID(TimeZone_class, "getOffset", "(J)I");
    assert(getOffsetMethod);
    getRawOffsetMethod = jniEnv->GetMethodID(TimeZone_class, "getRawOffset", "()I");
    assert(getRawOffsetMethod);
    //inDaylightTimeMethod = jniEnv->GetMethodID(TimeZone_class, "inDaylightTime", "(Ljava/util/Date)Z");
    //assert(inDaylightTimeMethod);
    //dateCtorMethod = jniEnv->GetMethodID(Date_class, "<init>", "(J)V");
    //assert(dateCtorMethod);

    methodIDsInitialized = true;
}

ESTimeZoneImplementation::ESTimeZoneImplementation(const char *olsonID) {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    if (!methodIDsInitialized) {
        initializeMethodIDs(jniEnv);
    }
    assert(jniEnv);  // Make sure ESUtil::init is called first
    // ESErrorReporter::logInfo("ESTZImpl ctor", "olson id %s", olsonID);
    jstring str = jniEnv->NewStringUTF(olsonID);
    assert(str);
    jobject obj = jniEnv->CallStaticObjectMethod(TimeZone_class, getTimeZoneMethod, str);
    assert(obj);
    jniEnv->DeleteLocalRef(str);
    _javaTZ = jniEnv->NewGlobalRef(obj);
    jniEnv->DeleteLocalRef(obj);
}

 
ESTimeZoneImplementation::~ESTimeZoneImplementation() {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    assert(jniEnv);  // Make sure ESUtil::init is called first
    jniEnv->DeleteGlobalRef(_javaTZ);
    _javaTZ = NULL;
}

/*static*/ std::string
ESTimeZoneImplementation::olsonNameOfLocalTimeZone() {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    assert(jniEnv);  // Make sure ESUtil::init is called first
    if (!methodIDsInitialized) {
        initializeMethodIDs(jniEnv);
    }
    jobject defaultTZ = jniEnv->CallStaticObjectMethod(TimeZone_class, getDefaultMethod);
    assert(defaultTZ);
    jstring str = (jstring)jniEnv->CallObjectMethod(defaultTZ, getIDMethod);
    assert(str);
    jniEnv->DeleteLocalRef(defaultTZ);
    const char *cstr = jniEnv->GetStringUTFChars(str, NULL);
    std::string s = cstr;
    jniEnv->ReleaseStringUTFChars(str, cstr);
    jniEnv->DeleteLocalRef(str);
    return s;
}

std::string
ESTimeZoneImplementation::name() {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    assert(jniEnv);  // Make sure ESUtil::init is called first
    jstring str = (jstring)jniEnv->CallObjectMethod(_javaTZ, getIDMethod);
    assert(str);
    const char *cstr = jniEnv->GetStringUTFChars(str, NULL);
    std::string s = cstr;
    jniEnv->ReleaseStringUTFChars(str, cstr);
    jniEnv->DeleteLocalRef(str);
    return s;
}

ESTimeInterval 
ESTimeZoneImplementation::offsetFromUTCForTime(ESTimeInterval utc) {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    assert(jniEnv);  // Make sure ESUtil::init is called first
    jlong javaTime = UNIX_TIME_FROM_ESTIME(utc);
    int offset = jniEnv->CallIntMethod(_javaTZ, getOffsetMethod, javaTime);
    return offset / 1000.0;
}

ESTimeInterval 
ESTimeZoneImplementation::nextDSTChangeAfterTime(ESTimeInterval dt, ESTimeZone *estz) {
    static bool messageEmitted = false;
    if (!messageEmitted) {
        ESErrorReporter::logError("ESTZ", "nextDSTChangeAfterTime() doing it the slow way; there's a better way in Android 'O'\n");
        messageEmitted = true;
    }
    return ESCalendar_nextDSTChangeAfterTimeIntervalTheSlowWay(estz, dt);
}

std::string
ESTimeZoneImplementation::abbrevName(ESTimeZone *estz) {
    JNIEnv *jniEnv = ESUtil::jniEnv();
    ESJNI_java_util_TimeZone jniTZ(_javaTZ);
    ESJNI_java_lang_String jtzName = jniTZ.getDisplayName(jniEnv);
    std::string tzName = jtzName.toESString(jniEnv);
    jtzName.DeleteLocalRef(jniEnv);
    return tzName;
}

ESTimeInterval
ESCalendar_tzOffsetAfterNextDSTChange(ESTimeZone     *estz,
                                      ESTimeInterval timeInterval) {
    ESTimeInterval tzOffsetNow = ESCalendar_tzOffsetForTimeInterval(estz, timeInterval);
    const int bimonthsOutToSearch = 18;
    int bimonth;
    ESTimeInterval tzOffsetThen;
    for (int bimonth = 0; bimonth < bimonthsOutToSearch; bimonth++) {
        ESTimeInterval tryInterval = timeInterval + bimonth * (60 * 60 * 24 * 30 * 2);
        tzOffsetThen = ESCalendar_tzOffsetForTimeInterval(estz, tryInterval);
        if (tzOffsetThen != tzOffsetNow) {
            break;
        }
    }
    return tzOffsetThen;
}

std::string
ESCalendar_version() {
    ESErrorReporter::logError("ESCalendar", "ESCalendar_version() unimplemented\n");
    return "android";
}

bool 
ESTimeZoneImplementation::isDSTAtTime(ESTimeInterval dt) {
    // We could use java::util::TimeZone::inDaylightTime here instead but that requires constructing
    // and then releasing a Date object, which I presume is more expensive than just calling two
    // methods which do not construct or destroy objects...
    JNIEnv *jniEnv = ESUtil::jniEnv();
    assert(jniEnv);  // Make sure ESUtil::init is called first
    jlong javaTime = UNIX_TIME_FROM_ESTIME(dt);
    int offsetNow = jniEnv->CallIntMethod(_javaTZ, getOffsetMethod, javaTime);
    int rawOffset = jniEnv->CallIntMethod(_javaTZ, getRawOffsetMethod);
    return (offsetNow != rawOffset);
}
