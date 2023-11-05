//
//  ESNTPDriver.hpp
//
//  Created by Steve Pucci 14 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESNTPDRIVER_HPP
#define _ESNTPDRIVER_HPP

#include "ESUtil.hpp"
#include "ESTime.hpp"
#include "ESTimeSourceDriver.hpp"
#include "ESNTPHostNames.hpp"
#include "ESTimer.hpp"

#include "ntp_fp.h"
#undef min
#undef max

// System includes
#include <sys/socket.h>
#include <unistd.h>

// Standard Template Library (STL)
#include <vector>
#include <string>
#include <queue>

// Forward refs
class ESNTPDriver;

// Opaque for private use
class ESNTPThread;
class ESNTPHostNameDescriptor;
class ESNTPSocketDescriptor;
class ESNTPHostReportObserver;
class ESNTPDriverNetworkObserver;
class ESNTPDriverSleepWakeObserver;

/////////////////////////////

/*! The main entry point for ESNTP */
class ESNTPDriver : public ESTimeSourceDriver {
  public:
    void                    startSyncing(bool userRequested);
    virtual void            stopSyncing();
    virtual void            disableSync();
    virtual void            enableSync();
    virtual void            resync(bool userRequested);
    virtual void            setDeviceCountryCode(const char *countryCode);
    virtual std::string     userName();
    static void             setAppSignature(const char *fourByteAppSig);

    static bool             isRunning() { return _theDriver != NULL; }

    void                    stopSyncingIfNecessary();
    static int		    getPollingInterval();
    static void             setPollingInterval(int secs);   // Poll each host once every N seconds, don't stop when synchronized.
                                                            // Call this once per session *before* creating any ESNTPDriver instances.
    void                    restartPollingCycle();
    void                    stopPollingCycle();
    bool                    pollingRunning();
    bool                    pollingCycleRunning();

    // Method callable by ESTime only -- others should go through ESTime
    static ESNTPDriver      *theDriver() { return _theDriver; }

    static ESTimeSourceDriver *makeOne();   // This method can be used as an ESTimeSourceDriverMaker*

    virtual void            release();  // Call this to destroy the driver and its thread in the right order

    static void             registerHostReportObserver(ESNTPHostReportObserver *observer);
    static void             unregisterHostReportObserver(ESNTPHostReportObserver *observer);

    ESTimeInterval          timeSinceLastSuccessfulSynchronization();

    // Methods callable by clients who know there is an ESNTPDriver active
    static int              numPacketsSent();
    static int              numPacketsReceived();
    static ESTimeInterval   lastSyncElapsedTime();
    static double           avgPacketsSent();
    static ESTimeInterval   avgSyncElapsedTime();
    static ESTimeInterval   timeOfLastSuccessfulSync();
    static ESTimeInterval   timeOfLastSyncAttempt();

// Methods called by ntp internals
    void                    gettimeofdayCont(struct timeval    *tv);
    void                    stopSyncingInThisThread();
    void                    stopSyncingIfNecessaryInThisThread();
    void                    enableSyncInThisThread();
    void                    disableSyncInThisThread();
    void                    resyncInThisThread(bool userRequested);
    void                    constructAndSendHostReportToObserver(ESNTPHostReportObserver *observer);
    void                    setDeviceCountryCodeInThread(const char *countryCode);
    void                    enteringBackgroundInThread(ESBackgroundTaskID taskID);

    bool                    inDriverThread();

  private:
                            ESNTPDriver();  // private:  Call only through makeOne()
                            ~ESNTPDriver();  // private:  Call only through release()

    static void             deleteGlue(void*, void*);
    static void             releaseGlue(void*, void*);

    void                    releaseInThread();

    // The main entry point called from the thread's main method
    void                    *threadMain();

    ESNTPThread             *thread() { return _thread; }

    void                    nameResolutionFailed(ESNTPHostNameDescriptor *hostNameDescriptor,
                                                 const std::string       &hostName,
                                                 int                     failureStatus);
    void                    nameResolutionComplete(ESNTPHostNameDescriptor *hostNameDescriptor);

    void                    startSyncingAfterDelay();

    void                    sendToNextPollingHost();

    void                    goingToSleep();
    void                    wakingUp();
    void                    enteringBackground();
    void                    leavingBackground();

    void                    setBitsForSocketFDs(fd_set *);
    void                    processRemoteSockets(fd_set *);
    void                    closeAllSockets();
    void                    checkMaxSocketFD(int fd);

    bool                    resolveOnePoolHostDNS();

