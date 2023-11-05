//
//  ESTimeSourceDriver.hpp
//
//  Created by Steve Pucci 14 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESTIMESOURCEDIRVER_HPP
#define _ESTIMESOURCEDIRVER_HPP

#include "ESTime.hpp"

#include <string>

// Forward decl;
class ESTimeSourceDriver;

/*! A time source driver is an abstract class which represents a time source
    which can be used by an application desiring an accurate time.  Possible
    implementations of this class include ESNTPDriver, ESGPSTimeDriver, and
    the degenerate ESSystemTimeDriver, which simply reports the system time
    on the assumption that the system time is synchronized with NTP.

    N.B.:  A system time source 
*/
class ESTimeSourceDriver {
  public:
    ESTimeInterval          contSkew()         // Delta of currentTime() - ESSystemTimeBase::currentContinuousSystemTime()
    { return _contSkew; }

    float                   currentTimeError()   // How confident are we in the currentTime() absolute value
    { return _currentTimeError; }

    ESTimeSourceStatus      currentStatus()
    { return _status; }

    virtual void            resync(bool userRequested) = 0;
    virtual void            disableSync() = 0;
    virtual void            enableSync() = 0;
    virtual void            stopSyncing() = 0;
    virtual void            setDeviceCountryCode(const char *countryCode) { }
    virtual std::string     userName() = 0;  // "NTP", "XGPS150", "Fake", "Device", etc
    virtual void            release() { delete this; }

// Methods to be called by ESTime
                            ESTimeSourceDriver();
    virtual                 ~ESTimeSourceDriver();

    void                    notifyContinuousOffsetChange();

// Methods to be called by implementation class internals
  protected:
    void                    setContSkew(ESTimeInterval newContSkew,
                                        ESTimeInterval currentTimeError);

    void                    setStatus(ESTimeSourceStatus status);

    ESTimeInterval          _contSkew;
    ESTimeInterval          _currentTimeError;
    ESTimeSourceStatus      _status;
};

#endif // _ESTIMESOURCEDIRVER_HPP
