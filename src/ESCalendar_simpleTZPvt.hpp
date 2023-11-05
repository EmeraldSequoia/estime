//
//  ESCalendar_simpleTZPvt.hpp
//
//  Created by Steve Pucci 14 Feb 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESCALENDAR_SIMPLETZPVT_HPP_
#define _ESCALENDAR_SIMPLETZPVT_HPP_

#if ES_ANDROID
#include "jni.h"
#endif

#include "string"

/*! "Abstract" class, not truly abstract but implemented differently
 *  on different platforms, representing the ability to return the
 *  offset from UTC for a given UTC. */
class ESTimeZoneImplementation {
  public:
                            ESTimeZoneImplementation(const char *olsonID);
                            ~ESTimeZoneImplementation();
    static std::string      olsonNameOfLocalTimeZone();
    std::string             name();
    ESTimeInterval          offsetFromUTCForTime(ESTimeInterval utc);
    ESTimeInterval          nextDSTChangeAfterTime(ESTimeInterval dt, ESTimeZone *estz);
    std::string             abbrevName(ESTimeZone *estz);
    bool                    isDSTAtTime(ESTimeInterval dt);
  private:
#if ES_ANDROID
    jobject                 _javaTZ;
#endif
};

#endif  // _ESCALENDAR_SIMPLETZPVT_HPP_
