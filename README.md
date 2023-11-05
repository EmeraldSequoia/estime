This is a library intended for a portable version of various Emerald Sequoia apps.

Note that as of this writing (2023) it is only used by the following apps:

*   The WearOS (aka Android) version of Emerald Chronometer
*   Emerald Observatory
*   Emerald Timestamp

The aim of the library is to provide UTC as accurately as possible,
using NTP on platforms which do not support NTP in the OS, optionally
with intra-leap-second timing for known leap seconds.


A secondary aim is to provide a continuous time base for things like
interval timers and stopwatches, in the presence of time changes
caused by cell-tower switches.


If there are new leap seconds announced, they need to be added to the
table in `src/ESLeapSeconds.cpp` and Timestamp should be udpated
(Observatory and Chronometer should also take advantage of *negative*
leap seconds, if there is one, but as we've never had one, that has
never been tested).
