//
//  ESXGPS150TimeDriver_iOS.mm
//
//  Created by Steve Pucci 18 Nov 2012
//  Copyright Emerald Sequoia LLC 2012. All rights reserved.
//

#include "ESXGPS150TimeDriver.hpp"

#import "ExternalAccessory/ExternalAccessory.h"

#include "ESErrorReporter.hpp"
#include "ESWatchTime.hpp"
#define ESTRACE
#include "ESTrace.hpp"

#define PROTOCOL_STRING @"com.dualav.xgps150"

static double sumdelta;
static double sumsquaredelta;
static int numdeltas;
static double mindelta = 1E100;
static double maxdelta = -1E100;

static ESTimeEnvironment *gpsTimeEnv;

static bool isCharging;
static float batteryPercent;
static ESTimeInterval deviceStatusContTime;

@interface ESXGPS150TimeDriverStreamDelegate : NSObject<NSStreamDelegate> {
    ESXGPS150TimeDriver *driver;
    bool                sendFastSampleRateCommandWhenSpaceAvailable;
}
-(id)initWithDriver:(ESXGPS150TimeDriver *)driver;
@end

@implementation ESXGPS150TimeDriverStreamDelegate

-(id)initWithDriver:(ESXGPS150TimeDriver *)driverParam {
    if ((self = [super init]) == nil) return nil;
    driver = driverParam;
    return self;
}

// Notification Center callback
-(void)accessoryDidConnect:(NSNotification *)notification {
    tracePrintf("ESXGPS150TimeDriverStreamDelegate accessoryDidConnect\n");
    if (driver) {
        driver->resync(false/*!userRequested*/);
    }
}

// Delegate callback (note parameter type)
-(void)accessoryDidDisconnect:(EAAccessory *)accessory {
    tracePrintf("ESXGPS150TimeDriverStreamDelegate accessoryDidDisconnect\n");
    if (driver) {
        driver->stopSyncing();
    }
}

-(void)disconnectDriver {
    driver = nil;
}

-(void)sendFastSampleRateCommandWhenSpaceAvailable {
    sendFastSampleRateCommandWhenSpaceAvailable = true;
}

