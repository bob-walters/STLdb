/*
 * wait_policy.h
 *
 *  Created on: Jun 3, 2009
 *      Author: rwalter3
 */

#ifndef STLDB_WAIT_POLICY_H_
#define STLDB_WAIT_POLICY_H_

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <stldb/exceptions.h>

namespace stldb {

// wait policy concept - wait indefinitely for a row lock to be released.
// This is constructed with the lock object that currently holds the container-level lock.
// If a row-level lock collision is detected by the container, it will issue specifically
// orchestrated calls to unlock_container, relock_container, and wait, in order to coordinate
// the act of waiting for the row level lock held by another transaction.
template <class container_lock_t>
class wait_indefinitely_policy {
public:
	wait_indefinitely_policy(container_lock_t &lock) : _lock(lock) { }

	void unlock_container() {
		_lock.unlock();
	}
	void relock_container() {
		_lock.lock();
	}

	template <class row_mutex_t, class condition_t>
	void wait(boost::interprocess::scoped_lock<row_mutex_t> &mutex, condition_t &condition)
	{
		// wait for txns to commit/rollback, thus releasing some row locks.
		stldb::timer t("wait_indefinitely_policy::wait()");
		condition.wait(mutex);
	}

private:
	container_lock_t &_lock;
};

// wait policy concept.  wait for up to a maximum indicated duration.  If the process
// actually waits for more than that amount of time, throw a lock_timeout_exception.
// This is constructed with the lock object that currently holds the container-level lock.
// If a row-level lock collision is detected by the container, it will issue specifically
// orchestrated calls to unlock_container, relock_container, and wait, in order to coordinate
// the act of waiting for the row level lock held by another transaction.
template <class container_lock_t>
class bounded_wait_policy {
public:
	bounded_wait_policy(container_lock_t &lock, const boost::posix_time::time_duration& max_wait)
		: _lock(lock), _max_wait(max_wait)
		{ }

	void unlock_container() {
		_lock.unlock();
	}
	void relock_container() {
		_lock.lock();
	}
	template <class row_mutex_t, class condition_t>
	void wait(boost::interprocess::scoped_lock<row_mutex_t> &held_lock, condition_t &condition)
	{
		// wait for txns to commit/rollback, thus releasing some row locks.
		stldb::timer t("bounded_wait_policy::wait()");
		boost::posix_time::ptime deadline( boost::posix_time::microsec_clock::universal_time() + _max_wait);
		if (!condition.timed_wait(held_lock, deadline)) {
			throw lock_timeout_exception("waiting for row-level lock release");
		}
	}

private:
	container_lock_t &_lock;
	boost::posix_time::time_duration _max_wait;
};

} // } namespace stldb;

#endif /* WAIT_POLICY_H_ */
