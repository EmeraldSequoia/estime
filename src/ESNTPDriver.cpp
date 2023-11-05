//
//  ESNTPDriver.cpp
//
//  Created by Steve Pucci 17 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#define ESTRACE  // This should be first
#include "ESTrace.hpp"

#include "ESPlatform.h"

#include "ESTime.hpp"
#include "ESNTPDriver.hpp"

#include "ESUtil.hpp"
#include "ESUserPrefs.hpp"
#include "ESErrorReporter.hpp"
#include "ESThread.hpp"
#include "ESNTPHostNames.hpp"
#include "ESNameResolver.hpp"
#include "ESNTPHostReport.hpp"
#include "ESNetwork.hpp"
#include "ESSystemTimeBase.hpp"
#if ES_TRIPLEBASE
#include "ESTimeCalibrator.hpp"
#endif

#include "ntp_fp.h"
#include "ntp.h"
#include "ntp_unixtime.h"

#include <strings.h>

// From ntpdate.c
#define	NTPDATE_PRECISION	(-6)		/* use this precision */
#define	NTPDATE_DISTANCE	FP_SECOND	/* distance is 1 sec */
#define	NTPDATE_DISP		FP_SECOND	/* so is the dispersion */
#define	NTPDATE_REFID		(0)		/* reference ID to use */

// All times below are in seconds
#define ES_DISPERSION_REJECT    0.3             /* Packets with a root dispersion greater than this are rejected out of hand */
#define ES_ROOT_DELAY_REJECT    1.0             /* Packets with a root delay greater than this are rejected out of hand */
#define ES_FIX_GOOD             0.16            /* Good enough to say we're done */
#define ES_FIX_REPORT           0.4             /* Good enough to report even if the old sync value is between the error bounds. */
#define ES_FIX_TIMEOUT          (3600.0 * 1.0)  /* We will attempt to resynchronize this often */
#define ES_FIX_TIMEOUT_INVALID  (3600.0 * 7.5)  /* If the synchronization is older than this then we no longer consider ourselves synchronized (we go yellow if we fail after this) */
#define ES_FIX_TIMEOUT_FAILED   (60.0 * 10.0)   /* We will attempt to resynchronize this often if we failed */
#define ES_FIX_TIMEOUT_FAILED_BACKOFF 2.0       /*  ... but we back off by this much every time we fail */
#define ES_FIX_TIMEOUT_FAILED_MAX ES_FIX_TIMEOUT /*  ... until we get here */
#define ES_SEND_FAILURE_THROTTLE 60.0           /* If a send on a particular socket (to a particular IP address) fails, wait this long before trying again */
#define ES_FORCE_RTT_FOR_BAD_PACKET 1800.0      /* A host which sends bad packets is forced to presume a large average RTT (for sorting only) so it will sort lower and be used only if there aren't any other hosts */
#define ES_FORCE_RTT_FOR_BAD_HOST 3600.0        /*   .. the difference between BAD_PACKET and BAD_HOST is that the former could conceivably be a network glitch rather than a host with bad data */
#define ES_RESOLUTION_FAILURE_RETRY_BASE 10.0   /* Delay this long the first time a given host's name resolution fails (and longer on each subsequent time) */
#define ES_SHORT_SOCKET_TIMEOUT 1.0             /* Try another host if a packet takes this long */
#define ES_LONG_SOCKET_TIMEOUT  10.0            /* If a packet takes this long, then presume the send failed */
#define ES_NO_PROGRESS_TIMEOUT  2.0             /* When we get this far without making progress, we add another host */
#define ES_PROXIMITY_LEVEL_TIMEOUT 10.0         /* Wait this long before moving to the next proximity level (country => continent => global => apple) */
#define ES_MAX_ACTIVE_POOL_HOSTS 10             /* The maximum number of pool hosts we are actively sending packets to */
#define ES_NETWORK_RESTART_DELAY 4.0            /* Wait this long after being informed of network availability before attempting to restart */
#define ES_THROTTLE_BASE_PACKETS 6              /* This packet number is the first to be throttled */
#define ES_THROTTLE_BASE_INTERVAL 0.5           /* The first time we throttle sends, we wait this long */
#define ES_THROTTLE_INTERVAL_MULTIPLIER 1.05    /* Each time we send after we start throttling, we increase the interval by this much */
#define ES_THROTTLE_INTERVAL_MAX  15.0          /* ...but don't make the interval any larger than this */
#define ES_GIVE_UP_INTERVAL      (60.0 * 8)     /* When this much has elapsed since we started syncing, and we haven't succeeded, give up */
#define ES_BASE_PENALTY_STRATUM    8            /* The first stratum which incurs a penalty for presumed unreported error */
#define ES_BASE_STRATUM_PENALTY    0.5          /* The amount of error presumed to be added to a host at stratum ES_BASE_PENALTY_STRATUM */
#define ES_PENALTY_PER_STRATUM     0.25         /* The amount of additional penalty added to ES_BASE_STRATUM_PENALTY per stratum that we are above ES_BASE_PENALTY_STRATUM */

#undef min
#undef max

#ifdef ES_SURVEY_POOL_HOSTS
extern const char *poolHosts[];
extern int numPoolHosts;
#endif

#include <string>
#include <iostream>

#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

class ESNTPSocketDescriptor;
class ESNTPHostNameDescriptor;

// Class-static variables (only in the class for quick access via static inline method)
/*static*/ ESNTPDriver *ESNTPDriver::_theDriver = NULL;  // singleton
/*static*/ ESUINT32 ESNTPDriver::_appSignature;

#define ES_NTP_PORT_AS_STRING "123"
#define ES_NTP_PORT_AS_NETWORK_NUMBER htons(123)

// File-static variables
static std::list<ESNTPHostReportObserver *> *hostReportObservers;

// Here so we can access with static methods, e.g., ESNTPDriver::numPacketsSent();
static int _numPacketsSent = 0;
static int _numPacketsReceived = 0;
static int _numLongTimeoutsTriggered = 0;
static ESTimeInterval _lastSyncStart = 0;
static ESTimeInterval _lastSyncElapsedTime = 0;
static int _sumNumPacketsSent = 0;
static ESTimeInterval _sumLastSyncElapsedTime = 0;
static int _numSyncs = 0;
static ESTimeInterval _timeOfLastSuccessfulSync = 0;
static ESTimeInterval _timeOfLastSyncAttempt = 0;

static bool inPollingMode = false;
static int nextSendHostIndex = 0;
static bool _pollingCycleRunning = false;
static bool _pollingRunning = false;
static int _pollingInterval = 0; // Wait this long between polling cycles
#define POLLING_INTERVALB 1.0  // Wait this long between polling cycles when there are unresolved host names
#define POLLING_HOST_GAP  0.2  // Wait this long between hosts in a single polling cycle

static ESTimeInterval timeStampMake(l_fp *ts) {
    struct timeval tv;
    //gettimeofday(&tv, NULL);
    ESTimeInterval esti = ESTime::gettimeofdayCont(&tv);
    ts->l_i = (int32)tv.tv_sec + JAN_1970;
    double dtemp = tv.tv_usec / 1e6;
    dtemp *= FRAC;
    ts->l_uf = (u_int32)dtemp;
    return esti;
}

// Class to get notified when the network goes away or comes back.
class ESNTPDriverNetworkObserver : public ESNetworkInternetObserver {
  public:
                            ESNTPDriverNetworkObserver(ESNTPDriver *driver,
                                                       bool        currentReachability)
    :   _driver(driver),
        ESNetworkInternetObserver(currentReachability)
    {
    }

    /*virtual*/ void        internetIsNowReachable() {
        _driver->stateGotNetworkReachable();
    }
    /*virtual*/ void        internetIsNowUnreachable() {
        _driver->stateGotNetworkUnreachable(false/* !knownToBeActive*/);
    }
  private:
    ESNTPDriver             *_driver;
};

/** Representation of transmit time, in host time and network time formats */
class ESNTPxmitTimeStamp {
  public:
                            ESNTPxmitTimeStamp(ESNTPxmitTimeStamp *oldHead)
    :   next(oldHead),
        longTimeoutTimer(NULL)
    {
        xmitTimeES = timeStampMake(&xmitTimeFP);
    }
                            ~ESNTPxmitTimeStamp()
    {
        if (longTimeoutTimer) {
            longTimeoutTimer->release();
        }
    }
    l_fp	            xmitTimeFP;	/* my transmit time stamp */
    ESTimeInterval          xmitTimeES;	/* my transmit time stamp */
    ESTimer                 *longTimeoutTimer;
    ESNTPxmitTimeStamp      *next;
};


/*! A host name and its associated IP addresses and sockets, if any */
class ESNTPHostNameDescriptor : public ESNameResolverObserver {
  public:
                            ESNTPHostNameDescriptor();
                            ~ESNTPHostNameDescriptor();
    void                    init(const std::string &name,
                                 int               proximity,
                                 bool              isUserHost,
                                 ESNTPDriver       *driver);
    void                    startResolving();  // Start a background name resolution

    std::string             nameAsRequested() const { return _name; }

    // ESNameResolverObserver redefines
    /*virtual*/ void        notifyNameResolutionComplete(ESNameResolver *resolver);
    /*virtual*/ void        notifyNameResolutionFailed(ESNameResolver *resolver,
                                                       int            failureStatus);
    int                     numSocketDescriptors() const;

    void                    setBitsForSocketFDs(fd_set *);
    void                    processRemoteSockets(fd_set *);
    void                    closeAllSockets();

    void                    sendPacketsToAllHosts();

    void                    setupPostResolutionFailureTimer();

    ESNTPDriver             *driver() const { return _driver; }

  private:
    void                    postResolutionFailureTimerFire();

    std::string             _name;
    bool                    _isUserHost;
    int                     _proximity;
    ESNameResolver          *_nameResolver;
    int                     _numSocketDescriptors;
    ESNTPSocketDescriptor   *_socketDescriptors;
    ESNTPDriver             *_driver;
    bool                    _resolving;
    bool                    _doneResolving;
    int                     _numResolutionFailures;
    ESTimer                 *_postResolutionFailureTimer;
friend class ESNTPSocketDescriptor;
friend class ESNTPHostReport;
friend class ESNTPDriver;
friend class ESNTPDriverPostResolutionFailureTimerObserver;
};

// Class used only in private methods
class ESNTPSocketDescriptor {
  public:
                            ESNTPSocketDescriptor();
                            ~ESNTPSocketDescriptor();
    void                    connectAndSendFirstPacket(ESNTPDriver *driver);
    std::string             humanReadableIPAddress() const;
    void                    badHost(ESNTPDriver *driver);
    void                    badPacket(ESNTPDriver *driver);
    void                    sendPacket(ESNTPDriver *driver,
                                       bool        haveTicket);
    bool                    recvPacket(ESNTPDriver *driver);
    int                     fd() const { return _fd; }
    void                    closeSocket();
    int                     packetsReceived() const { return _packetsReceived; }
    int                     packetsUsed() const { return _packetsUsed; }
    int                     packetsSent() const { return _packetsSent; }
    ESNTPHostNameDescriptor *hostNameDescriptor() const { return _hostNameDescriptor; }
    ESTimeInterval          averageRTT() const { return _averageRTT; }
    ESTimeInterval          skewLB() const { return _skewLB; }
    ESTimeInterval          skewUB() const { return _skewUB; }
    ESTimeInterval          nonRTTErrorFromLastPacket() const { return _nonRTTErrorFromLastPacket; }
    int                     leapBitsFromLastPacket() const { return _leapBitsFromLastPacket; }
    int                     stratumFromLastPacket() const { return _stratumFromLastPacket; }
  private:
    void                    sendTimeoutShort();
    void                    sendTimeoutLong();

    ESTimer                 *_timer;  // timer used for packet resend

    int                     _family;  // AF_INET or AF_INET6
    struct sockaddr_storage _addr;
    int                     _addrlen;
    int                     _fd;
    int                     _socktype;
    int                     _protocol;
    ESNTPxmitTimeStamp      *_xmitTimes;
    ESTimeInterval          _lastSendFailureTime;   /* The last time we failed trying to connect and/or send a packet */
    ESTimeInterval          _lastSendTime;
    ESTimeInterval          _averageRTT;
    ESTimeInterval          _sumRTT;  // Used to calculate average, divided by packetsReceived
    int                     _packetsSent;
    int                     _packetsReceived;
    int                     _packetsUsed;
    int                     _leapBitsFromLastPacket;
    int                     _stratumFromLastPacket;
    ESTimeInterval          _skewLB;
    ESTimeInterval          _skewUB;
    ESTimeInterval          _nonRTTErrorFromLastPacket;
    ESNTPHostNameDescriptor *_hostNameDescriptor;
friend class ESNTPHostNameDescriptor;
friend class ESNTPSocketShortTimeoutObserver;
friend class ESNTPSocketLongTimeoutObserver;
friend class ESSendThrottleTimerObserver;
friend class ESNTPDriver;
};

class ESNTPThread : public ESChildThread {
  public:
                            ESNTPThread(ESNTPDriver *driver);
    void                    *main();
    void                    detachAndClose();

  protected:
    /*virtual*/             ~ESNTPThread();  // Use release(), not dtor
  private:
    ESNTPDriver             *_driver;
};

ESNTPThread::ESNTPThread(ESNTPDriver *driver)
:   _driver(driver),
    ESChildThread("NTPDriver", ESChildThreadExitsOnlyByParentRequest)
{
    // This constructor happens in the calling thread, not the thread this object is associated with
    ESAssert(ESThread::inMainThread());
}

ESNTPThread::~ESNTPThread() {
    ESAssert(!inThisThread());
    ESAssert(ESThread::inMainThread());
    delete _driver;   // This is the only way to delete the driver
}

