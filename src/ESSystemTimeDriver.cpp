//
//  ESSystemTimeDriver.cpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTime.hpp"
#include "ESSystemTimeDriver.hpp"

ESSystemTimeDriver::ESSystemTimeDriver() {
    // Do nothing except initializing the constants
    _contSkew = 0;
    _currentTimeError = 1e9;  // We have no idea what the error is, unless we can get the system to tell us.
                              // Maybe we can.  But for now, we'll just say it's "unknown".
    _status = ESTimeSourceStatusOff;
}

// Methods to be called by app delegate
/*static*/ ESTimeSourceDriver *
ESSystemTimeDriver::makeOne() {   // This method can be used as an ESTimeSourceDriverMaker*
    return new ESSystemTimeDriver;
}

void 
ESSystemTimeDriver::resync(bool userRequested) {
    // Do nothing
}

void 
ESSystemTimeDriver::stopSyncing() {
    // Do nothing
}

void 
ESSystemTimeDriver::disableSync() {
    // Do nothing
}

void 
ESSystemTimeDriver::enableSync() {
    // Do nothing
}

/*virtual*/ std::string 
ESSystemTimeDriver::userName() {
    return "Device";
}

