//
//  ESXGPS150TimeDriver.hpp
//
//  Created by Steve Pucci 18 Nov 2012
//  Copyright Emerald Sequoia LLC 2012. All rights reserved.
//

#ifndef _ESXGPS150TIMEDRIVER_HPP_
#define _ESXGPS150TIMEDRIVER_HPP_

#include "ESPlatform.h"

ES_OPAQUE_OBJC(NSInputStream);
ES_OPAQUE_OBJC(NSOutputStream);
ES_OPAQUE_OBJC(ESXGPS150TimeDriverStreamDelegate);

/** This class provides a time source that uses an external Dual Electronics XGPS150 GPS dongle to provide the time. */
class ESXGPS150TimeDriver : public ESTimeSourceDriver {
  public:
                            ESXGPS150TimeDriver();
                            ~ESXGPS150TimeDriver();

    // ESTimeSourceDriver redefines
    virtual void            resync(bool userRequested);
    virtual void            stopSyncing();
    virtual void            enableSync();
    virtual void            disableSync();
    virtual std::string     userName();
    
#if ES_IOS
    NSInputStream           *inputStream() const { return _inputStream; }
    NSOutputStream           *outputStream() const { return _outputStream; }
    void                    setFastSampleRate(NSOutputStream *outputStream);
#endif

  private:
    bool                    _useFastSampleRate;

#if ES_IOS
    bool                    _sendFastSampleRateCommandWhenSpaceAvailable;
    bool                    _registeredForAccessoryNotifications;
    NSInputStream           *_inputStream;
    NSOutputStream          *_outputStream;
    ESXGPS150TimeDriverStreamDelegate *_delegate;
#endif
};

#endif  // _ESXGPS150TIMEDRIVER_HPP_