void *
ESNTPThread::main() {
    ESAssert(inThisThread());
    return _driver->threadMain();
}

bool ESNTPSocketRTTGreaterThan(ESNTPSocketDescriptor *s1,
                               ESNTPSocketDescriptor *s2) {
    ESAssert(s1);
    ESAssert(s2);
    return s1->averageRTT() > s2->averageRTT();
}

ESNTPSocketDescriptor::ESNTPSocketDescriptor()
:   _timer(NULL),
    _family(0),
    _addrlen(0),
    _fd(-1),
    _lastSendFailureTime(0),
    _lastSendTime(0),
    _averageRTT(0),
    _sumRTT(0),
    _nonRTTErrorFromLastPacket(0),
    _leapBitsFromLastPacket(0),
    _skewLB(-1e6),
    _skewUB(1e6),
    _xmitTimes(NULL),
    _packetsReceived(0),
    _packetsUsed(0),
    _packetsSent(0),
    _hostNameDescriptor(NULL)
{
    ESAssert(ESNTPDriver::_theDriver);
    ESAssert(ESNTPDriver::_theDriver->thread());
    ESAssert(ESNTPDriver::_theDriver->thread()->inThisThread());  // We can't use _driver because it's NULL before init() is called
}

 
ESNTPSocketDescriptor::~ESNTPSocketDescriptor() {
    if (_hostNameDescriptor) {
        ESAssert(_hostNameDescriptor->_driver->thread()->inThisThread());
        ESAssert(_hostNameDescriptor->_driver == ESNTPDriver::_theDriver);
    } else {
        ESAssert(ESNTPDriver::_theDriver);
        ESAssert(ESNTPDriver::_theDriver->thread());
        ESAssert(ESNTPDriver::_theDriver->thread()->inThisThread());  // We can't use _driver because it's NULL before init() is called
    }
    if (_fd >= 0) {
        closeSocket();
    }
    if (_timer) {
        _timer->release();
    }
    while (_xmitTimes) {
        ESNTPxmitTimeStamp *nxt = _xmitTimes->next;
        delete _xmitTimes;
        _xmitTimes = nxt;
    }
}

class ESNTPSocketLongTimeoutObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPSocketDescriptor *socketDescriptor = (ESNTPSocketDescriptor *)timer->info();
        socketDescriptor->sendTimeoutLong();
    }
};
static ESNTPSocketLongTimeoutObserver *socketLongTimeoutObserver = NULL;

void 
ESNTPSocketDescriptor::sendTimeoutShort() {
    if (!socketLongTimeoutObserver) {
        socketLongTimeoutObserver = new ESNTPSocketLongTimeoutObserver;
    }
    _timer = NULL;
    if (inPollingMode) {
        return;
    }
    ESAssert(_xmitTimes);
    _xmitTimes->longTimeoutTimer = new ESIntervalTimer(socketLongTimeoutObserver, ES_LONG_SOCKET_TIMEOUT);
    _xmitTimes->longTimeoutTimer->setInfo(this);
    _xmitTimes->longTimeoutTimer->activate();
    _hostNameDescriptor->_driver->stateGotShortSocketTimeout(this);
}

void 
ESNTPSocketDescriptor::sendTimeoutLong() {
    if (inPollingMode) {
        return;
    }
    _hostNameDescriptor->_driver->stateGotLongSocketTimeout(this);
}

class ESNTPSocketShortTimeoutObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPSocketDescriptor *socketDescriptor = (ESNTPSocketDescriptor *)timer->info();
        socketDescriptor->sendTimeoutShort();
    }
};
static ESNTPSocketShortTimeoutObserver *socketShortTimeoutObserver = NULL;

void 
ESNTPSocketDescriptor::badHost(ESNTPDriver *driver) {
#ifndef ES_SURVEY_POOL_HOSTS
    _averageRTT = ES_FORCE_RTT_FOR_BAD_HOST;
    driver->stateGotBadPacket(this);
#endif
}

void 
ESNTPSocketDescriptor::badPacket(ESNTPDriver *driver) {
#ifndef ES_SURVEY_POOL_HOSTS
    _averageRTT = ES_FORCE_RTT_FOR_BAD_PACKET;
    driver->stateGotBadPacket(this);
#endif
}

class ESSendThrottleTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPSocketDescriptor *socketDescriptor = (ESNTPSocketDescriptor *)timer->info();
        socketDescriptor->sendPacket(socketDescriptor->_hostNameDescriptor->driver(), true/*haveTicket*/);
    }
};
static ESSendThrottleTimerObserver *sendThrottleTimerObserver = NULL;

void
ESNTPSocketDescriptor::sendPacket(ESNTPDriver *driver,
                                  bool        haveTicket) {
    ESAssert(driver->thread()->inThisThread());
    if (_fd < 0) {
        connectAndSendFirstPacket(driver);
        return;
    }

    if (_timer) {
        _timer->release();  // Might be throttle timer
        _timer = NULL;
    }

    ESTimeInterval sendTime = ESTime::currentContinuousTime();
    if (!haveTicket && !_hostNameDescriptor->_isUserHost && !inPollingMode) {
        ESTimeInterval throttleTicketTime = driver->getThrottleTicket(sendTime);
        if (throttleTicketTime > sendTime) {
            if (!sendThrottleTimerObserver) {
                sendThrottleTimerObserver = new ESSendThrottleTimerObserver;
            }
            ESAssert(!_timer);
            _timer = new ESIntervalTimer(sendThrottleTimerObserver, throttleTicketTime - sendTime, sendTime);
            _timer->setInfo(this);
            _timer->activate();
            tracePrintf3("%s (%s) waiting %.2f seconds to throttle packet sending",
                         humanReadableIPAddress().c_str(),
                         hostNameDescriptor()->nameAsRequested().c_str(),
                         throttleTicketTime - sendTime);
            return;
        }
    }

    struct pkt xpkt;
    xpkt.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOWARNING, NTP_VERSION, MODE_CLIENT);
    xpkt.stratum = STRATUM_TO_PKT(STRATUM_UNSPEC);
    xpkt.ppoll = NTP_MINPOLL;
    xpkt.precision = NTPDATE_PRECISION;
    // Identify packets for tracking on pool servers (see email from leo@leobodnar.com).
    xpkt.rootdelay = htonl('E' << 8 | 'S');                               // Company: ES
    xpkt.rootdispersion = driver->_appSignature;  // Product: Obs, TS, etc
    xpkt.refid = htonl(NTPDATE_REFID);
    L_CLR(&xpkt.reftime);
    L_CLR(&xpkt.org);
    L_CLR(&xpkt.rec);
    // record the time in our format, put it on the front of the list
    _xmitTimes = new ESNTPxmitTimeStamp(_xmitTimes);
    HTONL_FP(&_xmitTimes->xmitTimeFP, &xpkt.xmt);
    // printf("Sending packet: ");
    // for (int i = 0; i < LEN_PKT_NOMAC; i++) {
    //     char c = ((const UInt8 *)&xpkt)[i];
    //     if (c == '\0') {
    //         printf(" ");
    //     } else if ((UInt8)c < 32 || (UInt8)c > 127) {
    //         printf("?");
    //     } else {
    //         printf("%c", c);
    //     }
    // }
    // printf(" [or]");
    // for (int i = 0; i < LEN_PKT_NOMAC; i++) {
    //     char c = ((const UInt8 *)&xpkt)[i];
    //     printf(" %u", (UInt8)c);
    // }
    // printf("\n");
    size_t sendSz = LEN_PKT_NOMAC;
//    ssize_t sz = sendto(_fd, &xpkt, sendSz, 0, (const sockaddr *)&_addr, _addrlen);
    ssize_t sz = send(_fd, &xpkt, sendSz, 0);
    if (sz == ((size_t)-1)) {
        ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "send() failure");
    } else {
        //printf("Sent %lu (of %lu) bytes in a packet to %s (%s)\n", sz, sendSz, _name.c_str(), humanReadableIPAddress().c_str());
    }
    if (sz == sendSz) {
        _lastSendTime = sendTime;
        _lastSendFailureTime = 0;
        if (!socketShortTimeoutObserver) {
            socketShortTimeoutObserver = new ESNTPSocketShortTimeoutObserver;
        }
        tracePrintf2("%s (%s) sent packet",
                     humanReadableIPAddress().c_str(),
                     hostNameDescriptor()->nameAsRequested().c_str());
#ifndef ES_SURVEY_POOL_HOSTS
        ESAssert(!_timer);
        _timer = new ESIntervalTimer(socketShortTimeoutObserver, ES_SHORT_SOCKET_TIMEOUT);
        _timer->setInfo(this);
        _timer->activate();
#endif
        _packetsSent++;
        driver->incrementNumPacketsSent();
        tracePrintf4("sendPacket incrementing number of packets sent to %d (sent) %d (received) %d (outstanding) %d (longtimeout)",
                     _numPacketsSent,
                     _numPacketsReceived,
                     _numPacketsSent - _numPacketsReceived,
                     _numLongTimeoutsTriggered);
    } else {
        ESErrorReporter::logError("ESNTPDriver", "Packet send failed\n");
        _averageRTT = ES_FORCE_RTT_FOR_BAD_HOST;
        _lastSendFailureTime = sendTime;
        driver->stateGotPacketSendError(this);
    }
}

