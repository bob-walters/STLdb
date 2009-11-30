/*
 * timer.h
 *
 *  Created on: Jun 1, 2009
 *      Author: rwalter3
 */

#ifndef TIMER_H_
#define TIMER_H_

#include <stldb/stldb.hpp>

#include <stldb/timing/time_tracked.h>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace stldb
{

class timer
{
public:
	inline timer(const char *scopename)
		: _end(boost::posix_time::not_a_date_time)
	{
		if (enabled) {
			_parent = time_tracked::current_timer.get();
			if (_parent == NULL) {
				_parent = time_tracked::get_root();
			}
			_times = _parent->get_child(scopename);
			time_tracked::current_timer.reset(_times);

			_start = boost::posix_time::microsec_clock::local_time();
		}
	}

	inline ~timer() {
		if (enabled && _end == boost::posix_time::not_a_date_time) {
			end();
		}
	}

	inline void end() {
		if (enabled) {
			_end = boost::posix_time::microsec_clock::local_time();

			boost::posix_time::time_duration duration = _end - _start;
			_times->add_invocation(duration.total_nanoseconds());

			assert(time_tracked::current_timer.get() == _times);

			time_tracked::current_timer.reset(_parent);
		}
	}

	// enables/disabled timers on a process-wide basis
	static BOOST_STLDB_DECL bool enabled;

private:
	// the place where the results of this call will be recorded;
	boost::posix_time::ptime _start;
	boost::posix_time::ptime _end;

	time_tracked* _times;
	time_tracked* _parent;
};

} // namespace

#endif /* TIMER_H_ */
