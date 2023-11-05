//
//  ESFakeTimeDriver.hpp
//
//  Created by Steven Pucci 29 Apr 2012
//  Copyright Emerald Sequoia LLC 2012. All rights reserved.
//

#ifndef _ESFAKETIMEDRIVER_HPP_
#define _ESFAKETIMEDRIVER_HPP_

#include "ESTimeSourceDriver.hpp"

/** A time source driver which, at synchronization, sets the clock to a specified fake
 *  time, regardless of the actual curent time. */
class ESFakeTimeDriver : public ESTimeSourceDriver {
  public:
                            ESFakeTimeDriver(ESTimeInterval fakeTimeForSync,
                                             ESTimeInterval fakeTimeError);

    // ESTimeSourceDriver redefines
    virtual void            resync(bool userRequested);
    virtual void            stopSyncing();
    virtual void            enableSync();
    virtual void            disableSync();
    virtual std::string     userName();

  private:
    void                    setupFakeTime();

    ESTimeInterval          _fakeTime;
    ESTimeInterval          _fakeError;

friend class DelayedFakeTimeSetter;
};

#endif  // _ESFAKETIMEDRIVER_HPP_
