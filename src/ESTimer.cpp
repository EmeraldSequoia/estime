//
//  ESTimer.cpp
//
//  Created by Steve Pucci 31 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#include "ESTimer.hpp"
#include "ESThread.hpp"
#include "ESErrorReporter.hpp"
#undef ESTRACE
#include "ESTrace.hpp"

#include "math.h"

#include <set>

// Does select() operate on the continuous time base (i.e., CACurrentMediaTime) as opposed to the "true" (NSDate-style) one?
// #define ES_SELECT_USES_CONTINUOUS_TIME 0
#define ES_SELECT_USES_CONTINUOUS_TIME 1

// Timer delivery strategy:  Keep track of the next timer in a sorted list.
// Have the timer thread select() to time out when the next timer comes in,
// deliver the notification, and then select() to time out for the following
// timer. Send a message to that thread via the normal ESThread call mechanism
// which will also trigger a return from the same select();

// This thread handles delivery for all threads, and maintains the list of
// timers in its own thread to avoid thread synchronization and race condition
// issues.  Like an Arnold Schwarzenegger Terminator, it can never die.
//
class ESTimerThread : public ESChildThread {
  public:
                            ESTimerThread();
    void                    *main();

    void                    activate(ESTimer *timer);
    void                    deactivate(ESTimer *timer);
    void                    release(ESTimer *timer,
                                    int     notificationsReceivedAtTimeOfRequest);

    void                    recalculateAllFireTimes();
    void                    fireLapsedTimers();
};

class ESTimerThreadTimeObserver : public ESTimeSyncObserver {
  public:
                            ESTimerThreadTimeObserver(ESTimerThread *timerThread);
    virtual void            syncValueChanged();    // There was a (potential) jump in the absolute time
    virtual void            syncStatusChanged();   // There was a change in the time source status (useful for indicators)
    virtual void            continuousTimeReset(); // There was a (potential) jump in the continuous time

  private:
    ESTimerThread           *_timerThread;
};


ESTimerThreadTimeObserver::ESTimerThreadTimeObserver(ESTimerThread *timerThread)
:   ESTimeSyncObserver(timerThread),
    _timerThread(timerThread)
{
}

/*virtual*/ void 
ESTimerThreadTimeObserver::syncValueChanged() {    // There was a (potential) jump in the absolute time
#if !ES_SELECT_USES_CONTINUOUS_TIME
    _timerThread->recalculateAllFireTimes();
#endif
}

/*virtual*/ void 
ESTimerThreadTimeObserver::syncStatusChanged() {   // There was a change in the time source status (useful for indicators)
    // Do nothing
}

/*virtual*/ void 
ESTimerThreadTimeObserver::continuousTimeReset() { // There was a (potential) jump in the continuous time
#if ES_SELECT_USES_CONTINUOUS_TIME
    _timerThread->recalculateAllFireTimes();
#endif
}

static ESTimerThread *timerThread;

// Comparison operator for sorting timers
struct ESltTimer {
    bool                    operator()(ESTimer *t1,
                                       ESTimer *t2) const
    {
        return t1->fireTime() < t2->fireTime();
    }
};

typedef std::multiset<class ESTimer *, struct ESltTimer> TimerPriorityQueue;

TimerPriorityQueue *timerPriorityQueue;

ESTimerThread::ESTimerThread()
:   ESChildThread("Timer", ESChildThreadExitsOnlyByParentRequest)
{
}

