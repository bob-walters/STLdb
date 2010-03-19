/*
 * timer.cpp
 *
 *  Created on: Jun 2, 2009
 *      Author: rwalter3
 */

#define BOOST_STLDB_SOURCE

#include <stldb/stldb.hpp>

#include <stldb/timing/timer.h>

namespace stldb
{

// Current timer configuration (frequency, report interval, etc.)
timer_configuration BOOST_STLDB_DECL timer::config;

// Is time tracking current on for the current thread.
boost::thread_specific_ptr<bool> BOOST_STLDB_DECL timer::thr_enabled;

// date/time of the last dump of accumulated timer data.
boost::posix_time::ptime BOOST_STLDB_DECL timer::last_report_time
		= boost::posix_time::microsec_clock::universal_time();


}
