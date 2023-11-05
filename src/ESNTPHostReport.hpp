//
//  ESNTPHostReport.hpp
//
//  Created by Steve Pucci 15 May 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESNTPHOSTREPORT_HPP_
#define _ESNTPHOSTREPORT_HPP_

class ESNTPDriver;

/////////////////////////////

// Class used for "debug" reporting (and for geeky apps like Emerald Time)
// Captures the complete state of the host query mechanism.
// See ESNTPDriver.hpp for the interface to obtain a report
class ESNTPHostReport {
  public:
                            ESNTPHostReport(const ESNTPDriver *driver);
                            ~ESNTPHostReport();
    struct HostNameInfo {
      public:
        std::string             hostNameAsRequested;
        
        struct HostInfo {
          public:
            // Active means we've started trying to use this IP address
            bool                    active() const { return packetsSent > 0; }

            std::string             humanReadableIPAddress;
            int                     packetsSent;
            int                     packetsReceived;
            int                     packetsUsed;
            ESTimeInterval          skewLB;  // Continuous-time skew
            ESTimeInterval          skewUB;  // Continuous-time skew
            ESTimeInterval          nonRTTError;
            int                     leapBits;
            int                     stratum;
            bool                    disabled; // Disabled means we started trying to use this IP address but we later stopped
        };

        bool                    resolving;
        bool                    resolved;
        bool                    failedResolution;

        int                     numberOfHostInfos() const { return _numberOfHostInfos; }
        const HostInfo          *hostInfos() const { return _hostInfos; }

      private:
        int                     _numberOfHostInfos;
        HostInfo                *_hostInfos;
    friend class ESNTPHostReport;
    };

    int                     numberOfHostNameInfos() const { return _numberOfHostNameInfos; }
    const HostNameInfo      *hostNameInfos() const { return _hostNameInfos; }

  private:
    int                     _numberOfHostNameInfos;
    HostNameInfo            *_hostNameInfos;
};

/////////////////////////////

/*! Abstract class for observers of host status reports */
class ESNTPHostReportObserver {
  public:
                            ESNTPHostReportObserver(ESThread *notificationThread = NULL);  // NULL means deliver immediately, don't care what thread
    virtual void            hostReportAvailable(ESNTPHostReport *hostReport) = 0;   // Delete the report (with 'delete') when done

  protected:
    ESThread                *_notificationThread;

  private:
    void                    callHostReportAvailable(ESNTPHostReport *hostReport);

friend class ESNTPDriver;
};

#endif  // _ESNTPHOSTREPORT_HPP_