void *
ESTimerThread::main() {
    ESAssert(inThisThread());
    ESAssert(!timerPriorityQueue);
    timerPriorityQueue = new TimerPriorityQueue;
    ESTime::registerTimeSyncObserver(new ESTimerThreadTimeObserver(this));
    while (1) {
        fd_set readers;
        FD_ZERO(&readers);
        int highestThreadFD = ESThread::setBitsForSelect(&readers);
        int nfds = highestThreadFD + 1;
        struct timeval *timeout;
        struct timeval tv;
#undef ES_INSTRUMENT_SELECT_TIMEBASE  // Define this and note what happens during an aSTC event -- one of the two elapsed times will match the requested interval, and one won't
#ifdef ES_INSTRUMENT_SELECT_TIMEBASE  //  On the iPhone, the elapsed time matches the continuous time and not the system time
        ESTimeInterval startSysTime;
        ESTimeInterval startContTime;
        ESTimeInterval startDelta;
#endif
        if (timerPriorityQueue->empty()) {
            timeout = NULL;  // No timers, so block in select waiting for a call
        } else {
            ESTimer *timer = *(timerPriorityQueue->begin());
            ESTimeInterval fireTime = timer->fireTime();
#if ES_SELECT_USES_CONTINUOUS_TIME
            ESTimeInterval now = ESTime::currentContinuousTime();
#else
            ESTimeInterval now = ESTime::currentTime(); // Shouldn't this (and all the ones like it) be ESSystemTimeBase::currentSystemTime() ?
#endif
            ESTimeInterval delta = fireTime - now;
            if (delta < 0) {
                delta = 0;
            }
            double seconds = floor(delta);
            tv.tv_sec = seconds;
            tv.tv_usec = (delta - seconds) * 1000000;
            timeout = &tv;
#ifdef ES_INSTRUMENT_SELECT_TIMEBASE
            ESTimeInterval nowSys = ESSystemTimeBase::currentSystemTime();
            printf("About to select with timeout of %.6f seconds (now %.6f, nowSys %.6f, fire time %.6f)\n", delta, now, nowSys, fireTime);
            startSysTime = ESSystemTimeBase::currentSystemTime();
            startContTime = ESTime::currentContinuousTime();
            startDelta = delta;
#endif
        }
        select(nfds, &readers, NULL/*writers*/, NULL, timeout);
#ifdef ES_INSTRUMENT_SELECT_TIMEBASE
        if (timeout) {
           printf("...returning from select with timeout.  %.6f system seconds have elapsed (%.6f more than delta); %.6f continuous seconds have elapsed (%.6f more than delta)\n",
                  ESSystemTimeBase::currentSystemTime() - startSysTime, ESSystemTimeBase::currentSystemTime() - startSysTime - startDelta,
                  ESTime::currentContinuousTime() - startContTime, ESTime::currentContinuousTime() - startContTime - startDelta);
        }
#endif
        ESThread::processInterThreadMessages(&readers);
        fireLapsedTimers();
    }
    return NULL;
}

void
ESTimerThread::fireLapsedTimers() {
    ESAssert(inThisThread());
    TimerPriorityQueue::iterator end = timerPriorityQueue->end();
    TimerPriorityQueue::iterator iter = timerPriorityQueue->begin();
#if ES_SELECT_USES_CONTINUOUS_TIME
    ESTimeInterval now = ESTime::currentContinuousTime();
#else
    ESTimeInterval now = ESTime::currentTime();
#endif
    while (iter != end) {
        ESTimer *timer = *iter;
        TimerPriorityQueue::iterator next = iter;
        next++;
        if (timer->fireTime() < now) {
            timer->deliverNotification();
            timerPriorityQueue->erase(iter);
            if (timer->possiblyRepeat()) {
                timer->calculateFireTime();
                //printf("fired timer 0x%08x, repeating, calculating new time and inserting\n", (unsigned int)timer);
                timerPriorityQueue->insert(timer);                
            } else {
                //printf("fired timer 0x%08x, not repeating, deactivating\n", (unsigned int)timer);
                timer->_activated = false;
            }
        } else {
            break;  // Nothing after this could be ready if this one isn't
        }
        iter = next;
    }
}

void 
ESTimerThread::activate(ESTimer *timer) {
    ESAssert(inThisThread());

    // Calculate fire time
    timer->calculateFireTime();

    // Add to sorted list
    timerPriorityQueue->insert(timer);

    timer->_activated = true;
    //printf("timer 0x%08x activated\n", (unsigned int)timer);
}

