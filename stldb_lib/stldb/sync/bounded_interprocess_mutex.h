/*
 * bounded_interprocess_mutex.h
 *
 *  Created on: Jun 4, 2009
 *      Author: rwalter3
 */

#ifndef STLDB_BOUNDED_INTERPROCESS_MUTEX_H_
#define STLDB_BOUNDED_INTERPROCESS_MUTEX_H_

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_recursive_mutex.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>

#include <stldb/stldb.hpp>
#include <stldb/exceptions.h>
#include <stldb/timing/timer.h>

/**
 * Contains a series of extensions to the basic boost::interprocess mutex
 * types which never perform an indefinite/unbounded wait when their lock()
 * methods are caled.  i.e. Overrides the mutexes so that a 'hang' is not possible.
 * Instead, after waiting for some very long time (i.e. a period which is impossible
 * unless some process has died while holding the mutex) the lock() method will
 * throw stldb::lock_timeout_exception (a subtype of interprocess_exception) thereby
 * not changing the exception spec of the mutexes.
 */
namespace stldb {

/**
 * The following public global defines the maximum wait time which
 * applies to the two mutex types below.  So the maximum wait is a process-wide
 * setting, applying to all instances of bounded_interprocess_mutex without
 * distinction.
 */
extern boost::posix_time::time_duration BOOST_STLDB_DECL bounded_mutex_timeout;


/**
 * This is a boost::interprocess::interprocess_mutex which has
 * it's lock() method overloaded to always use a timed wait when it has to
 * block, so that the mutex never blocks indefinitely
 * if some process dies while holding it.
 */
class bounded_interprocess_mutex
	: public boost::interprocess::interprocess_mutex
{
public:
	bounded_interprocess_mutex()
		: boost::interprocess::interprocess_mutex()
		{ }

	void lock() {
		stldb::timer lock_timer("bounded_interprocess_mutex::lock()");
		if ( !this->try_lock() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_mutex::lock(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_lock(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_mutex:lock() timeout");
		}
	}
};

class bounded_interprocess_recursive_mutex
	: public boost::interprocess::interprocess_recursive_mutex
{
public:
	bounded_interprocess_recursive_mutex()
		: boost::interprocess::interprocess_recursive_mutex()
		{ }

	void lock() {
		stldb::timer lock_timer("bounded_interprocess_recursive_mutex::lock()");
		if ( !this->try_lock() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_recursive_mutex::lock(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_lock(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_recursive_mutex:lock() timeout");
		}
	}
};

class bounded_interprocess_upgradable_mutex
	: public boost::interprocess::interprocess_upgradable_mutex
{
public:
	bounded_interprocess_upgradable_mutex()
		: boost::interprocess::interprocess_upgradable_mutex()
		{ }

	void lock() {
		stldb::timer lock_timer("bounded_interprocess_upgradable_mutex::lock()");
		if ( !this->try_lock() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_upgradable_mutex::lock(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_lock(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_upgradable_mutex:lock() timeout");
		}
	}

	void lock_sharable() {
		stldb::timer lock_timer("bounded_interprocess_upgradable_mutex::lock_sharable()");
		if ( !this->try_lock_sharable() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_upgradable_mutex::lock_sharable(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_lock_sharable(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_upgradable_mutex:lock_sharable() timeout");
		}
	}

	void lock_upgradable() {
		stldb::timer lock_timer("bounded_interprocess_upgradable_mutex::lock_upgradable()");
		if ( !this->try_lock_upgradable() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_upgradable_mutex::lock_upgradable(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_lock_upgradable(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_upgradable_mutex:lock_upgradable() timeout");
		}
	}

	void unlock_upgradable_and_lock() {
		stldb::timer lock_timer("bounded_interprocess_upgradable_mutex::unlock_upgradable_and_lock()");
		if ( !this->try_unlock_upgradable_and_lock() )
		{
			// will need to issue a blocking call (wait) to get the lock.
			stldb::timer blocking_lock_timer( "bounded_interprocess_upgradable_mutex::unlock_upgradable_and_lock(blocking)");
			boost::posix_time::ptime deadline(boost::posix_time::microsec_clock::universal_time() +  bounded_mutex_timeout);
			if (! this->timed_unlock_upgradable_and_lock(deadline) )
				throw stldb::lock_timeout_exception("bounded_interprocess_upgradable_mutex:unlock_upgradable_and_lock() timeout");
		}
	}
};

// Boost memory allocation algorithms make use of a 'mutex_family'
// template parameter, which corresponds to this:
struct bounded_mutex_family {
	typedef bounded_interprocess_mutex             mutex_type;
	typedef bounded_interprocess_recursive_mutex   recursive_mutex_type;
	typedef bounded_interprocess_upgradable_mutex  upgradable_mutex_type;
	typedef boost::interprocess::interprocess_condition   condition_type;
};

} // namespace stldb

#endif /* BOUNDED_INTERPROCESS_MUTEX_H_ */