bool
ESNTPSocketDescriptor::recvPacket(ESNTPDriver *driver) {
    struct pkt response;
    // int len = recvfrom(_fd, (char *)&response, sizeof(response), 0, (struct sockaddr *)&serverAddr, &slen);
    ssize_t len = recv(_fd, (char *)&response, sizeof(response), 0);
    l_fp recvTimeFP;
#ifdef ES_SURVEY_POOL_HOSTS
    ESTimeInterval recvTimeES =
#endif
        timeStampMake(&recvTimeFP);

    l_fp	org;		/* originate time stamp (server's copy of xmitTime)*/
    NTOHL_FP(&response.org, &org);
    ESNTPxmitTimeStamp *prev = NULL;
    ESNTPxmitTimeStamp *xmit = _xmitTimes;
    while (xmit && !L_ISEQU(&org, &xmit->xmitTimeFP)) {
        prev = xmit;
        xmit = xmit->next;
    }
    if (!xmit) {  // We didn't find one -- must have been from a previous sync, which might have been on a different time base
        tracePrintf("response xmit time didn't match any of ours");
	return false;
    }
    // If we're here, xmit matches; remove it from the list
    ESAssert(L_ISEQU(&org, &xmit->xmitTimeFP));
    if (prev) {
        prev->next = xmit->next;
    } else {
        ESAssert(xmit == _xmitTimes);
        _xmitTimes = xmit->next;
    }
#ifdef ES_SURVEY_POOL_HOSTS
    ESTimeInterval xmitTimeES = xmit->xmitTimeES;
#endif
    delete xmit;

    _packetsReceived++;
    driver->incrementNumPacketsReceived();
    tracePrintf4("Got a (matching) packet, packet stats are: %d (sent) %d (received) %d (outstanding) %d (longtimeout)",
                     _numPacketsSent,
                     _numPacketsReceived,
                     _numPacketsSent - _numPacketsReceived,
                     _numLongTimeoutsTriggered);

    if (_timer) {
        _timer->release();  // Whatever we were waiting for doesn't matter any more
        _timer = NULL;
    }

    // Precision (sys.precision, peer.precision, pkt.precision): This is a
    // signed integer indicating the precision of the various clocks, in
    // seconds to the nearest power of two. The value must be rounded to the
    // next larger power of two; for instance, a 50-Hz (20 ms) or 60-Hz (16.67
    // ms) power-frequency clock would be assigned the value -5 (31.25 ms),
    // while a 1000-Hz (1 ms) crystal-controlled clock would be assigned the
    // value -9 (1.95 ms).

    // Root Delay (sys.rootdelay, peer.rootdelay, pkt.rootdelay): This is a
    // signed fixed-point number indicating the total roundtrip delay to the
    // primary reference source at the root of the synchronization subnet, in
    // seconds. Note that this variable can take on both positive and negative
    // values, depending on clock precision and skew.

    // Root Dispersion (sys.rootdispersion, peer.rootdispersion,
    // pkt.rootdispersion): This is a signed fixed-point number indicating the
    // maximum error relative to the primary reference source at the root of
    // the synchronization subnet, in seconds. Only positive values greater
    // than zero are possible.

    u_char	leap;		/* local leap indicator */
    //u_char	pmode;		/* remote association mode */
    u_char	stratum;	/* remote stratum */
    //u_char	ppoll;		/* remote poll interval */
    double	precision;	/* remote clock precision */
    double	rootdelay;	/* roundtrip delay to primary clock */
    double	rootdispersion;	/* dispersion to primary clock */
    //u_int32	refid;		/* remote reference ID */
    l_fp	reftime;	/* update epoch */
    l_fp	rec;		/* receive time stamp */
    l_fp	xmt;		/* transmit time stamp */
    s_fp di;

    /*
     * Basic sanity checks
     */
    if (len != LEN_PKT_NOMAC) {
	//[self failed:[NSString stringWithFormat:@"got %d bytes", len]];
        tracePrintf1("recv call failed, length was %d", len);
        ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "recv() failure");
        badPacket(driver);  // Not the host's fault
	return false;
    }
    if (PKT_VERSION(response.li_vn_mode) < NTP_OLDVERSION || PKT_VERSION(response.li_vn_mode) > NTP_VERSION) {
	//[self failed:@"bad version"];
        tracePrintf1("response version funny %u", PKT_VERSION(response.li_vn_mode));
        badHost(driver);
	return false;
    }
    if ((PKT_MODE(response.li_vn_mode) != MODE_SERVER && PKT_MODE(response.li_vn_mode) != MODE_PASSIVE)) {
	//[self failed:[NSString stringWithFormat:@"received mode %d stratum %d", PKT_MODE(response.li_vn_mode), response.stratum]];
        tracePrintf1("response mode funny %d", PKT_MODE(response.li_vn_mode));
        badHost(driver);
	return false;
    }
    if (response.stratum == STRATUM_PKT_UNSPEC) {
        tracePrintf1("response stratum unspecified", response.stratum);
        badHost(driver);
	return false;
    }
    
    /*
     * More sanity checks.
     */
    stratum = PKT_TO_STRATUM(response.stratum);
    _stratumFromLastPacket = stratum;
    if (stratum == 0) {  // No such thing as a stratum-0 host, and in fact PKT_TO_STRATUM should never generate this
        tracePrintf1("response stratum funny %d", stratum);
        badHost(driver);
        return false;
    }

    if (response.precision < 0) {
        precision = 1.0 / (1 << -response.precision);
    } else {
        precision = 1 << response.precision;
    }
    if (precision > 0.25) {
        tracePrintf2("response precision too high %.8f (stratum is %d)", precision, stratum);
        badHost(driver);  // It's not bad per se, but it's not going to be much help
        return false;
    }
    // Note: below we reject delays and dispersions that are 0.  That would mean either we're looking
    // at a stratum 1 server (which we shouldn't pound on) or, more likely, at a server that's reporting wrong
    rootdelay = FPTOD(ntohl(response.rootdelay));
    if (rootdelay > ES_ROOT_DELAY_REJECT) {
        tracePrintf2("response rootdelay is too large at %.3f (stratum is %d)", rootdelay, stratum);
        badHost(driver);
        return false;
    }
    if (rootdelay == 0) {
        if (stratum == 1) {
            //tracePrintf("response rootdelay is 0 for stratum 1 host, accepting packet");
        } else {
            tracePrintf1("response rootdelay is 0 for non-stratum-1 host (stratum is %d)", stratum);
            badHost(driver);
            return false;
        }
    }
    rootdispersion = FPTOD(ntohl(response.rootdispersion));
    if (rootdispersion > ES_DISPERSION_REJECT || rootdispersion <= 0) {
        tracePrintf2("response rootdispersion is too large (or nonpositive) at %.3f (stratum is %d)", rootdispersion, stratum);
        badHost(driver);
        return false;
    }

    /*
     * Looks good.	Record info from the packet.
     */

    leap = PKT_LEAP(response.li_vn_mode);  // FIX FIX:  Do something here to account for upcoming or just-past leap-second transitions [stevep 2012 Jun 02: this was an old comment.  But I'm leaving it in for now...]
    _leapBitsFromLastPacket = leap;

    //refid = response.refid;
    NTOHL_FP(&response.reftime, &reftime);
    NTOHL_FP(&response.rec, &rec);
    NTOHL_FP(&response.xmt, &xmt);
    
    /*
     * Make sure the server is at least somewhat sane.
     */
    if (L_ISZERO(&rec)) {
	//[self failed:@"rec is zero"];
        tracePrintf("response rec time zero");
        driver->stateGotBadPacket(this);
	return false;
    }
    
    if (!L_ISHIS(&xmt, &rec) && !L_ISEQU(&xmt, &rec)) {
	//[self failed:@"rec before xmt"];
        tracePrintf("response rec time after xmt");
        driver->stateGotBadPacket(this);
	return false;
    }
    
    l_fp t10, t23, tmp;
    l_fp ci;
    /*
     * Calculate the round trip delay (di) and the clock offset (ci).
     * We use the equations (reordered from those in the spec):
     *
     * d = (t2 - t3) - (t1 - t0)
     * c = ((t2 - t3) + (t1 - t0)) / 2
     // t0 = my       time when I  received the response
     // t1 = server's time when he sent his response
     // t2 = server's time when he received the request
     // t3 = my time  time when I  sent the request     
     */
    t10 = xmt;			/* pkt.xmt == t1 */
    L_SUB(&t10, &recvTimeFP);	/* recv_time == t0*/
    
    t23 = rec;			/* pkt.rec == t2 */
    L_SUB(&t23, &org);		/* pkt->org == t3 */
    
    double d10, d23;
    LFPTOD(&t10, d10);
    LFPTOD(&t23, d23);
    // printf("d10=%7.6f; d23=%7.6f\n", d10, d23);
    
    /* now have (t2 - t3) and (t0 - t1).	Calculate (ci) and (di) */
    /*
     * Calculate (ci) = ((t1 - t0) / 2) + ((t2 - t3) / 2)
     * For large offsets this may prevent an overflow on '+'
     */
    ci = t10;
    L_RSHIFT(&ci);
    tmp = t23;
    L_RSHIFT(&tmp);
    L_ADD(&ci, &tmp);
    
    /*
     * Calculate di in t23 in full precision, then truncate
     * to an s_fp.
     */
    L_SUB(&t23, &t10);
    di = LFPTOFP(&t23);
    
    double offsetThisTime;
    LFPTOD(&ci,offsetThisTime);
    double rttThisTime = FPTOD(di);

#ifndef ES_SURVEY_POOL_HOSTS
    tracePrintf7("Got packet %s (%s)[%d]: prcsn %.8f, rootdelay %.4f, rootdisp %.4f, RTT %.4f",
                 humanReadableIPAddress().c_str(),
                 hostNameDescriptor()->nameAsRequested().c_str(), stratum,
                 precision, rootdelay, rootdispersion, rttThisTime);
#endif

    // Distinguish between hostError, which will never get better for this host, and the rtt, which conceivable could.
    // We want to take the hostError into account when picking the next host to try, but we'll still use this particular piece of data
    // (But as of this writing we're not actually doing anything different with the info...)
    double hostError = (rootdispersion * 1.25) + (rootdelay/2) + precision;
    if (stratum >= ES_BASE_PENALTY_STRATUM) {
        hostError += ES_BASE_STRATUM_PENALTY + (stratum - ES_BASE_PENALTY_STRATUM) * ES_PENALTY_PER_STRATUM;
    }
    double error = (rttThisTime/2) + hostError;
    double minOffset = offsetThisTime - error;
    double maxOffset = offsetThisTime + error;
    if (inPollingMode) {   // In polling mode, the host report shows just the last packet, so that post-leap-second skews aren't influenced by pre-event skews
        _nonRTTErrorFromLastPacket = hostError;
        _skewLB = minOffset;
        _skewUB = maxOffset;
    } else {
        if (minOffset > _skewLB) {
            _skewLB = minOffset;
            _nonRTTErrorFromLastPacket = hostError;
        }
        if (maxOffset < _skewUB) {
            _skewUB = maxOffset;
            _nonRTTErrorFromLastPacket = hostError;
        }
    }

    _packetsUsed++;  // Be optimistic
    _sumRTT += error * 2.0;
    _averageRTT = _sumRTT / _packetsReceived;
#ifdef ES_SURVEY_POOL_HOSTS
    printf("SURVEY: offset: %7.6f => %7.6f (ctr %7.6f), rtt %7.6f (%7.6f), dispersion %7.6f, rootdelay %.6f, precision %.8f, stratum %d, error %7.6f from %s\n",
           minOffset, maxOffset, offsetThisTime, rttThisTime, recvTimeES - xmitTimeES, rootdispersion, rootdelay, precision, stratum, error, humanReadableIPAddress().c_str());
#else
    if (!driver->gotPacket(minOffset, maxOffset, this)) {
        _packetsUsed--;
        driver->makeHostReportIfRequired();  // Do this here, after we bump down the count, not in driver::gotPacket
    }
#endif
    // DON'T DO ANYTHING HERE -- the socket descriptor may have been deleted by driver->gotPacket() if the driver accepted (or if this was a rogue packet)
    return true;
}

std::string
ESNTPSocketDescriptor::humanReadableIPAddress() const {
    char buf[sizeof(_addr)];
    if (_addrlen) {
        if (_family == AF_INET) {
            struct sockaddr_in *inp = (struct sockaddr_in *)&_addr;
            return inet_ntop(_family, (const char *) &inp->sin_addr, buf, sizeof(buf));
        } else if (_family == AF_INET6) {
            struct sockaddr_in6 *inp = (struct sockaddr_in6 *)&_addr;
            return inet_ntop(_family, (const char *) &inp->sin6_addr, buf, sizeof(buf));
        } else {
            return "<unknown>";
        }
    } else {
        return "";
    }
}

void
ESNTPSocketDescriptor::connectAndSendFirstPacket(ESNTPDriver *driver) {
    ESAssert(_hostNameDescriptor->_driver->thread()->inThisThread());
    ESAssert(_fd < 0);

#ifndef ES_SURVEY_POOL_HOSTS
    if (_lastSendFailureTime && (ESTime::currentContinuousTime() - _lastSendFailureTime < ES_SEND_FAILURE_THROTTLE)) {
        driver->stateGotPacketSendError(this);
        return;
    }
#endif

    _fd = socket(_family, _socktype, _protocol);
    if (_fd < 0) {
        ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "Creating socket for NTP server communication");
        _averageRTT = ES_FORCE_RTT_FOR_BAD_HOST;  // "bad host" because might be unusable IPv6 address
        _lastSendFailureTime = ESTime::currentContinuousTime();
        driver->stateGotPacketSendError(this);
        return;
    }
    int st = connect(_fd, (struct sockaddr *)&_addr, _addrlen);
    if (st != 0) {
        ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "Connecting socket for NTP server communication");
        int st = close(_fd);
        if (st != 0) {
            ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "Closing NTP socket");
        }
        _fd = -1;
        _averageRTT = ES_FORCE_RTT_FOR_BAD_HOST;  // "bad host" because might be unusable IPv6 address
        _lastSendFailureTime = ESTime::currentContinuousTime();
        driver->stateGotPacketSendError(this);
        return;
    }
    _hostNameDescriptor->_driver->checkMaxSocketFD(_fd);
    sendPacket(driver, false/* !haveTicket*/);
}

void 
ESNTPSocketDescriptor::closeSocket() {
    ESAssert(_hostNameDescriptor->_driver->thread()->inThisThread());
    if (_timer) {
        _timer->release();
        _timer = NULL;
    }
    if (_fd >= 0) {
        int st = close(_fd);
        if (st != 0) {
            ESErrorReporter::checkAndLogSystemError("ESNTPDriver", errno, "Closing NTP socket");
        }
    }
    _fd = -1;
}

// Sort method used by priority queue
extern bool ESNTPSocketRTTGreaterThan(ESNTPSocketDescriptor *s1,
                                      ESNTPSocketDescriptor *s2);


class ESNTPDriverSleepWakeObserver : public ESUtilSleepWakeObserver {
  public:
                            ESNTPDriverSleepWakeObserver(ESNTPDriver *driver)
    :   _driver(driver)
    {
        ESAssert(ESThread::inMainThread());  // Protect against driver going away while observer active
    }
    virtual void            goingToSleep() {
        ESAssert(ESThread::inMainThread());  // Protect against driver going away while observer active
        _driver->goingToSleep();
    }
    virtual void            wakingUp() {
        ESAssert(ESThread::inMainThread());  // Protect against driver going away while observer active
        _driver->wakingUp();
    }
    virtual void            enteringBackground() {
        ESAssert(ESThread::inMainThread());  // Protect against driver going away while observer active
        _driver->enteringBackground();
    }
    virtual void            leavingBackground() {
        ESAssert(ESThread::inMainThread());  // Protect against driver going away while observer active
        _driver->leavingBackground();
    }

  private:
    ESNTPDriver             *_driver;
};

ESNTPDriver::ESNTPDriver()
:   _maxSocketFD(-1),
    _maxMinOffset(0),
    _minMaxOffset(0),
    _stopThread(false),
    _stopReading(false),
    _reading(false),
    _released(false),
    _lastSuccessfulSyncTime(0),
    _poolHostLookupFailures(0),
    _throttleLastTicketTime(0),
    _throttleSendInterval(0),
    _currentProximity(0),
    _disabled(false),
    _noProgressTimeout(NULL),
    _restartSyncTimeout(NULL),
    _giveUpTimer(NULL),
    _proximityBumpTimer(NULL),
    _networkReachable(true),
    _deviceCountryCode(getAppropriateCountryCode()),  // Depends on .hpp ordering of fields
    _hostNameIterator(_deviceCountryCode.c_str()),
    _availablePoolSocketsByRTT(ESNTPSocketRTTGreaterThan),
    _networkObserver(NULL)
{
    ESAssert(ESThread::inMainThread());
    ESAssert(!_theDriver);   // Only make one of these
    _theDriver = this;
    _thread = new ESNTPThread(this);
    _thread->start();
    _sleepWakeObserver = new ESNTPDriverSleepWakeObserver(this);
    ESUtil::registerSleepWakeObserver(_sleepWakeObserver);
    ESErrorReporter::logInfo("ESNTPDriver", "constructor done");
}

ESNTPDriver::~ESNTPDriver() {
    ESAssert(ESThread::inMainThread());
    ESAssert(!_thread->inThisThread());
    ESAssert(_theDriver == this);
    ESAssert(_released);
    ESAssert(_networkObserver == NULL);  // Arrange for it to be deleted in thread before closing
    ESAssert(_noProgressTimeout == NULL);
    _theDriver = NULL;
    ESUtil::unregisterSleepWakeObserver(_sleepWakeObserver);
    delete _sleepWakeObserver;
}

/*static*/ void 
ESNTPDriver::deleteGlue(void *obj,
                        void *) {
    delete ((ESNTPDriver *)obj);
}

