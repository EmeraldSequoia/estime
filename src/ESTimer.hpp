//
//  ESTimer.hpp
//
//  Created by Steve Pucci 31 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESTIMER_HPP_
#define _ESTIMER_HPP_

#include "ESTime.hpp"
#include "ESCalendar.hpp"

// To create a timer, first derive from ESTimerObserver and override
// the notify() pure virtual method to do what you want to do.  Then
// simply construct one of the concrete timer classes
// (ESIntervalTimer, ESAbsoluteTimer, or ESDailyTimer) and call the
// activate() method in the thread that you want the message delivered
// to (or specify that thread in the activate method).

// Don't call 'delete' on a timer -- call timer->release() instead
// for the equivalent functionality.  a timer may only be destroyed
// by the timer system (because timers are scheduled in a separate
// thread, there is otherwise no way to ensure that the timer isn't
// being referenced when it is deleted).  The destructor for each
// timer type is private (well, it's protected so derived class
// destructors can call it, but no class should make the destructor
// public), so this should be caught by the compiler (it also means,
// quite correctly, that stack variables and global/static variables
// are illegal).

// Manipulating a timer, once activated, is not allowed.  The only
// time it is legal for even a subclass to change the firing time
// is during the possiblyRepeat() callback.

// Uses:
//   Daily alarm:          ESDailyTimer
//   Calendar entry alarm: ESAbsoluteTimer
//   Trigger update:       ESIntervalTimer
//   Trigger update every  ESAbsoluteTimer with repeatInterval
//     even second:
//   Interval timer:       ESIntervalTimer from reference point

// It is fairly straightforward to derive from ESTimer for another kind of timer.
// For example, ESSunriseTimer could be defined (presumably in ESAstronomy).  It
// simply needs to specify the time and redefine possiblyRepeat().  It could probably
// even save a few lines by deriving from ESAbsoluteTimer directly.

class ESTimer;

/* Abstract observer base class -- derive from this to be notified of a timer firing. */
class ESTimerObserver {
  public:
    virtual                 ~ESTimerObserver() {}
    virtual void            notify(ESTimer *timer) = 0;
};

/*! Abstract base class -- don't instantiate directly; use one of the derived classes below */
class ESTimer {
  public:
                            ESTimer(ESTimerObserver *observer,
                                    ESTimeInterval  atTime,
                                    bool            useContinuousTime);
    void                    activate(); // Notifications will be delivered in the thread active when activate() is called, if release() isn't called first
    void                    release();  // Call this in place of 'delete timer' when you want to destroy it, only in the activation thread

    void                    setInfo(void *info) { _info = info; }
    void                    *info() const { return _info; }

    static void             init();  // Called by ESTime::init -- make sure it happens before any timers are activated in non-main thread
    static void             shutdown();  // Called, in Android, when switching away from watch face

    // Methods to be called only by ESTimer internal classes:

  protected:
    virtual                 ~ESTimer();

    // Following function should either return false, meaning don't reactivate, or
    // it should set _atTime and then return true, meaning reactivate at the given _atTime
    // using the previously specified parameters (useContinuousTime, notificationThread, observer).
    // It is called in the special ESTimerThread, not in the main thread or the notificationThread.
    virtual bool            possiblyRepeat() = 0;

    ESTimeInterval          _atTime;
    bool                    _useContinuousTime;

  private:
    void                    deliverNotification();
    void                    calculateFireTime();
    ESTimeInterval          fireTime();
    bool                    releasePending();  // Only guaranteed in notification thread after activation
    void                    receiveNotificationMessage(ESTimerObserver *observer);

    static void             notificationGlue(void *obj,
                                             void *param);

    ESTimerObserver         *_observer;
    ESThread                *_notificationThread;
    bool                    _activated;  // Don't try to use this outside the timer thread
    bool                    _releasePending;  // Dont' try to use this outside the notification thread
    ESTimeInterval          _fireTime;
    void                    *_info;
    int                     _notificationsSent;
    int                     _notificationsReceived;

friend class ESTimerThread;
friend class ESltTimer;
};

/* Deliver a message to the observer after the given interval has passed. */
class ESIntervalTimer : public ESTimer {
  public:
                            ESIntervalTimer(ESTimerObserver *observer,
                                            ESTimeInterval  intervalFromReference,
                                            ESTimeInterval  continuousTimeReference = -1,  // use ESTime::currentContinuousTime() as the reference
                                            ESTimeInterval  repeatEverySeconds = 0);

  protected:
    virtual                 ~ESIntervalTimer();
    virtual bool            possiblyRepeat();

  private:
    ESTimeInterval          _repeatEverySeconds;
};

/* Deliver a message to the observer at the given absolute synchronized time. */
class ESAbsoluteTimer : public ESTimer {
  public:
                            ESAbsoluteTimer(ESTimerObserver *observer,
                                            ESTimeInterval  synchronizedTime,  // A time from ESTime::currentTime()
                                            ESTimeInterval  repeatEverySeconds = 0);
  protected:
    virtual                 ~ESAbsoluteTimer();
    virtual bool            possiblyRepeat();

  private:
    ESTimeInterval          _repeatEverySeconds;
};

/* Deliver a daily timer at the given absolute synchronized local time every day. */
class ESDailyTimer : public ESTimer {
  public:
                            ESDailyTimer(ESTimerObserver *observer,
                                         ESTimeInterval  *secondsAfterMidnightLocal,
                                         ESTimeZone      *timeZone,
                                         int             repeatIntervalInDays = 1);  // 0 means don't repeat, just deliver once
  protected:
    virtual bool            possiblyRepeat();
    virtual                 ~ESDailyTimer();

  private:
    ESTimeInterval          calculateNextTimerTime();

    ESTimeInterval          _repeatIntervalInDays;
    ESTimeZone              *_timeZone;
};

/* Deliver a single notification at a given continuous time. */
class ESContinuousTimeTimer : public ESTimer {
  public:
                            ESContinuousTimeTimer(ESTimerObserver *observer,
                                                  ESTimeInterval  continuousTime);
  protected:
    virtual bool            possiblyRepeat();
};

#endif  // _ESTIMER_HPP_
