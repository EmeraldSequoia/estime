//
//  ESFakeTimeDriver.cpp
//
//  Created by Steven Pucci 29 Apr 2012
//  Copyright Emerald Sequoia LLC 2012. All rights reserved.
//

#include "ESTime.hpp"
#include "ESUtil.hpp"
#include "ESTimer.hpp"

#include "ESFakeTimeDriver.hpp"
#include "ESSystemTimeBase.hpp"

class DelayedFakeTimeSetter : public ESTimerObserver {
  public:
                            DelayedFakeTimeSetter(ESFakeTimeDriver *driver)
    :   _driver(driver)
    {
    }
    void                    notify(ESTimer *) {
        _driver->setupFakeTime();
    }

  private:
    ESFakeTimeDriver *_driver;
};

ESFakeTimeDriver::ESFakeTimeDriver(ESTimeInterval fakeTimeForSync,
                                   ESTimeInterval fakeTimeError)
:   _fakeTime(fakeTimeForSync),
    _fakeError(fakeTimeError)
{
    
    ESIntervalTimer *timer = new ESIntervalTimer(new DelayedFakeTimeSetter(this), 0.5/*interval*/);
    timer->activate();
}

void
ESFakeTimeDriver::setupFakeTime() {
    ESTimeInterval contTime = ESSystemTimeBase::currentContinuousSystemTime();
    ESTimeInterval delta = _fakeTime - contTime;
    setContSkew(delta, _fakeError);
}

// ESTimeSourceDriver redefines
/*virtual*/ void 
ESFakeTimeDriver::resync(bool userRequested) {
    if (userRequested) {
        setupFakeTime();
    }
    // Otherwise do nothing
}
 
 /*virtual*/ void 
ESFakeTimeDriver::ESFakeTimeDriver::stopSyncing() {
     // Do nothing
}
 
/*virtual*/ void 
ESFakeTimeDriver::enableSync() {
    // Do nothing
}

/*virtual*/ void 
ESFakeTimeDriver::disableSync() {
    // Do nothing
}

/*virtual*/ std::string 
ESFakeTimeDriver::userName() {
    return "Fake";
}