void
ESNTPDriver::releaseInThread() {
    ESAssert(_thread->inThisThread());
    if (_noProgressTimeout) {
        _noProgressTimeout->release();
        _noProgressTimeout = NULL;
    }
    if (_restartSyncTimeout) {
        _restartSyncTimeout->release();
        _restartSyncTimeout = NULL;
    }
    if (_giveUpTimer) {
        _giveUpTimer->release();
        _giveUpTimer = NULL;
    }
    if (_networkObserver) {
        _networkObserver->release();
        _networkObserver = NULL;
    }
    ESThread::callInMainThread(deleteGlue, this, NULL);
}

/*static*/ void 
ESNTPDriver::releaseGlue(void *obj,
                         void *) {
    ((ESNTPDriver *)obj)->releaseInThread();
}

void
ESNTPDriver::release() {
    ESAssert(false);  // Not ready for prime time yet.
    // Release the thread, and let the thread dtor clean up the driver
    ESAssert(ESThread::inMainThread());
    ESAssert(!_thread->inThisThread());
    _thread->callInThread(releaseGlue, this, NULL);
}

/*static*/ void
ESNTPDriver::setPollingInterval(int secs) {
    //ESAssert(!_theDriver);  // Call this before creating any drivers
    inPollingMode = secs > 0;
    _pollingInterval = secs;
}

/*static*/ int
ESNTPDriver::getPollingInterval() {
    return _pollingInterval;
}

/*static*/ std::string
ESNTPDriver::getAppropriateCountryCode() {
    ESAssert(ESThread::inMainThread());
    std::string cc = ESUserPrefs::stringPref("ESNTPDriverCountryCode");
    //ESUtil::noteTimeAtPhase(ESUtil::stringWithFormat("getAppropriateCountryCode from prefs '%s'", cc.c_str()));
    if (cc.length() == 0) {
        cc = ESUtil::localeCountryCode();  // Returns empty string if uninitialized, never NULL
	//ESUtil::noteTimeAtPhase(ESUtil::stringWithFormat("getAppropriateCountryCode from locale '%s'", cc.c_str()));
        ESUserPrefs::setPref("ESNTPDriverCountryCode", cc);
    }
    return cc;
}

#ifdef ES_SURVEY_POOL_HOSTS
static bool poolHostNoResponseTimerFired = false;

class ESPoolHostSurveyNoResponseTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        //ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        printf("ES_SURVEY_POOL_HOSTS noResponseTimerFired\n");
        poolHostNoResponseTimerFired = true;
    }
};

void
ESNTPDriver::runPoolHostSurvey() {
    printf("ES_SURVEY_POOL_HOSTS active: doing the thing!\n");
    
    // Create the no-response timer observer
    ESPoolHostSurveyNoResponseTimerObserver *noResponseTimerObserver = new ESPoolHostSurveyNoResponseTimerObserver;

    // Create a socket
    ESNTPHostNameDescriptor *hostNameDescriptor = new ESNTPHostNameDescriptor;
    ESNTPSocketDescriptor *surveySocket = new ESNTPSocketDescriptor;
    surveySocket->_fd = -1;
    surveySocket->_hostNameDescriptor = hostNameDescriptor;
    surveySocket->_socktype = SOCK_DGRAM;
    surveySocket->_protocol = 17; // UDP

    const int startHostNumber = 0;
    for (int i = startHostNumber; i < numPoolHosts; i++) {
        // Convert the IP string into the socket descriptor parameter section
        const char *ipString = poolHosts[i];
        // ipString = "194.12.244.109";
        if (surveySocket->_fd >= 0) {
            surveySocket->closeSocket();
        }
        struct sockaddr_in *sockp = (struct sockaddr_in *)&surveySocket->_addr;
        surveySocket->_family = PF_INET;
        surveySocket->_addrlen = sizeof(struct sockaddr_in);
        sockp->sin_len = sizeof(struct sockaddr_in);
        sockp->sin_family = AF_INET;
        sockp->sin_port = ES_NTP_PORT_AS_NETWORK_NUMBER;
        int st = inet_pton(AF_INET, ipString, &sockp->sin_addr);
        if (st != 1) {
            struct sockaddr_in6 *sock6p = (struct sockaddr_in6 *)&surveySocket->_addr;
            st = inet_pton(AF_INET6, ipString, &sock6p->sin6_addr);
            surveySocket->_family = PF_INET6;
            surveySocket->_addrlen = sizeof(struct sockaddr_in6);
            sock6p->sin6_len = sizeof(struct sockaddr_in6);
            sock6p->sin6_family = AF_INET6;
            sock6p->sin6_port = ES_NTP_PORT_AS_NETWORK_NUMBER;
        }
        if (st != 1) {
            printf("SURVEY: Index %d host %s: Failure: %s\n", i, ipString, strerror(errno));
            continue;
        }
        // Also, set up the host name descriptor so printouts don't crash
        hostNameDescriptor->init(ipString, 0/*proximity*/, true/*isUserHost*/, this);  // Make it a user host to avoid throttling

        // Set up a no-response timer
        poolHostNoResponseTimerFired = false;
        ESIntervalTimer *noResponseTimer = new ESIntervalTimer(noResponseTimerObserver, 30);  // Long timer to survey long-RTT behavior
        noResponseTimer->setInfo(this);
        noResponseTimer->activate();

        // Send the packet
        surveySocket->sendPacket(this, true/*haveTicket*/);

        int fd = surveySocket->fd();
        if (fd < 0) {
            printf("SURVEY: socket fd negative; skipping wait for recv\n");
            noResponseTimer->release();
            noResponseTimer = NULL;
            continue;
        }

        // Select on the socket (and timer messages) to wait
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(fd, &readers);
        int highestThreadFD = ESThread::setBitsForSelect(&readers);
        int nfds = ESUtil::max(highestThreadFD, _maxSocketFD) + 1;
        select(nfds, &readers, NULL/*writers*/, NULL, NULL);
        if (FD_ISSET(fd, &readers)) {
            // Receive the packet, print the results
            printf("SURVEY: Index %d host %s: Got packet, statistics to follow\n", i, ipString);
            surveySocket->recvPacket(this);
        }
        ESThread::processInterThreadMessages(&readers);
        noResponseTimer->release();
        noResponseTimer = NULL;
    }
}
#endif

class ESNTPPollingHostTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        driver->sendToNextPollingHost();
    }
};
static ESNTPPollingHostTimerObserver *pollingHostTimerObserver = NULL;
static ESTimer *pollingHostTimer = NULL;

void
ESNTPDriver::sendToNextPollingHost() {
    ESAssert(inPollingMode);
    
    // Walk through all hosts.  As each host is encountered, increment host number.
    // When we get to nextSendHostIndex (or the end of the list), we send to that
    // host and increment nextSendHostIndex.
    
    ESTimeInterval timeBeforeNextCall = 0;
    bool anyUnresolvedHosts = false;
    int currentHostIndex = -1;
    for (int n = 0; n < _hostNameDescriptors.size(); n++) {
        const ESNTPHostNameDescriptor *hostNameDescriptor = &_hostNameDescriptors[n];
        int numSockets = hostNameDescriptor->numSocketDescriptors();
        if (numSockets == 0) {
            anyUnresolvedHosts = true;
        }
        for (int i = 0; i < numSockets; i++) {
            ESNTPSocketDescriptor *socketDescriptor = &hostNameDescriptor->_socketDescriptors[i];
            if (++currentHostIndex == nextSendHostIndex) {
                socketDescriptor->sendPacket(this, true/*haveTicket*/);
                nextSendHostIndex++;
                timeBeforeNextCall = POLLING_HOST_GAP;
                _pollingCycleRunning = true;
                break;
            }
            ESAssert(currentHostIndex < nextSendHostIndex);
        }
        if (timeBeforeNextCall) {
            break;
        }
    }
    if (!timeBeforeNextCall) {
        nextSendHostIndex = 0;
        timeBeforeNextCall = anyUnresolvedHosts ? POLLING_INTERVALB : _pollingInterval;
        _pollingCycleRunning = false;
    }
    ESAssert(pollingHostTimerObserver);
    pollingHostTimer = new ESIntervalTimer(pollingHostTimerObserver, timeBeforeNextCall);
    pollingHostTimer->setInfo(this);
    pollingHostTimer->activate();
}

static void restartPollingCycleGlue(void *obj,
                                    void *param) {
    ((ESNTPDriver *)obj)->restartPollingCycle();
}

void
ESNTPDriver::restartPollingCycle() {
    _pollingRunning = true;
    _pollingCycleRunning = true;
    ESAssert(inPollingMode);
    if (!_thread->inThisThread()) {
        _thread->callInThread(restartPollingCycleGlue, this, NULL);
        return;
    }
    if (!pollingHostTimerObserver) {
        pollingHostTimerObserver = new ESNTPPollingHostTimerObserver;
    }
    if (pollingHostTimer) {
        pollingHostTimer->release();
    }
    pollingHostTimer = new ESIntervalTimer(pollingHostTimerObserver, POLLING_INTERVALB);
    pollingHostTimer->setInfo(this);
    pollingHostTimer->activate();
}

static void stopPollingCycleGlue(void *obj,
                                    void *param) {
    ((ESNTPDriver *)obj)->stopPollingCycle();
}

void
ESNTPDriver::stopPollingCycle() {
    _pollingRunning = false;
    _pollingCycleRunning = false;
    ESAssert(inPollingMode);
    if (!_thread->inThisThread()) {
        _thread->callInThread(stopPollingCycleGlue, this, NULL);
        return;
    }
    if (pollingHostTimer) {
        pollingHostTimer->release();
        pollingHostTimer = NULL;
    }
}

bool
ESNTPDriver::pollingRunning() {
    ESAssert(inPollingMode);
    return _pollingRunning;
}

bool
ESNTPDriver::pollingCycleRunning() {
    ESAssert(inPollingMode);
    return _pollingCycleRunning;
}

class ESNTPProximityBumpTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        driver->bumpProximity();
    }
};
static ESNTPProximityBumpTimerObserver *proximityBumpTimerObserver = NULL;

void
ESNTPDriver::bumpProximity() {
    if (_currentProximity < 4) {
        _currentProximity++;
        if (_currentProximity < 3) {
            _proximityBumpTimer = new ESIntervalTimer(proximityBumpTimerObserver, ES_PROXIMITY_LEVEL_TIMEOUT);
            _proximityBumpTimer->setInfo(this);
            _proximityBumpTimer->activate();
        }
    }
}

class ESNTPRestartSyncTimeoutObserver : public ESTimerObserver {
    /*virtual*/ void        notify(ESTimer *timer) {
        if (inPollingMode) {
            return;
        }
        ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        driver->stateGotResyncTimeout();
    }
};
static ESNTPRestartSyncTimeoutObserver *restartSyncTimeoutObserver = NULL;

void
ESNTPDriver::giveUpTimerFire() {
    if (_restartSyncTimeout) {
        _restartSyncTimeout->release();
    }
    if (!restartSyncTimeoutObserver) {
        restartSyncTimeoutObserver = new ESNTPRestartSyncTimeoutObserver;
    }
    if (_lastFailTimeout > 0) {
        _lastFailTimeout *= ES_FIX_TIMEOUT_FAILED_BACKOFF;
        if (_lastFailTimeout > ES_FIX_TIMEOUT_FAILED_MAX) {
            _lastFailTimeout = ES_FIX_TIMEOUT_FAILED_MAX;
        }
    } else {
        _lastFailTimeout = ES_FIX_TIMEOUT_FAILED;
    }
    _restartSyncTimeout = new ESIntervalTimer(restartSyncTimeoutObserver, _lastFailTimeout);
    _restartSyncTimeout->setInfo(this);
    _restartSyncTimeout->activate();
    tracePrintf1("Too much time elapsed since starting sync; giving up and setting timer for %d seconds", _lastFailTimeout);
    // Set time source status based on whether we were synchronized before or not
    switch(_status) {
      case ESTimeSourceStatusNoNetSynchronized:
      case ESTimeSourceStatusPollingSynchronized:
      case ESTimeSourceStatusFailedSynchronized:
      case ESTimeSourceStatusSynchronized:  // Not sure how we'd get here with this state, but...
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
    stopSyncingInThisThread();
}

class ESNTPGiveUpTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        if (inPollingMode) {
            return;
        }
        ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        driver->giveUpTimerFire();
    }
};
static ESNTPGiveUpTimerObserver *giveUpTimerObserver = NULL;

