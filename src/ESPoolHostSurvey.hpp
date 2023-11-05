//
//  ESPoolHostSurvey.hpp
//
//  Created by Steve Pucci 04 Jun 2011
//  Copyright Emerald Sequoia LLC 2011. All rights reserved.
//

#ifndef _ESPOOLHOSTSURVEY_HPP_
#define _ESPOOLHOSTSURVEY_HPP_

#include "ESNTPDriver.hpp"  // For macro definition

#ifdef ES_SURVEY_POOL_HOSTS
extern const char *poolHosts[];
extern int numPoolHosts;
#endif // ES_SURVEY_POOL_HOSTS

#endif  // _ESPOOLHOSTSURVEY_HPP_
