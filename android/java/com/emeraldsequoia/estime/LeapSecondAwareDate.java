//
//  LeapSecondAwareDate.java
//
//  Created by Steve Pucci 02 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

package com.emeraldsequoia.estime;

import java.util.Date;

public class LeapSecondAwareDate {
    public static native LeapSecondAwareDate get();

    private LeapSecondAwareDate(Date d, double l) {
        date = d;
        liveLeapCorrection = l;
    }

    public Date date;
    public double liveLeapCorrection;
}
