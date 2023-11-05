//
//  ESTimeEnvironment.hpp
//  Emerald Sequoia LLC
//
//  Created by Steve Pucci 5/2011 based on ECWatchEnvironment in chronometer.
//  Copyright Emerald Sequoia LLC 2008-2011. All rights reserved.
//

#ifndef _ESTIMEENVIRONMENT_HPP_
#define _ESTIMEENVIRONMENT_HPP_

class ESWatchTime;
class ESTimeZone;
class ESTimeLocEnvironment;
class ESTimeLocAstroEnvironment;

#include "ESCalendar.hpp"  // For opaque ESTimeZone

#include <string>

// *****************************************************************
// ESTimeEnvironment
//
// The ECWatchEnvironment used by Chronometer was split into
// multiple parts.  This part (ESTimeEnvironment) records merely the timezone
// and associated caches, so that ESWatchTime can be implemented in
// terms of it.
//
// Built on this class is ESTimeLocEnvironment, which adds to this
// class's functionality location information and caching, and
// ESTimeLocAstroEnvironment, which additionally includes an astro mgr.
// *****************************************************************

class ESTimeEnvironment {
  public:
                            ESTimeEnvironment(const char *timeZoneName,
                                              bool       observingIPhoneTime);
    virtual                 ~ESTimeEnvironment();

    ESTimeZone              *estz() const { return _estz; }

    ESTimeInterval          midnightForTimeInterval(ESTimeInterval timeInterval);

    void                    setTimeZone(ESTimeZone *estz);
    void                    clearCache();

    std::string             timeZoneName();
    static void             lockForBGTZAccess();
    static void             unlockForBGTZAccess();

    virtual void            setupLocalEnvironmentForThreadFromActionButton(bool        fromActionButton,
                                                                           ESWatchTime *watchTime) {}  // redefined in ESTimeLocAstroEnvironment
    virtual void            cleanupLocalEnvironmentForThreadFromActionButton(bool fromActionButton) {} // redefined in ESTimeLocAstroEnvironment

    virtual bool            isLocationEnv() const { return false; }
    virtual bool            isAstroEnv() const { return false; }

    // Convenience cast-with-assert methods
    ESTimeLocEnvironment      *asLocationEnv();  // Defined in ESTimeLocEnvironmentInl.hpp, in the eslocation library; don't call this without the location library available
    ESTimeLocAstroEnvironment *asAstroEnv();     // Defined in ESTimeLocAstroEnvironmentInl.hpp, in the esastro library; don't call this without the astro library available

  protected:
    ESTimeZone              *timeZoneWhenNoneSpecified();
    void                    handleNewTimeZone();

    // The actual data
    ESTimeZone              *_estz;

    // Caches
    ESTimeInterval          _prevTimeForTZ;	    // a really really simple cache for tzOffset
    ESTimeInterval          _resultForTZ;	    // a really really simple cache for tzOffset
    ESTimeInterval          _cacheMidnightBase;        // a kind of simple cache for midnightForDateInterval
    ESTimeInterval          _cacheDSTEvent;        // a kind of simple cache for midnightForDateInterval
    ESTimeInterval          _cacheDSTDelta;        // a kind of simple cache for midnightForDateInterval

    bool                    _observingIPhoneTime;
    bool                    _timeZoneIsDefault;
friend class ESWatchTime;
};

#endif // _ESTIMEENVIRONMENT_HPP_
