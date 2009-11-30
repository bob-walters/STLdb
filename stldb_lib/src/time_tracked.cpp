/*
 * time_tracked.cpp
 *
 *  Created on: Jun 1, 2009
 *      Author: bobw
 */
#define BOOST_STLDB_SOURCE

#include <stldb/timing/time_tracked.h>

namespace stldb {

// We need a delete function for the thread_specific_ptr which does nothing, because
// the memory is shared for the whole process.
void cleanup_times(time_tracked* item) { };

// The root time_tracked object for the whole process (root of tree)
boost::thread_specific_ptr<time_tracked> time_tracked::thread_root(&cleanup_times);

// The current time_tracked object to be updated by timers when their destructors are
// called.  Moves up and down the tree as local timer objects are constructed.
boost::thread_specific_ptr<time_tracked>  time_tracked::current_timer(&cleanup_times);

process_time_tracked process_time_tracked::process_root;

} // namespace