void
ESNTPDriver::startSyncing(bool userRequested) {  // The name of this parameter is wrong -- should be "old status still valid"
    ESAssert(_thread->inThisThread());

    if (_disabled) {
        tracePrintf("NTP disabled, not synchronizing");
        return;
    }

#ifdef ES_SURVEY_POOL_HOSTS
    runPoolHostSurvey();
    return;
#endif

    tracePrintf2("startSyncing, userRequested=%s, already running=%s", userRequested ? "true" : "false", _lastSyncStart ? "true" : "false");

    if (_lastSyncStart) {
        // already running, nothing to do
        return;
    }

    if (!_networkReachable) {
        // wait for the notification that it's back up
        tracePrintf("network is down, not syncing");
        return;
    }

    _lastSyncStart = ESTime::currentContinuousTime();

    // Here, everything is stopped
    _stopReading = false;

    tracePrintf1("resync status on entry %s\n", ESTime::currentStatusEngrString().c_str());

    ESTimeSourceStatus newStatus;
    switch(_status) {
      case ESTimeSourceStatusUnsynchronized:
      case ESTimeSourceStatusPollingUnsynchronized:
      case ESTimeSourceStatusFailedUnynchronized:
      case ESTimeSourceStatusNoNetUnsynchronized:
      default:
        newStatus = ESTimeSourceStatusPollingUnsynchronized;
        break;
      case ESTimeSourceStatusSynchronized:
      case ESTimeSourceStatusPollingSynchronized:
      case ESTimeSourceStatusFailedSynchronized:
      case ESTimeSourceStatusNoNetSynchronized:
        if (userRequested) {
            newStatus = ESTimeSourceStatusPollingSynchronized;
        } else {
            newStatus = ESTimeSourceStatusPollingUnsynchronized;
        }
        break;
      case ESTimeSourceStatusPollingRefining:
      case ESTimeSourceStatusRefining:
      case ESTimeSourceStatusFailedRefining:
        if (userRequested) {
            newStatus = ESTimeSourceStatusPollingRefining;
        } else {
            newStatus = ESTimeSourceStatusPollingUnsynchronized;
        }
        break;
    }
    setStatus(newStatus);
    tracePrintf1("set new status %s\n", ESTime::currentStatusEngrString().c_str());
    ESTimeInterval effectiveError = userRequested ? _currentTimeError : 1e9;
    tracePrintf2("setting contSkew %.3f with effective error %.1f\n", _contSkew, effectiveError);
    setContSkew(_contSkew, effectiveError);  // In case anybody's watching
    ESTime::workingSyncValue(-1, -1);
    _maxMinOffset = -1E9;
    _minMaxOffset = 1E9;

    _maxSocketFD = -1;

    _poolHostLookupFailures = 0;
    _lastFailTimeout = 0;
    _numPacketsSent = 0;
    _numPacketsReceived = 0;
    _numLongTimeoutsTriggered = 0;
    _throttleLastTicketTime = 0;
    _throttleSendInterval = 0;

    if (_giveUpTimer) {
        _giveUpTimer->release();
        _giveUpTimer = NULL;  // for clarity
    }
    if (!giveUpTimerObserver) {
        giveUpTimerObserver = new ESNTPGiveUpTimerObserver;
    }
    _giveUpTimer = new ESIntervalTimer(giveUpTimerObserver, ES_GIVE_UP_INTERVAL);
    _giveUpTimer->setInfo(this);
    _giveUpTimer->activate();
    if (_restartSyncTimeout) {
        _restartSyncTimeout->release();
        _restartSyncTimeout = NULL;
    }
    
    _currentProximity = 0;
    if (_proximityBumpTimer) {
        _proximityBumpTimer->release();
        _proximityBumpTimer = NULL;  // for clarity
    }
    if (!proximityBumpTimerObserver) {
        proximityBumpTimerObserver = new ESNTPProximityBumpTimerObserver;
    }
    _proximityBumpTimer = new ESIntervalTimer(proximityBumpTimerObserver, ES_PROXIMITY_LEVEL_TIMEOUT);
    _proximityBumpTimer->setInfo(this);
    _proximityBumpTimer->activate();
    if (inPollingMode) {
        _currentProximity = 4;
        restartPollingCycle();
    }

    _hostNameDescriptors.resize(_hostNameIterator.totalNumberOfHostNames());

    ESNetwork::startNetworkActivityIndicator();

    int i = 0;
    bool firstNonUserResolved = false;
    while (_hostNameIterator.next()) {
        bool isUserHost = _hostNameIterator.isUserHost();
        ESNTPHostNameDescriptor *hostNameDescriptor = &_hostNameDescriptors[i];
        hostNameDescriptor->init(_hostNameIterator.host(),
                                 _hostNameIterator.proximity(),
                                 isUserHost,
                                 this);
        if (isUserHost || inPollingMode) {
            hostNameDescriptor->startResolving();
        } else {
            if (!firstNonUserResolved) {
                hostNameDescriptor->startResolving();
                firstNonUserResolved = true;
            }
        }
        i++;
    }
    ESAssert(i == _hostNameIterator.totalNumberOfHostNames());
    ESAssert(i == _hostNameDescriptors.size());
    makeHostReportIfRequired();
}

void 
ESNTPDriver::checkMaxSocketFD(int fd) {
    if (fd > _maxSocketFD) {
        _maxSocketFD = fd;
    }
}

void
ESNTPDriver::setBitsForSocketFDs(fd_set *fdset) {
    for (int i = 0; i < _hostNameDescriptors.size(); i++) {
        _hostNameDescriptors[i].setBitsForSocketFDs(fdset);
    }
}

void
ESNTPDriver::processRemoteSockets(fd_set *fdset) {
    ESAssert(_thread->inThisThread());
    _reading = true;
    for (int i = 0; i < _hostNameDescriptors.size(); i++) {
        _hostNameDescriptors[i].processRemoteSockets(fdset);
    }
    _reading = false;
    if (_stopReading) {
        _stopReading = false;
        stopSyncingInThisThread();
    }
}

void *
ESNTPDriver::threadMain() {
    ESAssert(_thread->inThisThread());

    ESErrorReporter::logInfo("ESNTPDriver", "NTPDriver thread main");
    tracePrintf("ESNTPDriver testing trace");

    // startSyncing(false/* !userRequested*/);

    //ESErrorReporter::logInfo("ESNTPDriver", "NTPDriver done starting all hosts");

    _stopThread = false;
    while (!_stopThread) {
        fd_set readers;
        FD_ZERO(&readers);
        setBitsForSocketFDs(&readers);
        int highestThreadFD = ESThread::setBitsForSelect(&readers);
        int nfds = ESUtil::max(highestThreadFD, _maxSocketFD) + 1;
        select(nfds, &readers, NULL/*writers*/, NULL, NULL);
        processRemoteSockets(&readers);
        ESThread::processInterThreadMessages(&readers);
    }

    return NULL;
}

bool 
ESNTPDriver::inDriverThread() {
    return _thread && _thread->inThisThread();
}

static void stopSyncingIfNecessaryGlue(void *obj,
                                       void *param) {
    ((ESNTPDriver *)obj)->stopSyncingIfNecessaryInThisThread();
}

void
ESNTPDriver::stopSyncingIfNecessary() {
    if (_lastSyncStart) {
        _thread->callInThread(stopSyncingIfNecessaryGlue, this, NULL);
    }    
}

void 
ESNTPDriver::goingToSleep() {
    ESAssert(ESThread::inMainThread());
#if !ES_ANDROID
    tracePrintf("NTP driver goingToSleep");
    if (_lastSyncStart) {
        if (_thread) {
            _thread->callInThread(stopSyncingIfNecessaryGlue, this, NULL);
        }
    }
#endif
}

// static void startSyncingGlue(void *obj,
//                              void *param) {
//     ESNTPDriver *driver = (ESNTPDriver *)obj;
//     bool userRequested = (bool)param;
//     ESAssert(driver->inDriverThread());
//     driver->startSyncing(userRequested);    
// }

void 
ESNTPDriver::wakingUp() {
    ESAssert(ESThread::inMainThread());
    // Don't need to resync here:  ESTime::continuousTimeReset does it (as it is common to all 2-time-base implementations)
    //_thread->callInThread(startSyncingGlue, this, (void *)false/* !userRequested*/);
}

static void bgTaskExpirationHandler() {
    ESAssert(ESThread::inMainThread());
    // If there were a way to *quickly* clean up here we would.  But we're already quickly cleaning up...
    // So do nothing.
    ESAssert(false);  // In normal cases, we should never see this -- it shouldn't take us that long to shut down
    tracePrintf("bgTaskExpirationHandler doing nothing");
}

void
ESNTPDriver::enteringBackgroundInThread(ESBackgroundTaskID bgTaskID) {
    ESAssert(_thread->inThisThread());
    bool alreadyStopped = (_lastSyncStart == 0);
    tracePrintf2("enteringBackgroundInThread %u, %s", (unsigned int)bgTaskID, alreadyStopped ? "already stopped" : "still running");
    if (!alreadyStopped) {
        stopSyncingInThisThread();
    }
    ESUtil::declareBackgroundTaskFinished(bgTaskID);
}

static void enteringBackgroundGlue(void *obj,
                                   void *param) {
    ESNTPDriver *driver = (ESNTPDriver *)obj;
    ESBackgroundTaskID bgTaskID = (ESBackgroundTaskID)(ESPointerSizedInt)param;
    driver->enteringBackgroundInThread(bgTaskID);
}

void 
ESNTPDriver::enteringBackground() {
    ESAssert(ESThread::inMainThread());
    if (_thread) {
        ESBackgroundTaskID bgTaskID = ESUtil::requestMoreTimeForBackgroundTask(bgTaskExpirationHandler);
        if (bgTaskID) {
            _thread->callInThread(enteringBackgroundGlue, this, (void*)bgTaskID);
        }
    }
}

void 
ESNTPDriver::leavingBackground() {
    ESAssert(ESThread::inMainThread());
}

/*static*/ ESTimeSourceDriver *
ESNTPDriver::makeOne() {   // This method can be used as an ESTimeSourceDriverMaker*
    return new ESNTPDriver;
}

void
ESNTPDriver::resyncInThisThread(bool userRequested) {
    ESAssert(_thread->inThisThread());
    if (_lastSyncStart != 0) {
        stopSyncingInThisThread();
    }
    startSyncing(userRequested);
}

static void resyncGlue(void *obj,
                       void *param) {
    ((ESNTPDriver *)obj)->resyncInThisThread((bool)param);
}

/*virtual*/ void 
ESNTPDriver::resync(bool userRequested) {
    if (!_thread->inThisThread()) {
        _thread->callInThread(resyncGlue, this, (void*)userRequested);
    } else {
        resyncInThisThread(userRequested);
    }
}

void
ESNTPDriver::disableSyncInThisThread() {
    _disabled = true;
    stopSyncingIfNecessaryInThisThread();
}

static void disableSyncGlue(void *obj,
                            void *param) {
    ((ESNTPDriver *)obj)->disableSyncInThisThread();
}

/*virtual*/ void 
ESNTPDriver::disableSync() {
    if (!_thread->inThisThread()) {
        _thread->callInThread(disableSyncGlue, this, NULL);
    } else {
        disableSyncInThisThread();
    }
}

void
ESNTPDriver::enableSyncInThisThread() {
    _disabled = false;
    resyncInThisThread(false);
}

static void enableSyncGlue(void *obj,
                            void *param) {
    ((ESNTPDriver *)obj)->enableSyncInThisThread();
}

/*virtual*/ void 
ESNTPDriver::enableSync() {
    if (!_thread->inThisThread()) {
        _thread->callInThread(enableSyncGlue, this, NULL);
    } else {
        enableSyncInThisThread();
    }
}

void
ESNTPDriver::closeAllSockets() {
    ESAssert(_thread->inThisThread());
    for (int i = 0; i < _hostNameDescriptors.size(); i++) {
        _hostNameDescriptors[i].closeAllSockets();
    }
}

static ESLock syncReportLock;

/*static*/ int 
ESNTPDriver::numPacketsSent() {
    return _numPacketsSent;
}

/*static*/ int 
ESNTPDriver::numPacketsReceived() {
    return _numPacketsReceived;
}

/*static*/ ESTimeInterval
ESNTPDriver::lastSyncElapsedTime() {
    syncReportLock.lock();
    ESTimeInterval returnValue;
    if (_lastSyncStart == 0) {
        returnValue = _lastSyncElapsedTime;
    } else {
        returnValue = ESTime::currentContinuousTime() - _lastSyncStart;
    }
    syncReportLock.unlock();
    return returnValue;
}

/*static*/ double
ESNTPDriver::avgPacketsSent() {
    syncReportLock.lock();
    double returnValue;
    // Don't include the current run in the average -- it will artificially lower the result
    if (_numSyncs == 0) {
        returnValue = 0;
    } else {
        returnValue = (double)_sumNumPacketsSent / _numSyncs;
    }
    syncReportLock.unlock();
    return returnValue;
}

/*static*/ ESTimeInterval
ESNTPDriver::timeOfLastSuccessfulSync() {
    return _timeOfLastSuccessfulSync;
}

/*static*/ ESTimeInterval
ESNTPDriver::timeOfLastSyncAttempt() {
    if (_lastSyncStart == 0) {
        return _timeOfLastSyncAttempt;
    } else {
        return ESTime::currentTime();
    }
}

/*static*/ ESTimeInterval
ESNTPDriver::avgSyncElapsedTime() {
    syncReportLock.lock();
    ESTimeInterval returnValue;
    // Don't include the current run in the average -- it will artificially lower the result
    if (_numSyncs == 0) {
        returnValue = 0;
    } else {
        returnValue = _sumLastSyncElapsedTime / _numSyncs;
    }
    syncReportLock.unlock();
    return returnValue;
}

void
ESNTPDriver::stopSyncingInThisThread() {
    ESAssert(_thread->inThisThread());
    if (_reading) {
        _stopReading = true;  // In case we're in the middle of reading
        return;
    } else {
        if (_lastSyncStart) {
            syncReportLock.lock();
            _lastSyncElapsedTime = ESTime::currentContinuousTime() - _lastSyncStart;
            _timeOfLastSyncAttempt = ESTime::currentTime();
            tracePrintf3("stopSyncing after sending %d packet%s, synchronization took %.2f seconds",
                         _numPacketsSent, _numPacketsSent == 1 ? "" : "s", _lastSyncElapsedTime);
            _lastSyncStart = 0;
            _numSyncs++;
            _sumLastSyncElapsedTime += _lastSyncElapsedTime;
            _sumNumPacketsSent += _numPacketsSent;
            syncReportLock.unlock();
        } else {
            ESAssert(false);  // This happens but I don't know why so I'm putting this in to find out...
            tracePrintf2("stopSyncing after sending %d packet%s, sync stop without corresponding start?",
                         _numPacketsSent, _numPacketsSent == 1 ? "" : "s");
        }
        closeAllSockets();
    }
    ESNetwork::stopNetworkActivityIndicator();    
    if (_noProgressTimeout) {
        _noProgressTimeout->release();
        _noProgressTimeout = NULL;
    }
    if (_giveUpTimer) {
        _giveUpTimer->release();
        _giveUpTimer = NULL;
    }
    if (_proximityBumpTimer) {
        _proximityBumpTimer->release();
        _proximityBumpTimer = NULL;  // for clarity
    }
    if (inPollingMode) {
        stopPollingCycle();
    }
    ESTime::workingSyncValue(-1, -1);
    // Most of this is redundant with what happens in startSyncing():
    _throttleSendInterval = 0;
    _hostNameIterator.reset();
    _hostNameDescriptors.clear();
    // Gack.  STL priority_queues have no clear() method?  Why would anyone want that? </sarcasm>
    // ... so we assign an empty queue to it.  Yeah, much better. </sarcasm>
    _availablePoolSocketsByRTT = std::priority_queue<ESNTPSocketDescriptor*, std::vector<ESNTPSocketDescriptor *>, bool (*)(ESNTPSocketDescriptor *, ESNTPSocketDescriptor *)>(ESNTPSocketRTTGreaterThan);
}