void 
ESTimerThread::deactivate(ESTimer *timer) {
    ESAssert(inThisThread());
    // Remove from list
    timerPriorityQueue->erase(timer);
    
    timer->_activated = false;
}

#ifndef NDEBUG
static bool inReleaseDelete = false;
#endif

// There is a potential race condition with messages crossing between threads:
//    The timer thread sends a notification, dispatches a message to notification thread
//    The notification thread releases, sending a message to timer thread
//    The timer thread receives the release message and deletes the timer
//    The notification thread receives the notification message and attempts to dispatch off deleted timer storage
// To avoid this condition, the release message includes the number of notifications received for this timer
// and the timer thread will not delete the timer unless the number of notifications received equals the number that
// it has recorded as being sent.  If they do not match, it means that the notification thread had not received
// all of its notifications when it sent the release message; it will therefore eventually receive another one (or
// more) and will receive them when the _releasePending flag (which it set itself) is on.  Each time it receives a
// notification with _releasePending it will attempt another release message with the updated notification count;
// eventually it should send a message with a count that matches the number that have been sent by the timer thread
// and when the timer thread receives that message it is free to delete the timer since there are no more pending
// notifications
void 
ESTimerThread::release(ESTimer *timer,
                       int     notificationsReceivedAtTimeOfRequest) {
    ESAssert(inThisThread());
    if (timer->_activated) {
        deactivate(timer);
    }
    if (notificationsReceivedAtTimeOfRequest != timer->_notificationsSent) {
        tracePrintf3("timer thread refusing to delete timer 0x%08x whose # of notifications sent (%d) does not match # received at time request was sent (%d)",
                     (unsigned long int)timer, timer->_notificationsSent, notificationsReceivedAtTimeOfRequest);
        return;  // A notification is pending; wait for it to finish
    }
    
    // Then delete it
#ifndef NDEBUG
    inReleaseDelete = true;
#endif
    delete timer;
#ifndef NDEBUG
    inReleaseDelete = false;
#endif
}

void
ESTimerThread::recalculateAllFireTimes() {
    ESAssert(inThisThread());
    ESAssert(timerPriorityQueue);
    TimerPriorityQueue *newQueue = new TimerPriorityQueue;
    TimerPriorityQueue::iterator end = timerPriorityQueue->end();
    TimerPriorityQueue::iterator iter = timerPriorityQueue->begin();
    TimerPriorityQueue::iterator newIter = newQueue->begin();
    while (iter != end) {
        ESTimer *timer = *iter;
        timer->calculateFireTime();
        newIter = newQueue->insert(newIter, timer);
        iter++;
    }
    delete timerPriorityQueue;
    timerPriorityQueue = newQueue;
    fireLapsedTimers();
}

//////////// ESTimer

ESTimer::ESTimer(ESTimerObserver *observer,
                 ESTimeInterval  atTime,
                 bool            useContinuousTime)
:   _observer(observer),
    _atTime(atTime),
    _useContinuousTime(useContinuousTime),
    _notificationThread(NULL),
    _releasePending(false),
    _activated(false),
    _notificationsSent(0),
    _notificationsReceived(0)
{
    tracePrintf1("Ctor of ESTimer 0x%08x:\n", (unsigned long int)this);
    //printf("%s\n",ESUtil::stackTrace().c_str());
}

/*virtual*/ 
ESTimer::~ESTimer() {
    ESAssert(inReleaseDelete);  // The destructor for a timer should only be called by release in the timer thread
    if (timerThread) {
        // We might be in shutdown, in which case timerThread is dead, Jed.
        ESAssert(timerThread->inThisThread());
        ESAssert(!_activated);
    }
    tracePrintf1("Dtor of ESTimer 0x%08x:\n", (unsigned long int)this);
    //printf("%s\n",ESUtil::stackTrace().c_str());
}

