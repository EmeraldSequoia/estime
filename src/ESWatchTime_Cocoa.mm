//
//  ESWatchTime_Cocoa.cpp
//
//  Created by Steven Pucci 27 Aug 2012
//  Copyright Emerald Sequoia LLC 2012. All rights reserved.
//

#include "ESWatchTime.hpp"

NSDate *
ESWatchTime::currentDate() {
    return [NSDate dateWithTimeIntervalSinceReferenceDate:currentTime()];
}