static void stopSyncingGlue(void *obj,
                            void *param) {
    ((ESNTPDriver *)obj)->stopSyncingInThisThread();
}

/*virtual*/ void 
ESNTPDriver::stopSyncing() {
    if (!_thread->inThisThread()) {
        _thread->callInThread(stopSyncingGlue, this, NULL);
    } else {
        stopSyncingInThisThread();
    }
}

void 
ESNTPDriver::stopSyncingIfNecessaryInThisThread() {
    ESAssert(_thread->inThisThread());
    if (_lastSyncStart) {
        stopSyncingInThisThread();
    }
}

/*virtual*/ std::string 
ESNTPDriver::userName() {
    return "NTP";
}

/*virtual*/ void
ESNTPDriver::setDeviceCountryCodeInThread(const char *countryCode) {
    ESAssert(_thread->inThisThread());
    //ESUtil::noteTimeAtPhase(ESUtil::stringWithFormat("setDeviceCountryCodeInThread %s", countryCode));
    _hostNameIterator.reset(countryCode);
    resyncInThisThread(false/* !userRequested*/);
}

static void setDeviceCountryCodeGlue(void *obj,
                                     void *param) {
    ESNTPDriver *driver = (ESNTPDriver *)obj;
    const char *countryCode = (const char *)&param;
    driver->setDeviceCountryCodeInThread(countryCode);
}

/*virtual*/ void 
ESNTPDriver::setDeviceCountryCode(const char *countryCode) {
    ESAssert(ESThread::inMainThread());
    //ESUtil::noteTimeAtPhase(ESUtil::stringWithFormat("setDeviceCountryCode %s", countryCode));
    if (strcasecmp(countryCode, _deviceCountryCode.c_str()) != 0) {
        _deviceCountryCode = countryCode;
        ESUserPrefs::setPref("ESNTPDriverCountryCode", _deviceCountryCode);
        //ESUtil::noteTimeAtPhase(ESUtil::stringWithFormat("setDeviceCountryCode '%s' is new", countryCode));
        if (strlen(countryCode) == 2) {  // All of the ones we know about are two characters.  Anything else won't help...
            // Hack:  Inter-thread messages can't pass strings, but a country code is only two characters,
            // so pass those two characters as a pointer value </Hack>
            void *phony;
            ESAssert(sizeof(phony) > 3);
            bcopy(countryCode, &phony, 3);  // copy the two characters and the NULL, while avoiding any alignment issues
            _thread->callInThread(setDeviceCountryCodeGlue, this, phony);
        }
    }
}

void
ESNTPDriver::nameResolutionFailed(ESNTPHostNameDescriptor *hostNameDescriptor,
                                  const std::string       &hostName,
                                  int                     failureStatus) {
    ESAssert(_thread->inThisThread());
    makeHostReportIfRequired();
    std::string msg = "Error resolving NTP host name: '";
    msg += hostName;
    msg += '\'';
    ESErrorReporter::checkAndLogGAIError("ESNTPDriver", failureStatus, msg.c_str());
    if (!_networkObserver) {
        ESUtil::noteTimeAtPhase("asking about network");
        bool networkReachable = ESNetwork::internetIsReachable();
        ESUtil::noteTimeAtPhase("done asking about network");
        tracePrintf1("The network %s available", networkReachable ? "IS" : "IS NOT");
        _networkObserver = new ESNTPDriverNetworkObserver(this, networkReachable);
        if (!networkReachable) {
            stateGotNetworkUnreachable(true/*knownToBeActive*/);
            return;
        }
    }
    stateGotNameResolutionFailure(hostNameDescriptor);
}

void 
ESNTPDriver::nameResolutionComplete(ESNTPHostNameDescriptor *hostNameDescriptor) {
    makeHostReportIfRequired();
    stateGotNameResolutionComplete(hostNameDescriptor);
}

void
ESNTPDriver::logAllHostAddresses(bool       includeNoPacketHosts,
                                 const char *introMessage) {
    ESAssert(_thread->inThisThread());
    if (_hostNameDescriptors.size() == 0) {
        ESErrorReporter::logInfo("ESNTPDriver", "No hosts defined");
        return;
    }
    if (introMessage == NULL) {
        if (includeNoPacketHosts) {
            introMessage = "All hosts:";
        } else {
            introMessage = "All hosts with valid packets:";
        }
    }
    ESErrorReporter::logInfo("ESNTPDriver", introMessage);
    for (int n = 0; n < _hostNameDescriptors.size(); n++) {
        const ESNTPHostNameDescriptor *hostNameDescriptor = &_hostNameDescriptors[n];
        int numSockets = hostNameDescriptor->numSocketDescriptors();
        if (numSockets == 0 && includeNoPacketHosts) {
            ESErrorReporter::logInfo("ESNTPDriver", ESUtil::stringWithFormat(" <not resolved>  (%s)", hostNameDescriptor->nameAsRequested().c_str()).c_str());
        } else {
            for (int i = 0; i < numSockets; i++) {
                const ESNTPSocketDescriptor *socketDescriptor = &hostNameDescriptor->_socketDescriptors[i];
                if (socketDescriptor->packetsUsed() > 0 || includeNoPacketHosts) {
                    ESErrorReporter::logInfo("ESNTPDriver", ESUtil::stringWithFormat(" %15s (%s) used %d packet(s)",
                                                                                     socketDescriptor->humanReadableIPAddress().c_str(),                                                                               
                                                                                     hostNameDescriptor->nameAsRequested().c_str(),
                                                                                     socketDescriptor->packetsUsed()) .c_str());
                }
            }
        }
    }
}

void 
ESNTPDriver::incrementNumPacketsSent() {
    _numPacketsSent++;
}

void 
ESNTPDriver::incrementNumPacketsReceived() {
    _numPacketsReceived++;
}

// Return true iff we're using the packet
bool 
ESNTPDriver::gotPacket(ESTimeInterval        minOffset,
                       ESTimeInterval        maxOffset,
                       ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    if (minOffset > _maxMinOffset) {
        _maxMinOffset = minOffset;
    }
    if (maxOffset < _minMaxOffset) {
        _minMaxOffset = maxOffset;
    }
    tracePrintf6("%s (%s) got packet min/max %.3f %.3f (rtt %.3f, avg rtt %.3f)",
                 socketDescriptor->humanReadableIPAddress().c_str(),
                 socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str(),
                 minOffset, maxOffset, (maxOffset - minOffset), socketDescriptor->averageRTT());
    tracePrintf6("%s (%s) ... cumulativeMinMax %.3f %.3f (bound %.3f, err %.3f)",
                 socketDescriptor->humanReadableIPAddress().c_str(),
                 socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str(),
                 _maxMinOffset, _minMaxOffset, _minMaxOffset - _maxMinOffset, (_minMaxOffset - _maxMinOffset)/2.0);

    if (_minMaxOffset < _maxMinOffset) {
        std::string str = ESUtil::stringWithFormat("Got non-overlapping packet data from host %s (%s) (the error might be from another host though):",
                                                   socketDescriptor->humanReadableIPAddress().c_str(),
                                                   socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str());
        if (inPollingMode) {
            makeHostReportIfRequired();  // Do this here, after we bump down the count, not in driver::gotPacket
            return true;
        }
        ESErrorReporter::logError("ESNTPDriver", str.c_str());
        logAllHostAddresses(false/* !includeNoPacketHosts*/,
                            "Hosts which have contributed to the cumulative result follow:");
        ESErrorReporter::logInfo("ESNTPDriver", "Restarting because of non-overlapping packet data");
        resyncInThisThread(false/* !userRequested*/);
        return true;  // we're not "using" this packet but this will prevent a deleted socket from referencing itself
    }
    double currentError =  (_minMaxOffset - _maxMinOffset) / 2;
    double tentativeSkew = (_minMaxOffset + _maxMinOffset) / 2;

    //printf("..cumulative skew %7.6f error %7.6f (current skew %7.6f)\n", tentativeSkew, currentError, _contSkew);

    if (currentError < ES_FIX_GOOD) {
        // We're done.  Shut down.
#ifdef ESTRACE
        //logAllHostAddresses(true/*includeNoPacketHosts*/, "Hosts active during successful sync:");
#endif
        tracePrintf("...so we're done!!");
        makeHostReportIfRequired();  // Do this before stopping the sync otherwise the array of descriptors will be already gone
        setContSkew(tentativeSkew, currentError);
        setStatus(ESTimeSourceStatusSynchronized);
        _timeOfLastSuccessfulSync = ESTime::currentTime();
        _timeOfLastSyncAttempt = _timeOfLastSuccessfulSync;
#if ES_TRIPLEBASE
        ESTimeCalibrator::notifySuccessfulSkew(tentativeSkew, currentError);
#endif
        if (inPollingMode) {
            return true;
        }
        stateGotFinishingPacket();
        return true;
    } else {
        // There was some code here which attempted to give weight to the old value on the theory that it would reduce jiggling
        // but it meant that the error was higher than it would be if we just took the midpoint all of the time, so we're doing
        // that now.

        // We do want to avoid changing the skew when the old error bound is better than the new one, and the new range
        // encompasses the old one.  But in no case do we use the old skew value, because we can report a better error if we
        // use the new one.
        double minSkew = tentativeSkew - currentError;
        double maxSkew = tentativeSkew + currentError;
        if (_contSkew < minSkew) {
            tracePrintf("...so (because old skew is known to be too low) reporting refining status and refining skew");
            setContSkew(tentativeSkew, currentError);
        } else if (_contSkew > maxSkew) {
            //printf("reporting refining skew...\n");
            tracePrintf("...so (because old skew is known to be too high) reporting refining status and refining skew");
            setContSkew(tentativeSkew, currentError);
        } else if (currentError < _currentTimeError) {  // Better off with new value because we can make the error bound smaller
            tracePrintf("...so (because new error is better than old error)) reporting refining status and refining skew");
            setContSkew(tentativeSkew, currentError);
        }
        ESTime::workingSyncValue(tentativeSkew, currentError);
        //printf("reporting refining...\n");
        setStatus(ESTimeSourceStatusRefining);
        makeHostReportIfRequired();  // Rule is we do this if we return true, but not if we return false (we have to do it because of the case above)
        if (inPollingMode) {
            return true;
        }
        stateGotPacketWithReportableSync(socketDescriptor);
        return true;
    }
}

void 
ESNTPDriver::pushPoolSocket(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    tracePrintf3("Pushing pool socket %s (%s) with averageRTT %.4f",
                 socketDescriptor->humanReadableIPAddress().c_str(),
                 socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str(),
                 socketDescriptor->averageRTT());
    _availablePoolSocketsByRTT.push(socketDescriptor);
}

void
ESNTPDriver::sendPacketToFastestPoolHost() {
    ESAssert(_thread->inThisThread());
    if (_availablePoolSocketsByRTT.size() == 0) {
        resolveOnePoolHostDNS();
        return;
    }
    ESNTPSocketDescriptor *fastestPoolSocket = _availablePoolSocketsByRTT.top();
    if (fastestPoolSocket->averageRTT() >= ES_FORCE_RTT_FOR_BAD_HOST) {
        // All that's left are bad hosts -- let's try adding some more
        if (resolveOnePoolHostDNS()) {
            return;
        }
    }
    _availablePoolSocketsByRTT.pop();
    tracePrintf3("Popping pool socket %s (%s) with averageRTT %.4f",
                 fastestPoolSocket->humanReadableIPAddress().c_str(),
                 fastestPoolSocket->hostNameDescriptor()->nameAsRequested().c_str(),
                 fastestPoolSocket->averageRTT());
    fastestPoolSocket->sendPacket(this, false/* !haveTicket*/);
}

void
ESNTPDriver::sendPacketToFastestPoolHostIncludingThisOne(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    if (_availablePoolSocketsByRTT.size() == 0) {
        socketDescriptor->sendPacket(this, false/* !haveTicket*/);
        return;
    }
    ESNTPSocketDescriptor *fastestPoolSocket = _availablePoolSocketsByRTT.top();
    if (fastestPoolSocket->averageRTT() < socketDescriptor->averageRTT()) {
        tracePrintf3("Popping pool socket %s (%s) with averageRTT %.4f",
                     fastestPoolSocket->humanReadableIPAddress().c_str(),
                     fastestPoolSocket->hostNameDescriptor()->nameAsRequested().c_str(),
                     fastestPoolSocket->averageRTT());
        _availablePoolSocketsByRTT.pop();
        tracePrintf3("...and pushing pool socket %s (%s) with averageRTT %.4f",
                     socketDescriptor->humanReadableIPAddress().c_str(),
                     socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str(),
                     socketDescriptor->averageRTT());
        _availablePoolSocketsByRTT.push(socketDescriptor);
        fastestPoolSocket->sendPacket(this, false/* !haveTicket*/);
    } else {
        tracePrintf4("Top pool socket %s (%s) with averageRTT %.4f is worse than reference at %.4f",
                     fastestPoolSocket->humanReadableIPAddress().c_str(),
                     fastestPoolSocket->hostNameDescriptor()->nameAsRequested().c_str(),
                     fastestPoolSocket->averageRTT(),
                     socketDescriptor->averageRTT());
        socketDescriptor->sendPacket(this, false/* !haveTicket*/);
    }
}

