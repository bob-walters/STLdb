/*
 * bounded_interprocess_mutex.cpp
 *
 *  Created on: Jun 4, 2009
 *      Author: rwalter3
 */
#define BOOST_STLDB_SOURCE

#include <stldb/sync/bounded_interprocess_mutex.h>

namespace stldb {

boost::posix_time::time_duration  bounded_mutex_timeout( boost::posix_time::seconds(10) );

} // namespace