static void activateGlue(void *obj,
                         void *param) {
    timerThread->activate((ESTimer *)obj);
}

void
ESTimer::calculateFireTime() {
    ESAssert(timerThread);  // Because this is called only once a timer has been activated
    ESAssert(timerThread->inThisThread());
#if ES_SELECT_USES_CONTINUOUS_TIME
    if (_useContinuousTime) {
        _fireTime = _atTime;
    } else {
        _fireTime = ESTime::cTimeForNTPTime(_atTime);
        //printf("atTime %.4f _fireTime %.4f\n", _atTime, _fireTime);
    }
#else
    if (_useContinuousTime) {
        _fireTime = ESTime::ntpTimeForCTime(_atTime);
    } else {
        _fireTime = _atTime;
    }
#endif    
}

/*static*/ void 
ESTimer::init() {  // Called by ESTime::init -- make sure it happens before any timers are activated in non-main thread
    if (!timerThread) {
        ESAssert(ESThread::inMainThread());  // To avoid race conditions creating the timer thread
        timerThread = new ESTimerThread;
        timerThread->start();
    }
}

/*static*/ void
ESTimer::shutdown() {  // Called by ESTime::shutdown
    // This routine is not ready for prime time, so we're asserting false here.  Actually the code *here*
    // is probably ok, if you could guarantee no timers will fire afterwards, but that's tough to guarantee
    // without a completely thorough scouring of the rest of the code.
    ESAssert(false);  // So for now, to protect us shooting ourselves in the foot later.
    
    timerThread->requestExitAndWaitForJoin();
    timerThread = NULL;

    TimerPriorityQueue::iterator end = timerPriorityQueue->end();
    TimerPriorityQueue::iterator iter = timerPriorityQueue->begin();
    while (iter != end) {
        ESTimer *timer = *iter;
#ifndef NDEBUG
        inReleaseDelete = true;
#endif
        delete timer;
#ifndef NDEBUG
        inReleaseDelete = false;
#endif
    }

    delete timerPriorityQueue;
    timerPriorityQueue = NULL;
}

void 
ESTimer::activate() {
    ESAssert(!_releasePending);  // Don't try to activate a timer that's been released
    if (!timerThread) {
        ESAssert(ESThread::inMainThread());  // To avoid race conditions creating the timer thread
        timerThread = new ESTimerThread;
        timerThread->start();
    }
    ESAssert(!_notificationThread);
    _notificationThread = ESThread::currentThread();
    timerThread->callInThread(activateGlue, this, NULL);
}

static void releaseGlue(void *obj,
                        void *param) {
    ESPointerSizedInt notificationsReceivedAtTimeOfRequest = (ESPointerSizedInt)param;
    timerThread->release((ESTimer *)obj, (int)notificationsReceivedAtTimeOfRequest);
}

void 
ESTimer::release() {  // Call this in place of 'delete timer' when you want to destroy it
    ESAssert(!_releasePending);  // Don't call release() more than once
    _releasePending = true;
    if (_notificationThread) {  // already activated
        ESAssert(_notificationThread->inThisThread());  // To avoid race conditions with notification delivery check of _releasePending
    } else {
        // Never activated: Just delete it
    }
    ESAssert(timerThread);  // Don't release any timers before activating the first one to create the thread (this could be rewritten to just call 'delete this' if we really need it)
    timerThread->callInThread(releaseGlue, this, (void*)(long)_notificationsReceived);

    // Nothing should happen after the callInThread above, because
    // after that call there is no guarantee that the object exists
    // any more
}

bool 
ESTimer::releasePending() {  // Only guaranteed in notification thread after activation
    tracePrintf1("releasePending on ESTimer 0x%08x\n", (unsigned long int)this);
    ESAssert(_notificationThread);  // Set by activate()
    ESAssert(_notificationThread->inThisThread());
    return _releasePending;
}