// return true if it might help to call this again
bool
ESNTPDriver::ensureFreshHostAvailable() {
    ESAssert(_thread->inThisThread());
    if (_availablePoolSocketsByRTT.size() == 0) {
        return resolveOnePoolHostDNS();
    }
    ESNTPSocketDescriptor *fastestPoolSocket = _availablePoolSocketsByRTT.top();
    if (fastestPoolSocket->averageRTT() != 0) {
        return resolveOnePoolHostDNS();
    }
    return true;
}

// May do nothing if we've already tried to resolve all hosts
bool
ESNTPDriver::resolveOnePoolHostDNS() {
    ESAssert(_thread->inThisThread());
    for (int n = 0; n < _hostNameDescriptors.size(); n++) {
        ESNTPHostNameDescriptor *hostNameDescriptor = &_hostNameDescriptors[n];
        if (hostNameDescriptor->_proximity > _currentProximity) {
            return false;
        }
        if (!hostNameDescriptor->_resolving && !hostNameDescriptor->_doneResolving) { // _doneResolving is set even on failure for just this reason
            // virgin host -- never resolved: resolve it!
            hostNameDescriptor->startResolving();
            return true;
        }
    }
    return false;
}

class ESNTPNoProgressTimeoutObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPDriver *driver = (ESNTPDriver *)timer->info();
        driver->stateGotNoProgressTimeout();
    }
};
static ESNTPNoProgressTimeoutObserver *noProgressTimeoutObserver = NULL;

void 
ESNTPDriver::installNoProgressTimeout() {
    if (!noProgressTimeoutObserver) {
        noProgressTimeoutObserver = new ESNTPNoProgressTimeoutObserver;
    }
    if (_noProgressTimeout) {
        _noProgressTimeout->release();  // Avoid leak
    }
    _noProgressTimeout = new ESIntervalTimer(noProgressTimeoutObserver,
                                             ES_NO_PROGRESS_TIMEOUT
                                             + _throttleSendInterval); // heuristic
    _noProgressTimeout->setInfo(this);
    _noProgressTimeout->activate();
}

ESTimeInterval 
ESNTPDriver::timeSinceLastSuccessfulSynchronization() {
    return ESTime::currentContinuousTime() - _lastSuccessfulSyncTime;
}

// *****************************************************************************
// ESNTPDriver (possible) state transition triggers
// *****************************************************************************

void
ESNTPDriver::stateGotFinishingPacket() {
    ESAssert(_thread->inThisThread());
    if (_restartSyncTimeout) {
        _restartSyncTimeout->release();
    }
    if (!restartSyncTimeoutObserver) {
        restartSyncTimeoutObserver = new ESNTPRestartSyncTimeoutObserver;
    }
    _lastSuccessfulSyncTime = ESTime::currentContinuousTime();
    _lastFailTimeout = 0;
    _restartSyncTimeout = new ESIntervalTimer(restartSyncTimeoutObserver, ES_FIX_TIMEOUT);
    _restartSyncTimeout->setInfo(this);
    _restartSyncTimeout->activate();
    stopSyncingInThisThread();
}

void
ESNTPDriver::stateGotPacketSendError(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    tracePrintf2("%s (%s) got packet send error, delegating to stateGotBadPacket",
                 socketDescriptor->humanReadableIPAddress().c_str(),
                 socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str());
    stateGotBadPacket(socketDescriptor);  // For now
}

ESTimeInterval
ESNTPDriver::getThrottleTicket(ESTimeInterval sendTime) {
    ESAssert(_thread->inThisThread());
    if (_numPacketsReceived < ES_THROTTLE_BASE_PACKETS) {  // Use numPacketsReceived; if packets are disappearing they aren't pounding on the pool hosts (probably)
        ESAssert(_throttleSendInterval == 0);
        _throttleSendInterval = 0;
        _throttleLastTicketTime = sendTime;
    } else {
        if (_numPacketsReceived == ES_THROTTLE_BASE_PACKETS) {
            _throttleSendInterval = ES_THROTTLE_BASE_INTERVAL;
        } else {
            _throttleSendInterval *= ES_THROTTLE_INTERVAL_MULTIPLIER;
            if (_throttleSendInterval > ES_THROTTLE_INTERVAL_MAX) {
                _throttleSendInterval = ES_THROTTLE_INTERVAL_MAX;
            }
        }
        _throttleLastTicketTime += _throttleSendInterval;
    }
    return _throttleLastTicketTime;
}

void
ESNTPDriver::stateGotPacketWithReportableSync(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    if (socketDescriptor->_hostNameDescriptor->_isUserHost) {
        // Just resend : All user hosts are active all of the time
        socketDescriptor->sendPacket(this, false/* !haveTicket*/);
    } else {
        sendPacketToFastestPoolHostIncludingThisOne(socketDescriptor);
    }
}

void
ESNTPDriver::stateGotPacketWithUnreportableSync(ESNTPSocketDescriptor *socketDescriptor,
                                                ESTimeInterval        rtt) {
    ESAssert(_thread->inThisThread());
    if (socketDescriptor->_hostNameDescriptor->_isUserHost) {
        // Just resend : All user hosts are active all of the time
        socketDescriptor->sendPacket(this, false/* !haveTicket*/);
    } else {
        // We didn't make progress.
        sendPacketToFastestPoolHostIncludingThisOne(socketDescriptor);
    }
}

void 
ESNTPDriver::stateGotBadPacket(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    static bool recursing = false;
    if (recursing) {
        tracePrintf2("%s (%s) got bad user-host packet, recursion detected, returning without push...",
                     socketDescriptor->humanReadableIPAddress().c_str(),
                     socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str());
        sendPacketToFastestPoolHost();
        return;
    }
    recursing = true;
    if (socketDescriptor->_hostNameDescriptor->_isUserHost) {
        socketDescriptor->sendPacket(this, false/* !haveTicket*/);
        tracePrintf2("%s (%s) got bad user-host packet, resending...",
                     socketDescriptor->humanReadableIPAddress().c_str(),
                     socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str());
    } else {
        tracePrintf2("%s (%s) got bad pool-host packet, pushing and selecting another host...",
                     socketDescriptor->humanReadableIPAddress().c_str(),
                     socketDescriptor->hostNameDescriptor()->nameAsRequested().c_str());
        _availablePoolSocketsByRTT.push(socketDescriptor);  // It will sort to the end with the forced-high RTT
        if (inPollingMode) {
            return;
        }
        sendPacketToFastestPoolHost();
    }
    recursing = false;
}

void 
ESNTPDriver::stateGotNameResolutionComplete(ESNTPHostNameDescriptor *hostNameDescriptor) {
    ESAssert(_thread->inThisThread());
    _poolHostLookupFailures = 0;  // Must not be a network problem (any more)
    if (inPollingMode) {
        return;
    }
    if (hostNameDescriptor->_isUserHost) {
        hostNameDescriptor->sendPacketsToAllHosts();
    } else {
        sendPacketToFastestPoolHost();
    }
    if (!_noProgressTimeout && !inPollingMode) {
        installNoProgressTimeout();
    }
}

void
ESNTPDriver::stateGotPostResolutionFailureTimerTimeout(ESNTPHostNameDescriptor *hostNameDescriptor) {
    ESAssert(_thread->inThisThread());
    tracePrintf1("timeout after resolution failure complete, restarting resolution for %s",
                 hostNameDescriptor->nameAsRequested().c_str());
    hostNameDescriptor->startResolving();
}

void 
ESNTPDriver::stateGotNameResolutionFailure(ESNTPHostNameDescriptor *hostNameDescriptor) {
    ESAssert(_thread->inThisThread());
    // Set up timer for retry
    hostNameDescriptor->setupPostResolutionFailureTimer();    
    if (!hostNameDescriptor->_isUserHost) {
        ESErrorReporter::logOffline(ESUtil::stringWithFormat("Name resolution failure for %s", hostNameDescriptor->nameAsRequested().c_str()));
        // Move on to next pool host
        tracePrintf1("Resolving another host because %s failed",
                     hostNameDescriptor->nameAsRequested().c_str());
        resolveOnePoolHostDNS();
    }
}

void 
ESNTPDriver::stateGotShortSocketTimeout(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    // New strategy, based on evidence that at least in England UDP packets are being lost frequently on non-3G networks:
    // - Resend quickly on all sockets, with the following caveats:
    //   1) We must allow older packets to come through and be handled properly
    //   2) We should round-robin through all sockets at this level (e.g., country) and should always retain one or two hosts at each level
    //   3) We shouldn't assume anything about the RTT of a socket which didn't respond -- likely to be the network
    //   4) We shouldn't throttle based on packet sends that don't have corresponding receives.
    if (socketDescriptor->_hostNameDescriptor->_isUserHost) {
        socketDescriptor->sendPacket(this, false/*!haveTicket*/);
    } else {
        ensureFreshHostAvailable();  // Make sure the top thing in the pool is a host we haven't used, if there are some at this level
        sendPacketToFastestPoolHostIncludingThisOne(socketDescriptor);
    }
}

void
ESNTPDriver::stateGotNoProgressTimeout() {
    ESAssert(_thread->inThisThread());
    int numHostsActive = _numPacketsSent - _numPacketsReceived - _numLongTimeoutsTriggered;
    if (numHostsActive >= ES_MAX_ACTIVE_POOL_HOSTS) {
        tracePrintf1("no progress timeout with %d hosts active, ensuring fresh host", numHostsActive);
        if (ensureFreshHostAvailable()) {  // Make sure the top thing in the pool is a host we haven't used
            installNoProgressTimeout();
        }
    } else {
        tracePrintf1("no progress timeout with %d hosts active, adding another host", numHostsActive);
        sendPacketToFastestPoolHost();  // will add one to active even if it means resolving another DNS name
        installNoProgressTimeout();
    }
}

void 
ESNTPDriver::stateGotLongSocketTimeout(ESNTPSocketDescriptor *socketDescriptor) {
    ESAssert(_thread->inThisThread());
    _numLongTimeoutsTriggered++;
}

void 
ESNTPDriver::stateGotNetworkUnreachable(bool knownToBeActive) {
    ESAssert(_thread->inThisThread());
    _networkReachable = false;
    if (knownToBeActive || _hostNameDescriptors.size() > 0) {  // We're trying to do something
        // Set time source status based on whether we were synchronized before or not
        switch(_status) {
          case ESTimeSourceStatusNoNetSynchronized:
          case ESTimeSourceStatusPollingSynchronized:
          case ESTimeSourceStatusFailedSynchronized:
          case ESTimeSourceStatusSynchronized:  // Not sure how we'd get here with this state, but...
            setStatus(ESTimeSourceStatusNoNetSynchronized);
            break;
          case ESTimeSourceStatusPollingRefining:
          case ESTimeSourceStatusRefining:
          case ESTimeSourceStatusFailedRefining:
          case ESTimeSourceStatusNoNetRefining:
            setStatus(ESTimeSourceStatusNoNetRefining);
            break;
          default:
            setStatus(ESTimeSourceStatusNoNetUnsynchronized);
            break;
        }
    }
    tracePrintf("Network unreachable: turning off sync");
    if (_lastSyncStart != 0) {
        stopSyncingInThisThread();
    }
}

void 
ESNTPDriver::startSyncingAfterDelay() {
    ESAssert(_thread->inThisThread());
    if (!restartSyncTimeoutObserver) {
        restartSyncTimeoutObserver = new ESNTPRestartSyncTimeoutObserver;
    }
    if (_restartSyncTimeout) {  // hijack this timer whose firing does what we want
        _restartSyncTimeout->release();
    }
    _restartSyncTimeout = new ESIntervalTimer(restartSyncTimeoutObserver, ES_NETWORK_RESTART_DELAY);
    _restartSyncTimeout->setInfo(this);
    _restartSyncTimeout->activate();
}

void 
ESNTPDriver::stateGotNetworkReachable() {
    ESAssert(_thread->inThisThread());
    _networkReachable = true;
    switch(_status) {
      case ESTimeSourceStatusNoNetSynchronized:
      case ESTimeSourceStatusNoNetRefining:
      case ESTimeSourceStatusNoNetUnsynchronized:
        tracePrintf("Network now reachable again: restarting sync via 'successful sync timeout' observer");
        startSyncingAfterDelay();
        return;
      default:  // Satisfy pedantic compiler
        break;
    }
    // Otherwise do nothing; the network didn't go down while we cared
    tracePrintf("Network now reachable again but either we weren't trying anything when it went down or we're already active so doing nothing");
}

void 
ESNTPDriver::stateGotResyncTimeout() {
    ESAssert(_thread->inThisThread());
    // Should this be "user requested"?  I think so, unless it's been a *really* long time since our last valid sync
#ifdef ESTRACE
    bool userRequested = timeSinceLastSuccessfulSynchronization() < ES_FIX_TIMEOUT_INVALID;
    tracePrintf1("Resync timeout expired, resyncing with userRequested %s", userRequested ? "true" : "false");
#endif
    resyncInThisThread(true/*userRequested*/);
}

