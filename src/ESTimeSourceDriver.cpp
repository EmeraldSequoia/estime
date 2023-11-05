//
//  ESTimeSourceDriver.cpp
//
//  Created by Steve Pucci 23 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTime.hpp"
#include "ESUtil.hpp"
#include "ESUserPrefs.hpp"
#include "ESErrorReporter.hpp"

static void
initDefaultPrefs() {
    ESUserPrefs::initDefaultPref("timeSkew", 0.0);
}

ESTimeSourceDriver::ESTimeSourceDriver() {
    initDefaultPrefs();  // unnecessary if this is not at startup, but doesn't matter

    _status = ESTimeSourceStatusOff;
    if (ESSystemTimeBase::isSingleTimeBase()) {
        ESErrorReporter::logOffline("Single time base start, zeroing everything");
        _contSkew = 0;
        _currentTimeError = 1e9;
    } else {
        if (ESSystemTimeBase::continuousTimeStableAtStart()) {
            _contSkew = ESUserPrefs::doublePref("contSkew");
            _currentTimeError = ESUserPrefs::doublePref("timeSkewAccuracy");
            if (_currentTimeError == 0) {
                _currentTimeError = 1e9;
            }
            ESErrorReporter::logOffline(ESUtil::stringWithFormat("NTP start, contTime stable, contSkew %.3f ±%.3f, skew %.3f", 
                                                                 _contSkew, 
                                                                 _currentTimeError,
                                                                 _contSkew + ESSystemTimeBase::continuousOffset()));
        } else {
            _contSkew = ESUserPrefs::doublePref("timeSkew") - ESSystemTimeBase::continuousOffset();
            _currentTimeError = 1e9;
            ESErrorReporter::logOffline(ESUtil::stringWithFormat("NTP start, contTime NOT stable, contSkew %.3f ±%.3f, skew %.3f", 
                                                                 _contSkew,
                                                                 _currentTimeError,
                                                                 _contSkew + ESSystemTimeBase::continuousOffset()));
        }
    }
}

/*virtual*/ 
ESTimeSourceDriver::~ESTimeSourceDriver() {
    // Do nothing, but this needs to be defined for derived classes to (implicitly) call
}


void 
ESTimeSourceDriver::setContSkew(ESTimeInterval newContSkew,
                                ESTimeInterval currentTimeError) {
    if (newContSkew != _contSkew ||
        currentTimeError != _currentTimeError) {
        _contSkew = newContSkew;
        _currentTimeError = currentTimeError;
        ESUserPrefs::setPref("timeSkew", _contSkew + ESSystemTimeBase::continuousOffset());
        ESUserPrefs::setPref("timeSkewAccuracy", _currentTimeError);
        ESUserPrefs::setPref("contSkew", _contSkew);
        ESTime::syncValueChangedByDriver(this);
        ESErrorReporter::logOffline(ESUtil::stringWithFormat("SYNC contSkew %.3f ±%.3f, skew %.3f", ESTime::continuousSkewForReportingPurposesOnly(),
                                                             ESTime::skewAccuracy(), ESTime::skewForReportingPurposesOnly()));
    }
}

void 
ESTimeSourceDriver::setStatus(ESTimeSourceStatus status) {
    if (status != _status) {
        _status = status;
        ESTime::syncStatusChangedByDriver(this);
    }
}

void 
ESTimeSourceDriver::notifyContinuousOffsetChange() {
    // In the triple base scheme, continuousAltOffset never changes, so no need to store contSkew here.
    ESUserPrefs::setPref("timeSkew", _contSkew + ESSystemTimeBase::continuousOffset());
}