void
ESTimer::receiveNotificationMessage(ESTimerObserver *observer) {
    ESAssert(_notificationThread);  // Because it should have been set when we activated this timer
    ESAssert(_notificationThread->inThisThread());
    _notificationsReceived++;
    if (releasePending()) {  // We need to resend the release request because the first one will have been sent with too small a _notificationsReceived
        timerThread->callInThread(releaseGlue, this, (void*)(long)_notificationsReceived);
    } else {
        observer->notify(this);
    }
}

/*static*/ void
ESTimer::notificationGlue(void *obj,
                          void *param) {
    ((ESTimer *)obj)->receiveNotificationMessage((ESTimerObserver *)param);
}

void 
ESTimer::deliverNotification() {
    ESAssert(timerThread);  // Because it should have been created when we activated the first timer
    ESAssert(timerThread->inThisThread());
    ESAssert(_notificationThread);  // Because it should have been set when we activated this timer
    _notificationsSent++;
    _notificationThread->callInThread(notificationGlue, this, _observer);
}

ESTimeInterval
ESTimer::fireTime() {
    return _fireTime;
}

//////////// ESIntervalTimer

ESIntervalTimer::ESIntervalTimer(ESTimerObserver *observer,
                                 ESTimeInterval  intervalFromReference,
                                 ESTimeInterval  continuousTimeReference,
                                 ESTimeInterval  repeatEverySeconds)
:   ESTimer(observer, (continuousTimeReference == -1 ? ESTime::currentContinuousTime() : continuousTimeReference) + intervalFromReference, true/*useContinuousTime*/),
    _repeatEverySeconds(repeatEverySeconds)
{
}

ESIntervalTimer::~ESIntervalTimer()
{
}

/*virtual*/ bool
ESIntervalTimer::possiblyRepeat() {
    if (_repeatEverySeconds) {
        _atTime += _repeatEverySeconds;  // FIX: Find the next such time that's after this instant
        return true;
    }
    return false;
}

//////////// ESAbsoluteTimer

ESAbsoluteTimer::ESAbsoluteTimer(ESTimerObserver *observer,
                                 ESTimeInterval  synchronizedTime,  // A time from ESTime::currentTime()
                                 ESTimeInterval  repeatEverySeconds)
:   ESTimer(observer, synchronizedTime, false/* !useContinuousTime*/),
    _repeatEverySeconds(repeatEverySeconds)
{
}

ESAbsoluteTimer::~ESAbsoluteTimer()
{
}

/*virtual*/ bool
ESAbsoluteTimer::possiblyRepeat() {
    if (_repeatEverySeconds) {
        _atTime += _repeatEverySeconds;  // FIX: Find the next such time that's after this instant
        return true;
    }
    return false;
}

//////////// ESDailyTimer

ESTimeInterval
ESDailyTimer::calculateNextTimerTime() {
    ESAssert(false);
    return 0;  // FIX FIXME
}

ESDailyTimer::~ESDailyTimer()
{
}

ESDailyTimer::ESDailyTimer(ESTimerObserver *observer,
                           ESTimeInterval  *secondsAfterMidnightLocal,
                           ESTimeZone      *timeZone,
                           int             repeatIntervalInDays)  // 0 means don't repeat, just deliver once
:   ESTimer(observer, 0/*calculate time below*/, false/* !useContinuousTime*/),
    _repeatIntervalInDays(repeatIntervalInDays),
    _timeZone(timeZone)
{
    _atTime = calculateNextTimerTime();
}

/*virtual*/ bool
ESDailyTimer::possiblyRepeat() {
    if (_repeatIntervalInDays) {
        _atTime = calculateNextTimerTime();
        return true;
    }
    return false;
}

//////////// ESContinuousTimeTimer

ESContinuousTimeTimer::ESContinuousTimeTimer(ESTimerObserver *observer,
                                             ESTimeInterval  continuousTime)
:  ESTimer(observer, continuousTime, true/*useContinuousTime*/)
{
}

/*virtual*/ bool 
ESContinuousTimeTimer::possiblyRepeat() {
    return false;
}

