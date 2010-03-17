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
#include <boost/thread/tss.hpp>

namespace stldb
{

// A structure representing the configuration of the timers in the STLdb code.
struct BOOST_STLDB_DECL timer_configuration {
	// The percentage of transactions which should turn on time tracking.
	// as a value between 0 and 1.
	double enabled_percent;

	// The frequency with which reported stats should be dumped to stdout.
	static long report_interval_seconds;

	// an indication of whether or not the report should reset all stat values
	// after it is reported.  (Causes each report to show only the data for the
	// report interval instead of a running total.
	static bool reset_after_print;
};


class timer
{
public:
	inline timer(const char *scopename)
		: _enabled(thr_enabled.get() ? *thr_enabled.get() : false)
		, _end(boost::posix_time::not_a_date_time)
	{
		if (_enabled) {
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
		if (_enabled && _end == boost::posix_time::not_a_date_time) {
			end();
		}
	}

	inline void end() {
		if (_enabled) {
			_end = boost::posix_time::microsec_clock::local_time();

			boost::posix_time::time_duration duration = _end - _start;
			_times->add_invocation(duration.total_nanoseconds());

			assert(time_tracked::current_timer.get() == _times);

			time_tracked::current_timer.reset(_parent);
		}
	}

	// enable or disable tracking for the current thread
	static inline void enable(bool value) {
		bool *flag = thr_enabled.get();
		if (!flag) {
			thr_enabled.reset( new bool(value) );
		}
		else {
			*flag = value;
		}
	}

	static inline void configure(const timer_configuration &cfg)
	{
		config = cfg;
	}

	template <class stream>
	static inline void report(stream& s) {
		// check to see if it is time for a summary report of timing data
		boost::posix_time::ptime now = boost::posix_time::microsec_clock::universal_time();
		if ( now > last_report_time
				 + (boost::posix_time::seconds)config.report_interval_seconds )
		{
			// if so print, and reset last_report_time to now.
			time_tracked::print(s, config.reset_after_print);
			last_report_time = now;
		}
	}

private:
	// Current timer configuration (frequency, report interval, etc.)
	static timer_configuration BOOST_STLDB_DECL config;

	// Is time tracking current on for the current thread.
	static boost::thread_specific_ptr<bool> BOOST_STLDB_DECL thr_enabled;

	// date/time of the last dump of accumulated timer data.
	static boost::posix_time::ptime BOOST_STLDB_DECL last_report_time;

	// Is time tracking enabled for this instance.
	bool _enabled;

	// the place where the results of this call will be recorded;
	boost::posix_time::ptime _start;
	boost::posix_time::ptime _end;

	time_tracked* _times;
	time_tracked* _parent;
};

} // namespace

#endif /* TIMER_H_ */
