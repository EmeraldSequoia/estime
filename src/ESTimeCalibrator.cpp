//
//  ESTimeCalibrator.cpp
//
//  Created by Steve Pucci 06 Jul 2017
//  Copyright Emerald Sequoia LLC 2017. All rights reserved.
//

#include "ESTimeCalibrator.hpp"

#include "ESUtil.hpp"
#include "ESFile.hpp"
#include "ESSystemTimeBase.hpp"
#include "ESLinearRegression.hpp"

#include <fcntl.h>
#include <errno.h>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if ES_ANDROID_USE_NTP

static const char *CALIBRATION_DATA_PATH = "calibration_data.txt";

// Each data point describes the data at a particular time.
struct CalibrationDataPoint {
    ESTimeInterval          timestampForDebugging;  // Unused except to print; is in OS system time, uncorrected
    ESTimeInterval          contSkew;
    ESTimeInterval          skewError;
    ESTimeInterval          boottimeSeconds;
    ESTimeInterval          monotonicSeconds;
    ESTimeInterval          monotonicRawSeconds;
    bool                    reboot;  // Is this the first point after a reboot?
};

// TODO: Switch to forward_list once we go to C++11.
static std::list<CalibrationDataPoint> *calibrationData = NULL;

static bool nextDataPointIsReboot = true;
static bool initialized = false;

static std::string calibrationDataName() {
    return ESUtil::deviceID() + "-calibration_data.txt";
}

// Indexes for 2-variable regression
static const int NOT_SUSPENDED_INDEX = 0;
static const int SUSPENDED_INDEX = 1;

static void 
readCalibrationDataPoints() {
    size_t contentLength;
    char *fileContents = ESFile::getFileContentsInMallocdArray(calibrationDataName().c_str(), ESFilePathTypeRelativeToDocumentDir, 
                                                               true /* missingOK */, &contentLength);
    if (!fileContents) {
        ESErrorReporter::logInfo("ESTimeCalibrator", "No calibration data to read");
        return;
    }
    int pos = 0;
    int numPoints = 0;
    while (true) {
        CalibrationDataPoint dataPoint;
        int bytesRead;
        char trueFalseChar;
        // Try first with monotonic raw, then with monotonic if that doesn't work
        int st = sscanf(fileContents + pos, "%lf %lf %lf %lf %lf %lf %c\n%n", 
                        &dataPoint.timestampForDebugging,
                        &dataPoint.contSkew, &dataPoint.skewError, 
                        &dataPoint.boottimeSeconds, &dataPoint.monotonicSeconds, &dataPoint.monotonicRawSeconds, &trueFalseChar, &bytesRead);
        if (st != 7) {  // The %n doesn't count
            st = sscanf(fileContents + pos, "%lf %lf %lf %lf %lf %c\n%n", 
                        &dataPoint.timestampForDebugging,
                        &dataPoint.contSkew, &dataPoint.skewError, 
                        &dataPoint.boottimeSeconds, &dataPoint.monotonicSeconds, &trueFalseChar, &bytesRead);
            dataPoint.monotonicRawSeconds = dataPoint.monotonicSeconds;
        }
        if (st == 6) {  // The %n doesn't count
            dataPoint.reboot = (trueFalseChar == 'T');
            calibrationData->push_back(dataPoint);
            numPoints++;
            pos += bytesRead;
        } else {
            if (st != EOF) {
                ESErrorReporter::logError("ESTimeCalibrator", "Found an incomplete line with %d element(s)", st);
            }
            free(fileContents);
            ESErrorReporter::logError("ESTimeCalibrator", "Read %d calibration data point%s", numPoints, (numPoints == 1) ? "" : "s");
            return;
        }
    }
}