- (void)stream:(NSStream *)theStream handleEvent:(NSStreamEvent)eventCode {
    ESTimeInterval cNow = ESTime::currentContinuousTime();  // Keep this first so it's as soon as possible after receipt of packet
    if (!driver) {
        return;
    }
    if (sendFastSampleRateCommandWhenSpaceAvailable && theStream == driver->outputStream()) {
        sendFastSampleRateCommandWhenSpaceAvailable = false;  // Do this first; it might happen again
        driver->setFastSampleRate(driver->outputStream());
        return;
    }
    if (theStream != driver->inputStream()) {
        return;  // Probably left over from a prior session
    }
    ESTimeInterval now = ESTime::ntpTimeForCTime(cNow);
    ESWatchTime watchTime(now);
    switch(eventCode) {
      case NSStreamEventHasBytesAvailable:
        {
            uint8_t buf[1025];
            char *bufp = (char *)(buf);
            unsigned int len = 0;
            len = (unsigned int)[(NSInputStream *)theStream read:buf maxLength:1024];
            if(len) {
                while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n')) {
                    len--;
                }
                buf[len] = 0;
                //tracePrintf1("Message from XGPS150:\n%s\n", buf);
                int hour;
                int min;
//                int sec;
//                int thousandths;
                float seconds;
                double lat;
                double lon;
                char NS;
                char EW;
                int fixStatus;
                int numSats;
                float hDilution;
                double altMeters;
                double heightMeters;

                int st;
                //printf("Buf: %s\n", buf);
                if ((st = sscanf(bufp, "PGGA,%2d%2d%f,%lf,%c,%lf,%c,%d,%02d,%f,%lf,M,%lf,M", &hour, &min, &seconds, &lat, &NS, &lon, &EW, &fixStatus, &numSats, &hDilution, &altMeters, &heightMeters)) == 12) {
                    if (!gpsTimeEnv) {
                        gpsTimeEnv = new ESTimeEnvironment("UTC", true/*observingIPhoneTime*/);
                    }
                    tracePrintf7("%02d%02d%06.3f : (%s) (%d satellites, %.1f dilution) %s\n",
                                 watchTime.hour24NumberUsingEnv(gpsTimeEnv),
                                 watchTime.minuteNumberUsingEnv(gpsTimeEnv),
                                 watchTime.secondValueUsingEnv(gpsTimeEnv),
                                 fixStatus == 1 ? "valid" : "invalid",
                                 numSats,
                                 hDilution,
                                 bufp);
//                    double seconds = sec + thousandths / 1000.0;
                    double delta = fabs(watchTime.secondValueUsingEnv(gpsTimeEnv) - seconds);
                    printf("...delta = %7.4f\n", delta);
                    if (delta < 1.0) {
                        if (delta < mindelta) {
                            mindelta = delta;
                        }
                        if (delta > maxdelta) {
                            maxdelta = delta;
                        }
                        sumdelta += delta;
                        sumsquaredelta += (delta * delta);
                        numdeltas++;
                        printf("......cumulative avg (%d samples) %.4f ± %.4f, min %.4f, max %.4f\n",
                               numdeltas,
                               sumdelta / numdeltas,
                               sqrt(((numdeltas * sumsquaredelta) - (sumdelta * sumdelta)) / (numdeltas * (numdeltas - 1))),
                               mindelta,
                               maxdelta);
                    }
//                } else if ((st = sscanf(bufp, "PGGA,%2d%2d%f,%lf,%c,%lf,%c,%d", &hour, &min, &seconds, &lat, &NS, &lon, &EW, &fixStatus)) == 8) {
                } else if (*bufp == '@') {
                    if (cNow > deviceStatusContTime + 5) {
                        int vbat;
                        float bvolt, batLevel, batteryVoltage;
        
#define kVolt415				644     // Battery level conversion constant.
#define kVolt350				543     // Battery level conversion constant.

                        vbat = (unsigned char)buf[1];
                        vbat <<= 8;
                        vbat |= (unsigned char)buf[2];
                        if (vbat < kVolt350) vbat = kVolt350;
                        if (vbat > kVolt415) vbat = kVolt415;
                        
                        bvolt = (float)vbat * 330.0f / 512.0f;
                        batLevel = ((bvolt / 100.0f) - 3.5f) / 0.65f;
                        if (batLevel > 1.0) batteryVoltage = 1.0;
                        else if (batLevel < 0) batteryVoltage = 0.0;
                        else batteryVoltage = batLevel;
                        
                        if( buf[5] & 0x04 ) isCharging = true;
                        else isCharging = false;
                        batteryPercent = batteryVoltage * 100;
                        deviceStatusContTime = cNow;
                        tracePrintf2("Device status: battery %.0f%%, %s\n", batteryPercent, isCharging ? "charging" : "not charging");
                    }
                } else {
//                    tracePrintf1("Unrecognized message from XGPS150:\n%s\n", buf);
                }
            } else {
                NSLog(@"no buffer!");
            }
            break;
        }
    }
}
@end

ESXGPS150TimeDriver::ESXGPS150TimeDriver()
:   _inputStream(nil),
    _delegate([[ESXGPS150TimeDriverStreamDelegate alloc] initWithDriver:this]),
    _registeredForAccessoryNotifications(false),
    _sendFastSampleRateCommandWhenSpaceAvailable(false),
    _useFastSampleRate(false)
{
}

ESXGPS150TimeDriver::~ESXGPS150TimeDriver() {
    if (_inputStream) {
        stopSyncing();
    }
    ESAssert(!_inputStream);
    [_delegate disconnectDriver];
    [_delegate release];
}

/*virtual*/ void 
ESXGPS150TimeDriver::stopSyncing() {
    if (_inputStream) {
        // Set status
        switch(_status) {
          case ESTimeSourceStatusNoNetSynchronized:
          case ESTimeSourceStatusPollingSynchronized:
          case ESTimeSourceStatusFailedSynchronized:
          case ESTimeSourceStatusSynchronized:
            setStatus(ESTimeSourceStatusFailedSynchronized);
            break;
          case ESTimeSourceStatusPollingRefining:
          case ESTimeSourceStatusRefining:
          case ESTimeSourceStatusFailedRefining:
          case ESTimeSourceStatusNoNetRefining:
            setStatus(ESTimeSourceStatusFailedRefining);
            break;
          default:
            setStatus(ESTimeSourceStatusFailedUnynchronized);
            break;
        }

        // Kill session
        [_inputStream close];
        [_inputStream removeFromRunLoop:[NSRunLoop currentRunLoop]
                                forMode:NSDefaultRunLoopMode];
        [_inputStream release];
        _inputStream = nil;
    }
}

