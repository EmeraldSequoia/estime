//
//  ESSystemTimeDriver.hpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESSYSTEMTIMEDRIVER_HPP_
#define _ESSYSTEMTIMEDRIVER_HPP_

#include "ESTimeSourceDriver.hpp"

/*! A trivial implementation of ESTimeSourceDriver which simply returns the system time as if
    it were 100% accurate all of the time.  

    NOTE Steve 2017/07/01:  THIS DOESN'T WORK.  

    It is not possible to implement this class in such a way as to satisfy both
    the constraints on continuous time (that it is monotonic and steadily
    increasing) AND the constraints on wall-clock ("system") time (that it is
    what the OS thinks the time is).  It is currently set up to do the former
    and not the latter (that is, it will track continuously, but won't
    necessarily reflect the latest system time).  It is also, I believe, broken
    if the 'alt' time is CLOCK_MONOTONIC, because that clock can be suspended
    when the system is, and we don't seem to properly recover from it (empirical
    evidence only; I don't understand why we don't recover properly on every
    wake).

    NOTE Steve 2017/07/16:  Well, it *can* "work" if you provide a single_timebase
    which uses the realtime (system) time for both actual and continuous, and then
    use this class (with a contSkew hardcoded to zero, as this does).  It won't
    handle continuous time properly, but that might not matter, particularly if
    you don't care about NTP anyway and you can't sync across sleeps.

 */
class ESSystemTimeDriver : public ESTimeSourceDriver {
  public:
                            ESSystemTimeDriver();
    virtual void            resync(bool userRequested);
    virtual void            stopSyncing();
    virtual void            disableSync();
    virtual void            enableSync();
    virtual std::string     userName();

    static ESTimeSourceDriver *makeOne();   // This method can be used as an ESTimeSourceDriverMaker*
};

#endif  // _ESSYSTEMTIMEDRIVER_HPP_