static bool 
appendCalibrationDataPoint(const CalibrationDataPoint &dataPoint) {
    calibrationData->push_back(dataPoint);
    char rebootChar = dataPoint.reboot ? 'T' : 'F';
    std::string s = 
        ESUtil::stringWithFormat("%.4f %.4f %.4f %.4f %.4f %.4f %c\n", 
                                 dataPoint.timestampForDebugging, dataPoint.contSkew, dataPoint.skewError, dataPoint.boottimeSeconds, dataPoint.monotonicSeconds,
                                 dataPoint.monotonicRawSeconds, rebootChar);
    int fd = open((ESFile::documentDirectory() + "/" + calibrationDataName()).c_str(),
                  O_CREAT|O_APPEND|O_WRONLY, 0777);
    if (fd < 0) {
        ESErrorReporter::logErrorWithCode("ESTimeCalibrator", errno, strerror_r, "Couldn't open calibration data file for appending");
        return false;
    }
    size_t writtenBytes = write(fd, s.c_str(), s.length());
    if (writtenBytes != s.length()) {
        ESErrorReporter::logErrorWithCode("ESTimeCalibrator", errno, strerror_r, 
                                          ESUtil::stringWithFormat("Only wrote %d of %d bytes to calibration data file", writtenBytes, s.length()).c_str());
        return false;
    }
    int st = close(fd);
    if (st != 0) {
        ESErrorReporter::logErrorWithCode("ESTimeCalibrator", errno, strerror_r, "Calibration data file close failed");
        return false;
    }
    ESErrorReporter::logError("ESTimeCalibrator", "Logged calibration data point: %s", s.c_str());
    return true;
}

