//
//  ESNTPHostNames.hpp
//
//  Created by Steve Pucci 26 Jan 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESNTPHOSTS_HPP_
#define _ESNTPHOSTS_HPP_

#include <string>
#include <list>

// Opaque for private data
class CountryFarmDescriptor;
class ContinentFarmDescriptor;

/*! Class to iterate over the "appropriate" hosts for the given session,
 *  starting with the most appropriate and working our way through them.
 *
 *  ESNTPHostNamesIterator iter;
 *  while (iter.next()) {
 *      doSomethingWith(iter.host());
 *  }
 */
class ESNTPHostNamesIterator {
  public:
                            ESNTPHostNamesIterator(const char *countryCode);
                            ~ESNTPHostNamesIterator();
    bool                    next();
    void                    reset(const char *countryCode = NULL);

    std::string             host();
    bool                    isUserHost() const { return _isUserHost; }
    int                     proximity() const { return _proximity; } /**< What level of pool host (user=0, country=1, continent=2, global=3, Apple=4) */

    static int              numberOfUserHostNames();
    int                     totalNumberOfHostNames();

    static void             setDeviceCountryCode(const char *countryCode);

  private:
    const CountryFarmDescriptor   *_countryFarmDescriptor;
    int                     _hostIndex;
    bool                    _isUserHost;
    std::string             _host;
    int                     _proximity;
    std::list<std::string>::iterator *_userIterator;
};

class ESNTPHostNames {
  public:
    static void             addUserServer(const std::string &userHostSpecification);  // IP number or hostname
    static void             removeUserServer(const std::string &userHostSpecification);  // IP number or hostname
    static void             clearUserServerList();
    static void             enablePoolHosts();
    static void             disablePoolHosts();
};

#endif  // _ESNTPHOSTS_HPP_