    bool                    gotPacket(ESTimeInterval        minOffset,
                                      ESTimeInterval        maxOffset,
                                      ESNTPSocketDescriptor *socketDescriptor);

    void                    pushPoolSocket(ESNTPSocketDescriptor *socketDescriptor);

    void                    sendPacketToFastestPoolHost();
    void                    sendPacketToFastestPoolHostIncludingThisOne(ESNTPSocketDescriptor *socketDescriptor);
    bool                    ensureFreshHostAvailable();

    void                    installNoProgressTimeout();

    void                    logAllHostAddresses(bool       includeNoPacketHosts,
                                                const char *introMessage);

    void                    incrementNumPacketsSent();
    void                    incrementNumPacketsReceived();

    void                    giveUpTimerFire();
    void                    bumpProximity();

    ESTimeInterval          getThrottleTicket(ESTimeInterval sendTime);

    static std::string      getAppropriateCountryCode();

    // (possible) state transition triggers
    void                    stateGotFinishingPacket();
    void                    stateGotBadPacket(ESNTPSocketDescriptor *socketDescriptor);
    void                    stateGotPacketWithReportableSync(ESNTPSocketDescriptor *socketDescriptor);
    void                    stateGotPacketWithUnreportableSync(ESNTPSocketDescriptor *socketDescriptor,
                                                               ESTimeInterval        rtt);
    void                    stateGotNameResolutionComplete(ESNTPHostNameDescriptor *hostNameDescriptor);
    void                    stateGotNameResolutionFailure(ESNTPHostNameDescriptor *hostNameDescriptor);
    void                    stateGotPostResolutionFailureTimerTimeout(ESNTPHostNameDescriptor *hostNameDescriptor);
    void                    stateGotShortSocketTimeout(ESNTPSocketDescriptor *socketDescriptor);
    void                    stateGotLongSocketTimeout(ESNTPSocketDescriptor *socketDescriptor);
    void                    stateGotNoProgressTimeout();
    void                    stateGotNetworkUnreachable(bool knownToBeActive);
    void                    stateGotNetworkReachable();
    void                    stateGotResyncTimeout();
    void                    stateGotPacketSendError(ESNTPSocketDescriptor *socketDescriptor);

    void                    makeHostReportIfRequired();

#undef ES_SURVEY_POOL_HOSTS
#ifdef ES_SURVEY_POOL_HOSTS
    void                    runPoolHostSurvey();
#endif

    // private data
    ESNTPThread             *_thread;
    int                     _maxSocketFD;
    double                  _maxMinOffset;
    double                  _minMaxOffset;
    bool                    _stopThread;
    bool                    _stopReading;
    bool                    _reading;
    bool                    _released;
    bool                    _networkReachable;
    ESTimeInterval          _lastSuccessfulSyncTime;
    int                     _poolHostLookupFailures;
    int                     _throttleNumTicketsIssued;
    int                     _currentProximity;
    ESTimeInterval          _throttleLastTicketTime;
    ESTimeInterval          _throttleSendInterval;
    ESTimeInterval          _lastFailTimeout;
    ESTimer                 *_noProgressTimeout;
    ESTimer                 *_restartSyncTimeout;
    ESTimer                 *_giveUpTimer;
    ESTimer                 *_proximityBumpTimer;
    bool                    _disabled;
    static ESUINT32         _appSignature;

    ESNTPDriverNetworkObserver *_networkObserver;
    ESNTPDriverSleepWakeObserver *_sleepWakeObserver;
    
    std::string             _deviceCountryCode;
    ESNTPHostNamesIterator  _hostNameIterator;  // Must follow _deviceCountryCode for initialization ordering
    std::vector<ESNTPHostNameDescriptor> _hostNameDescriptors;
    std::priority_queue<ESNTPSocketDescriptor*, std::vector<ESNTPSocketDescriptor *>, bool (*)(ESNTPSocketDescriptor *, ESNTPSocketDescriptor *)> _availablePoolSocketsByRTT;

    static ESNTPDriver      *_theDriver;

friend class ESNTPThread;
friend class ESNTPSocketDescriptor;
friend class ESNTPHostNameDescriptor;
friend class ESNTPHostReport;
friend class ESNTPDriverNetworkObserver;
friend class ESNTPNoProgressTimeoutObserver;
friend class ESNTPRestartSyncTimeoutObserver;
friend class ESNTPGiveUpTimerObserver;
friend class ESNTPProximityBumpTimerObserver;
friend class ESNTPDriverSleepWakeObserver;
friend class ESNTPPollingHostTimerObserver;
};

#endif // _ESNTPDRIVER_HPP