/*static*/ void
ESNTPDriver::setAppSignature(const char *fourByteAppSig) 
{
    int sigLength = (int)strlen(fourByteAppSig);
    ESAssert(sigLength <= 4 && sigLength > 0);
    _appSignature = fourByteAppSig[0] << 24;
    if (sigLength > 1) {
        _appSignature |= fourByteAppSig[1] << 16;
        if (sigLength > 2) {
            _appSignature |= fourByteAppSig[2] << 8;
            if (sigLength > 3) {
                _appSignature |= fourByteAppSig[3];
            }
        }
    }
    _appSignature = htonl(_appSignature);
}

// *****************************************************************************
// ESNTPHostNameDescriptor
// *****************************************************************************

ESNTPHostNameDescriptor::ESNTPHostNameDescriptor()
:   _isUserHost(false),
    _nameResolver(NULL),
    _numSocketDescriptors(0),
    _socketDescriptors(NULL),
    _driver(NULL),
    _resolving(false),
    _doneResolving(false),
    _numResolutionFailures(0),
    _postResolutionFailureTimer(NULL)
{
    ESAssert(ESNTPDriver::_theDriver);
    ESAssert(ESNTPDriver::_theDriver->thread());
    ESAssert(ESNTPDriver::_theDriver->thread()->inThisThread());  // We can't use _driver because it's NULL before init() is called
}

ESNTPHostNameDescriptor::~ESNTPHostNameDescriptor() {
    if (_driver) {
        ESAssert(_driver->thread()->inThisThread());
        ESAssert(_driver == ESNTPDriver::_theDriver);
    } else {
        ESAssert(ESNTPDriver::_theDriver);
        ESAssert(ESNTPDriver::_theDriver->thread());
        ESAssert(ESNTPDriver::_theDriver->thread()->inThisThread());  // We can't use _driver because it's NULL before init() is called
    }
    if (_socketDescriptors) {
        delete [] _socketDescriptors;
    }
    if (_nameResolver) {
        _nameResolver->release();  // Means no more notifications will be delivered
    }
    if (_postResolutionFailureTimer) {
        _postResolutionFailureTimer->release();
    }
}

void
ESNTPHostNameDescriptor::init(const std::string &name,
                              int               proximity,
                              bool              isUserHost,
                              ESNTPDriver       *driver) {
    _name = name;
    _proximity = proximity;
    _isUserHost = isUserHost;
    _driver = driver;
    ESAssert(_driver->thread()->inThisThread());  // Must go after the initialization of _driver
}

int 
ESNTPHostNameDescriptor::numSocketDescriptors() const {
    ESAssert(_driver->thread()->inThisThread());
    return _numSocketDescriptors;
}

void 
ESNTPHostNameDescriptor::setBitsForSocketFDs(fd_set *fdset) {
    ESAssert(_driver->thread()->inThisThread());
    for (int i = 0; i < _numSocketDescriptors; i++) {
        int fd = _socketDescriptors[i].fd();
        if (fd >= 0) {
            FD_SET(fd, fdset);
        }
    }
}

void 
ESNTPHostNameDescriptor::processRemoteSockets(fd_set *fdset) {
    for (int i = 0; i < _numSocketDescriptors; i++) {
        ESNTPSocketDescriptor *socketDescriptor = &_socketDescriptors[i];
        int fd = socketDescriptor->fd();
        if (fd >= 0 && FD_ISSET(fd, fdset)) {
            socketDescriptor->recvPacket(_driver);
            if (_driver->_stopReading) {
                break;
            }
        }
    }
}

void 
ESNTPHostNameDescriptor::closeAllSockets() {
    ESAssert(_driver->thread()->inThisThread());
    for (int i = 0; i < _numSocketDescriptors; i++) {
        _socketDescriptors[i].closeSocket();
    }
}

void 
ESNTPHostNameDescriptor::sendPacketsToAllHosts() {
    ESAssert(_driver->thread()->inThisThread());
    for (int i = 0; i < _numSocketDescriptors; i++) {
        _socketDescriptors[i].sendPacket(_driver, false/* !haveTicket*/);
    }
}

void 
ESNTPHostNameDescriptor::startResolving() {  // Start a background name resolution
    ESAssert(_driver->thread()->inThisThread());
    _resolving = true;
    tracePrintf1("startResolving '%s'", _name.c_str());
    _nameResolver = new ESNameResolver(this,   // observer
                                       _name,  // host name
                                       ES_NTP_PORT_AS_STRING, // portAsString
                                       AI_NUMERICSERV      // Service name ("123") is numeric
                                         | AI_ADDRCONFIG,  // IPv6 addresses only returned if IPv6 configured locally
                                       IPPROTO_UDP);
}

void
ESNTPHostNameDescriptor::postResolutionFailureTimerFire() {
    _postResolutionFailureTimer = NULL;
    // Leave the observer around for the (likely) next failure
    _driver->stateGotPostResolutionFailureTimerTimeout(this);
}

class ESNTPDriverPostResolutionFailureTimerObserver : public ESTimerObserver {
  public:
    /*virtual*/ void        notify(ESTimer *timer) {
        ESNTPHostNameDescriptor *hostDescriptor = (ESNTPHostNameDescriptor *)timer->info();
        hostDescriptor->postResolutionFailureTimerFire();
    }
};
static ESNTPDriverPostResolutionFailureTimerObserver *postResolutionFailureTimerObserver = NULL;

void
ESNTPHostNameDescriptor::setupPostResolutionFailureTimer() {
    ESAssert(_driver->thread()->inThisThread());
    ESTimeInterval delay = ES_RESOLUTION_FAILURE_RETRY_BASE * _numResolutionFailures;
    ESAssert(!_postResolutionFailureTimer);  // Because we do this only once per failure
    if (!postResolutionFailureTimerObserver) {
        postResolutionFailureTimerObserver = new ESNTPDriverPostResolutionFailureTimerObserver();
    }
    _postResolutionFailureTimer = new ESIntervalTimer(postResolutionFailureTimerObserver, delay);
    _postResolutionFailureTimer->setInfo(this);
    _postResolutionFailureTimer->activate();
    tracePrintf2("host %s resolution failure timer set to expire in %.1f seconds",
                 _name.c_str(), delay);
}

// ESNameResolverObserver redefines
/*virtual*/ void 
ESNTPHostNameDescriptor::notifyNameResolutionComplete(ESNameResolver *resolver) {
    ESAssert(_driver->thread()->inThisThread());
    ESAssert(_numSocketDescriptors == 0);
    ESAssert(_socketDescriptors == NULL);
    _numSocketDescriptors = 0;
    _resolving = false;
    _doneResolving = true;
    tracePrintf1("name resolution complete for '%s'", _name.c_str());
    const struct addrinfo *result0 = resolver->result0();
    for (const struct addrinfo *res = result0; res; res = res->ai_next) {
        _numSocketDescriptors++;
    }    
    if (_numSocketDescriptors == 0) {
        ESAssert(result0 == NULL);
        ESErrorReporter::logError("ESNTPDriver",
                                  "Name resolution succeeded but host list was empty for host '%s'",
                                  _name.c_str());
        _driver->nameResolutionFailed(this, _name, 0);
        return;
    }
    _socketDescriptors = new ESNTPSocketDescriptor[_numSocketDescriptors];
    int i = 0;
    for (const struct addrinfo *addrinfo = result0; addrinfo; addrinfo = addrinfo->ai_next, i++) {
        ESNTPSocketDescriptor *desc = &_socketDescriptors[i];
        desc->_hostNameDescriptor = this;
        desc->_family = addrinfo->ai_family;
        ESAssert(addrinfo->ai_addrlen < sizeof(desc->_addr));
        memcpy(&desc->_addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
        desc->_addrlen = addrinfo->ai_addrlen;
        desc->_socktype = addrinfo->ai_socktype;
        desc->_protocol = addrinfo->ai_protocol;
        if (!_isUserHost) {
            _driver->pushPoolSocket(desc);
        }
    }
    _driver->nameResolutionComplete(this);
    freeaddrinfo(resolver->result0());
}

/*virtual*/ void 
ESNTPHostNameDescriptor::notifyNameResolutionFailed(ESNameResolver *resolver,
                                                    int            failureStatus) {
    _resolving = false;
    _doneResolving = true;
    _numResolutionFailures++;
    tracePrintf1("name resolution FAILED for '%s'", _name.c_str());
    _driver->nameResolutionFailed(this, _name, failureStatus);
}

// *****************************************************************************
// Host reporting...  The implementation is split among ESNTPDriver and the
// two ESNTPHostReport classes so we place it here to put it all in one place
// *****************************************************************************

static void makeHostReportGlue(void *obj,
                               void *param) {
    ((ESNTPDriver *)obj)->constructAndSendHostReportToObserver((ESNTPHostReportObserver *)param);
}

/*static*/ void 
ESNTPDriver::registerHostReportObserver(ESNTPHostReportObserver *observer) {
    ESAssert(ESThread::inMainThread());
    if (!hostReportObservers) {
        hostReportObservers = new std::list<ESNTPHostReportObserver *>;
    }
    hostReportObservers->push_back(observer);
    if (_theDriver && _theDriver->_thread) {
        _theDriver->_thread->callInThread(makeHostReportGlue, _theDriver, observer);
    }
}

/*static*/ void 
ESNTPDriver::unregisterHostReportObserver(ESNTPHostReportObserver *observer) {
    hostReportObservers->remove(observer);
}


void
ESNTPDriver::makeHostReportIfRequired() {
    ESAssert(_thread->inThisThread());

    // Call observers "safely" with respect to removing the observer we're on
    if (hostReportObservers) {
        ESNTPHostReportObserver *observer;
        std::list<ESNTPHostReportObserver *>::iterator end = hostReportObservers->end();
        std::list<ESNTPHostReportObserver *>::iterator iter = hostReportObservers->begin();
        if (iter != end) {
            observer = *iter;
            iter++;
            while (iter != end) {
                constructAndSendHostReportToObserver(observer);  // Each observer needs its own copy of the report, since it will own the storage
                observer = *iter;
                iter++;
            }
            constructAndSendHostReportToObserver(observer);
        }
    }
}

void
ESNTPDriver::constructAndSendHostReportToObserver(ESNTPHostReportObserver *observer) {
    ESAssert(_thread->inThisThread());
    ESNTPHostReport *hostReport = new ESNTPHostReport(this);
    observer->callHostReportAvailable(hostReport);
}

ESNTPHostReport::ESNTPHostReport(const ESNTPDriver *driver) {
    ESAssert(driver->_thread->inThisThread());  // Because we're accessing the _socketDescriptors array which is changed in that thread
    _numberOfHostNameInfos = (int)driver->_hostNameDescriptors.size();
    HostNameInfo *hostNameInfoPtr = 
        _hostNameInfos = new HostNameInfo[_numberOfHostNameInfos];
    for (int n = 0; n < _numberOfHostNameInfos; n++, hostNameInfoPtr++) {
        const ESNTPHostNameDescriptor *hostNameDescriptor = &driver->_hostNameDescriptors[n];
        hostNameInfoPtr->resolving = hostNameDescriptor->_resolving;
        hostNameInfoPtr->resolved = hostNameDescriptor->_doneResolving;
        hostNameInfoPtr->failedResolution = (hostNameDescriptor->_numResolutionFailures > 0);
        hostNameInfoPtr->hostNameAsRequested = hostNameDescriptor->nameAsRequested();
        hostNameInfoPtr->_numberOfHostInfos = hostNameDescriptor->numSocketDescriptors();
        HostNameInfo::HostInfo *hostInfoPtr = 
            hostNameInfoPtr->_hostInfos = new HostNameInfo::HostInfo[hostNameInfoPtr->_numberOfHostInfos];
        for (int i = 0; i < hostNameInfoPtr->_numberOfHostInfos; i++) {
            const ESNTPSocketDescriptor *socketDescriptor = &hostNameDescriptor->_socketDescriptors[i];
            hostInfoPtr->humanReadableIPAddress = socketDescriptor->humanReadableIPAddress();
            hostInfoPtr->packetsReceived = socketDescriptor->packetsReceived();
            hostInfoPtr->packetsUsed = socketDescriptor->packetsUsed();
            hostInfoPtr->packetsSent = socketDescriptor->packetsSent();
            hostInfoPtr->disabled = false; // FIX FIX
            hostInfoPtr->skewLB = socketDescriptor->skewLB();
            hostInfoPtr->skewUB = socketDescriptor->skewUB();
            hostInfoPtr->leapBits = socketDescriptor->leapBitsFromLastPacket();
            hostInfoPtr->stratum = socketDescriptor->stratumFromLastPacket();
            hostInfoPtr->nonRTTError = socketDescriptor->nonRTTErrorFromLastPacket();
            hostInfoPtr++;
        }
        ESAssert(hostInfoPtr == hostNameInfoPtr->_hostInfos + hostNameInfoPtr->_numberOfHostInfos);
    }
    ESAssert(hostNameInfoPtr == _hostNameInfos + _numberOfHostNameInfos);
}

ESNTPHostReport::~ESNTPHostReport() {
    for (int n = 0; n < _numberOfHostNameInfos; n++) {
        delete [] _hostNameInfos[n]._hostInfos;
    }
    delete [] _hostNameInfos;
}

static void
hostReportGlue(void *obj,
               void *param) {
    ESNTPHostReportObserver *observer = (ESNTPHostReportObserver *)obj;
    ESNTPHostReport *hostReport = (ESNTPHostReport *)param;
    observer->hostReportAvailable(hostReport);
}

void 
ESNTPHostReportObserver::callHostReportAvailable(ESNTPHostReport *hostReport) {
    if (_notificationThread && !_notificationThread->inThisThread()) {
        _notificationThread->callInThread(hostReportGlue, this, hostReport);
    } else {
        hostReportAvailable(hostReport);
    }
}


ESNTPHostReportObserver::ESNTPHostReportObserver(ESThread *notificationThread)
:   _notificationThread(notificationThread)
{
}
