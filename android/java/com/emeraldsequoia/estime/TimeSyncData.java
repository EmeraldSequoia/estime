//
//  TimeSyncData.java
//
//  Created by Steve Pucci 02 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

package com.emeraldsequoia.estime;

import java.util.Date;

public class TimeSyncData {
    public static native TimeSyncData get();

    private TimeSyncData(boolean avail, double skew, double acc, double cskew, double coff, Date boot, double susp, Date sync, Date attempt) {
        ntpAvailable = avail;
        ntpSkewSeconds = skew;
        ntpAccuracySeconds = acc;
        continuousSkewSeconds = cskew;
        continuousOffsetSeconds = coff;
        bootTimeAsSystemTime = boot;
        timeSuspendedSinceBootSeconds = susp;
        ntpSyncDate = sync;
        lastAttemptDate = attempt;
    }
    
    public boolean ntpAvailable;
    public double ntpSkewSeconds;
    public double ntpAccuracySeconds;
    public double continuousSkewSeconds;
    public double continuousOffsetSeconds;        // exact (changed only by OS at cell-tower etc changes)
    public Date bootTimeAsSystemTime;             // fixed
    public double timeSuspendedSinceBootSeconds;  // fixed except when suspended
    public Date ntpSyncDate;
    public Date lastAttemptDate;
}
