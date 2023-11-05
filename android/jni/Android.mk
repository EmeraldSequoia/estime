LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := estime
LOCAL_CFLAGS    := -Werror -DES_ANDROID
LOCAL_SRC_FILES := \
../../src/ESCalendar.cpp \
../../src/ESCalendar_simpleTZ.cpp \
../../src/ESCalendar_android.cpp \
../../src/ESLeapSecond.cpp \
../../src/ESNTPDriver.cpp \
../../src/ESNTPDriver_android.cpp \
../../src/ESNTPHostNames.cpp \
../../src/ESPoolHostSurvey.cpp \
../../src/ESSystemTimeBase_dualBase.cpp \
../../src/ESSystemTimeBase_android.cpp \
../../src/ESTime.cpp \
../../src/ESTimeEnvironment.cpp \
../../src/ESTimer.cpp \
../../src/ESTimeSourceDriver.cpp \
../../src/ESWatchTime.cpp \

# Leave a blank line before this one
LOCAL_LDLIBS    := -llog
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../src ../../deps/esutil/src

include $(BUILD_STATIC_LIBRARY)