static void
recalibrate() {
    // Find the number of intervals to be used as calibration data.  It's not quite N - 1, where N is the number of calibration data
    // points, because if there's a reboot in the middle, the interval across the reboot is not counted (because we have no data about
    // how the skew moved in that time).  So we make a first pass to determine how many intervals there are.
    int numIntervals = 0;
    std::list<CalibrationDataPoint>::iterator end = calibrationData->end();
    bool firstEntry = true;
    for (std::list<CalibrationDataPoint>::iterator iter = calibrationData->begin(); iter != end; iter++) {
        const CalibrationDataPoint &point = *iter;
        if (firstEntry) {
            firstEntry = false;
        } else {
            if (!point.reboot && point.skewError != 0) {
                numIntervals++;
            }
        }
    }
    if (numIntervals == 0) {
        return;  // Nothing to do (yet)
    }

    // OK, now we have numIntervals, so we can create the input arrays for the regression.
    // We'll do both a 2-variable regression and a 1-variable regression, and see which one is better.

    // The results are shared with both 1-variable and 2-variable regressions.
    double *results = new double[numIntervals];
    
    // For comparison, we also run with equal weighting.
    double *weights_identity = new double[numIntervals];

    // The 1-variable regression
    double *weights_1variable = new double[numIntervals];
    ESLinearRegression::Matrix x_1variable(1, numIntervals);
    int sampleNumber = 0;
    CalibrationDataPoint previousDataPoint;
    firstEntry = true;
    for (std::list<CalibrationDataPoint>::iterator iter = calibrationData->begin(); iter != end; iter++) {
        const CalibrationDataPoint &point = *iter;
        if (firstEntry) {
            firstEntry = false;
        } else {
            if (!point.reboot && point.skewError != 0) {
                // Set the observed result in its array.
                results[sampleNumber] = point.contSkew - previousDataPoint.contSkew;

                ESTimeInterval timeSinceLastInterval = point.boottimeSeconds - previousDataPoint.boottimeSeconds;
                // We weight each interval's data by the elapsed time (the longer the elapsed time, the
                // more confident we are in the delta) and by the inverse of the skew error (the smaller
                // the skew error, the more confident we are).
                weights_1variable[sampleNumber] = timeSinceLastInterval / point.skewError;

                weights_identity[sampleNumber] = 1.0;

                // The 1-variable regression just uses the time since the last skew as a predictor.
                x_1variable(0, sampleNumber) = timeSinceLastInterval;

                sampleNumber++;
            }
        }
        previousDataPoint = point;
    }
    ESLinearRegression linearRegression_1variable(results, x_1variable, weights_1variable);
    ESLinearRegression linearRegression_1variable_identityWeights(results, x_1variable, weights_identity);

    // The 2-variable regression
    double *weights_2variable = new double[numIntervals];
    ESLinearRegression::Matrix x_2variable(2, numIntervals);
    sampleNumber = 0;
    firstEntry = true;
    for (std::list<CalibrationDataPoint>::iterator iter = calibrationData->begin(); iter != end; iter++) {
        const CalibrationDataPoint &point = *iter;
        if (firstEntry) {
            firstEntry = false;
        } else {
            if (!point.reboot && point.skewError != 0) {
                ESTimeInterval timeSinceLastInterval = point.boottimeSeconds - previousDataPoint.boottimeSeconds;

                // The 2-variable regression uses time suspended and time not suspended
                ESTimeInterval timeNotSuspended = point.monotonicSeconds - previousDataPoint.monotonicSeconds;
                ESTimeInterval timeSuspended = timeSinceLastInterval - timeNotSuspended;

                // In the two-variable case, the weight is a function of how long both intervals are (non-suspended & suspended).
                // We take the minimum interval (as the one resulting in the least precision in the drift rate).
                weights_2variable[sampleNumber] = fmin(timeSuspended, timeNotSuspended) / (point.skewError + previousDataPoint.skewError);

                x_2variable(NOT_SUSPENDED_INDEX, sampleNumber) = timeNotSuspended;
                x_2variable(SUSPENDED_INDEX, sampleNumber) = timeSuspended;

                sampleNumber++;
            }
        }
        previousDataPoint = point;
    }
    ESLinearRegression linearRegression_2variable(results, x_2variable, weights_2variable);
    ESLinearRegression linearRegression_2variable_identityWeights(results, x_2variable, weights_identity);

    if (linearRegression_1variable.valid) {
        double driftSecondsPerSecond = linearRegression_1variable.C[0];
        double driftSecondsPerDay = driftSecondsPerSecond * 24 * 3600;
        ESErrorReporter::logInfo("ESTimeCalibrator", "1-variable regression on %d samples found drift of %.2f seconds per day, resulting skew stdev %.4f", 
                                 numIntervals, driftSecondsPerDay, linearRegression_1variable.SDV);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("1-variable regression on %d samples found drift of %.2f seconds per day, resulting skew stdev %.4f", 
        //                                                      numIntervals, driftSecondsPerDay, linearRegression_1variable.SDV));
    } else {
        ESErrorReporter::logError("ESTimeCalibrator", "1-variable regression invalid");
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("1-variable regression invalid"));
    }

    if (linearRegression_2variable.valid) {
        double driftSecondsNotSuspendedPerSecond = linearRegression_2variable.C[NOT_SUSPENDED_INDEX];
        double driftSecondsNotSuspendedPerDay = driftSecondsNotSuspendedPerSecond * 24 * 3600;
        ESErrorReporter::logInfo(
            "ESTimeCalibrator", "2-variable regression found drift (not suspended) of %7.2f seconds per day, resulting skew stdev %.4f", 
            driftSecondsNotSuspendedPerDay, linearRegression_2variable.SDV);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat(
        //                                 "2-variable regression found drift (not suspended) of %7.2f seconds per day, resulting skew stdev %.4f", 
        //                                 driftSecondsNotSuspendedPerDay, linearRegression_2variable.SDV));
        double driftSecondsSuspendedPerSecond = linearRegression_2variable.C[SUSPENDED_INDEX];
        double driftSecondsSuspendedPerDay = driftSecondsSuspendedPerSecond * 24 * 3600;
        ESErrorReporter::logInfo("ESTimeCalibrator", "2-variable regression found drift     (suspended) of %7.2f seconds per day", 
                                 driftSecondsSuspendedPerDay);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("2-variable regression found drift     (suspended) of %7.2f seconds per day", 
        //                                                      driftSecondsSuspendedPerDay));
    } else if (numIntervals < 2) {
        ESErrorReporter::logError("ESTimeCalibrator", "2-variable regression not possible (numIntervals=%d)", numIntervals);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("2-variable regression not possible (numIntervals=%d)", numIntervals));
    } else {
        ESErrorReporter::logError("ESTimeCalibrator", "2-variable regression invalid");
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("2-variable regression invalid"));
    }

    // Now the same analysis with the identity weights regressions.

    if (linearRegression_1variable_identityWeights.valid) {
        double driftSecondsPerSecond = linearRegression_1variable_identityWeights.C[0];
        double driftSecondsPerDay = driftSecondsPerSecond * 24 * 3600;
        ESErrorReporter::logInfo("ESTimeCalibrator", 
                                 "IDENTITY WEIGHTS 1-variable regression on %d samples found drift of %.2f seconds per day, resulting skew stdev %.4f", 
                                 numIntervals, driftSecondsPerDay, linearRegression_1variable_identityWeights.SDV);
        ESErrorReporter::logOffline(ESUtil::stringWithFormat(
                                        "IDENTITY WEIGHTS 1-variable regression on %d samples found drift of %.2f seconds per day, resulting skew stdev %.4f",
                                        numIntervals, driftSecondsPerDay, linearRegression_1variable_identityWeights.SDV));
    } else {
        ESErrorReporter::logError("ESTimeCalibrator", "IDENTITY WEIGHTS 1-variable regression invalid");
        ESErrorReporter::logOffline(ESUtil::stringWithFormat("IDENTITY WEIGHTS 1-variable regression invalid"));
    }

    if (linearRegression_2variable_identityWeights.valid) {
        double driftSecondsNotSuspendedPerSecond = linearRegression_2variable_identityWeights.C[NOT_SUSPENDED_INDEX];
        double driftSecondsNotSuspendedPerDay = driftSecondsNotSuspendedPerSecond * 24 * 3600;
        ESErrorReporter::logInfo(
            "ESTimeCalibrator", "IDENTITY WEIGHTS 2-variable regression found drift (not suspended) of %7.2f seconds per day, resulting skew stdev %.4f", 
            driftSecondsNotSuspendedPerDay, linearRegression_2variable_identityWeights.SDV);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat(
        //                                 "IDENTITY WEIGHTS 2-variable regression found drift (not suspended) of %7.2f seconds per day, resulting skew stdev %.4f", 
        //                                 driftSecondsNotSuspendedPerDay, linearRegression_2variable_identityWeights.SDV));
        double driftSecondsSuspendedPerSecond = linearRegression_2variable_identityWeights.C[SUSPENDED_INDEX];
        double driftSecondsSuspendedPerDay = driftSecondsSuspendedPerSecond * 24 * 3600;
        ESErrorReporter::logInfo("ESTimeCalibrator", "IDENTITY WEIGHTS 2-variable regression found drift     (suspended) of %7.2f seconds per day", 
                                 driftSecondsSuspendedPerDay);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("IDENTITY WEIGHTS 2-variable regression found drift     (suspended) of %7.2f seconds per day", 
        //                                                      driftSecondsSuspendedPerDay));
    } else if (numIntervals < 2) {
        ESErrorReporter::logError("ESTimeCalibrator", "IDENTITY WEIGHTS 2-variable regression not possible (numIntervals=%d)", numIntervals);
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("IDENTITY WEIGHTS 2-variable regression not possible (numIntervals=%d)", numIntervals));
    } else {
        ESErrorReporter::logError("ESTimeCalibrator", "IDENTITY WEIGHTS 2-variable regression invalid");
        // ESErrorReporter::logOffline(ESUtil::stringWithFormat("IDENTITY WEIGHTS 2-variable regression invalid"));
    }
}

/*static*/ void 
ESTimeCalibrator::init(bool rebootDetected) {
    calibrationData = new std::list<CalibrationDataPoint>;
    readCalibrationDataPoints();
    nextDataPointIsReboot = rebootDetected || calibrationData->empty();
    initialized = true;
}

/*static*/ void 
ESTimeCalibrator::notifySuccessfulSkew(ESTimeInterval contSkew, ESTimeInterval skewError) {
    ESAssert(initialized);
    CalibrationDataPoint dataPoint;
    dataPoint.contSkew = contSkew;
    dataPoint.skewError = skewError;
    dataPoint.reboot = nextDataPointIsReboot;
    ESSystemTimeBase::getTimes(&dataPoint.timestampForDebugging, &dataPoint.monotonicSeconds, &dataPoint.boottimeSeconds, &dataPoint.monotonicRawSeconds);
    if (appendCalibrationDataPoint(dataPoint)) {
        nextDataPointIsReboot = false;
    }
    recalibrate();
}

#endif  // ES_ANDROID_USE_NTP