void
ESXGPS150TimeDriver::setFastSampleRate(NSOutputStream *outputStream) {
    traceEnter("ESXGPS150TimeDriver::setFastSampleRate");
    ESAssert(_useFastSampleRate);
    const uint8_t cfgFast[12] = {
        'S', '0', '5',
        'F', 'F', 'F', 'F',
        'F', 'F', 'F', 'F',    
        0x0a
    };
    
    if([outputStream hasSpaceAvailable]) {
        NSInteger written = [outputStream write:cfgFast maxLength:12];
        if (written != 12) {
            _useFastSampleRate = false;
            ESAssert(false);
        }
        tracePrintf1("setFastSampleRate wrote %d bytes to output stream\n", written);
    } else {
        ESAssert(_delegate);
        ESAssert(false);  // Can remove this if it ever triggers
        [_delegate sendFastSampleRateCommandWhenSpaceAvailable];
    }
    traceExit("ESXGPS150TimeDriver::setFastSampleRate");
}

/*virtual*/ void 
ESXGPS150TimeDriver::resync(bool userRequested) {
    traceEnter1("ESXGPS150TimeDriver::resync(%s)", userRequested ? "true" : "false");
    if (_inputStream) {
        stopSyncing();
    }
    ESAssert(_inputStream == nil);

    EAAccessoryManager *mgr = [EAAccessoryManager sharedAccessoryManager];

    NSArray *accessories = [mgr connectedAccessories];
    tracePrintf1("found %d connected accessories\n", [accessories count]);
    EAAccessory *accessory = nil;
    for (EAAccessory *obj in accessories) {
        if ([[obj protocolStrings] containsObject:PROTOCOL_STRING]) {
             tracePrintf1("found accessory with matching protocol %s\n", [PROTOCOL_STRING UTF8String]);
            accessory = obj;
            tracePrintf1("...serial number %s\n", [[obj name] UTF8String]);
            tracePrintf1("...firmware version %s\n", [[obj firmwareRevision] UTF8String]);
            NSArray *firmwareComponents = [[obj firmwareRevision] componentsSeparatedByString:@"."];
            _useFastSampleRate = !(
                [[firmwareComponents objectAtIndex:0] isEqualToString:@"1"] &&
                [[firmwareComponents objectAtIndex:1] intValue] < 1);
            tracePrintf1("_useFastSampleRate %s\n", _useFastSampleRate ? "true" : "false");
            break;
        }
    }
 
    if (accessory) {
        EASession *session = [[EASession alloc] initWithAccessory:accessory
                                                      forProtocol:PROTOCOL_STRING];
        if (session) {
            _inputStream = [session inputStream];
            [_inputStream setDelegate:_delegate];
            [_inputStream scheduleInRunLoop:[NSRunLoop currentRunLoop]
                                    forMode:NSDefaultRunLoopMode];
            [_inputStream open];
            _outputStream = [session outputStream];
            [_outputStream open];
            if (_useFastSampleRate) {
                setFastSampleRate(_outputStream);
            }
            [_outputStream setDelegate:_delegate];
            [_outputStream scheduleInRunLoop:[NSRunLoop currentRunLoop]
                                     forMode:NSDefaultRunLoopMode];
            [session autorelease];

        }
    }

    if (!_registeredForAccessoryNotifications) {
        [mgr registerForLocalNotifications];
        [[NSNotificationCenter defaultCenter] addObserver:_delegate selector:@selector(accessoryDidConnect:) name:EAAccessoryDidConnectNotification object:mgr];
    }
    traceExit1("ESXGPS150TimeDriver::resync(%s)", userRequested ? "true" : "false");
}

