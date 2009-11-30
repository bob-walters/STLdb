/*
 * exceptions.h
 *
 *  Created on: Jun 3, 2009
 *      Author: rwalter3
 */

#ifndef STLDB_EXCEPTIONS_H_
#define STLDB_EXCEPTIONS_H_

#include <exception>
#include <boost/interprocess/exceptions.hpp>
#include <stldb/cachetypes.h>

namespace stldb
{

//!As a convention, all exceptions thrown by STLdb inherit from this base,
//!which in turn inherits from std::exception.
class stldb_exception : public boost::interprocess::interprocess_exception
{
public:
	stldb_exception()
		: boost::interprocess::interprocess_exception(), _why() { }
	stldb_exception(const char *why) throw ()
		: boost::interprocess::interprocess_exception(), _why(why) { }
	virtual ~stldb_exception() throw ()
		{ }
	virtual const char* what() const throw()
		{ return _why.c_str(); }

private:
	std::string _why;
};


//!Exception thrown when a transaction attempts to modify a
//!row which has been deleted by another thread/transaction.
//!This exception is only thrown from blocking methods.  Upon
//!discovering that the row indicated by an iterator is already
//!locked by another thread, the method will block until that
//!lock is resolved (via transaction commit/rollback), at that
//!point if the entry that the iterator was on is deleted by
//!that transaction, the blocking method fails, throwing this
//!exception.
class row_deleted_exception : public stldb_exception
{
public:
	row_deleted_exception() : stldb_exception() { }
	row_deleted_exception(const char *why) : stldb_exception(why) { }
};

//!Exception thrown when a transaction attempts to modify a
//!row which has an outstanding, uncommitted, row-level lock
//!held by another thread/transaction in a non-blocking
//!method.  i.e. Rather than block to wait for the row to become
//!unlocked, this exception is thrown.
class row_level_lock_contention : public stldb_exception
{
public:
	row_level_lock_contention() : stldb_exception() { }
	row_level_lock_contention(const char *why) : stldb_exception(why) { }
};

//!Exception type thrown when a blocking wait is done with a timeout.
//!If the timeout elapses before the lock or condition is acquired,
//!An exception of this type is thrown.
class lock_timeout_exception : public stldb_exception
{
public:
	lock_timeout_exception() : stldb_exception() { }
	lock_timeout_exception(const char *why) : stldb_exception(why) { }
};

//!Exception type thrown when a Database is found to have been compromised
//!because of failed processes which may have left hung locks, or because
//!the boost::interprocess::region check_sanity() method returns false.
class recovery_needed : public stldb_exception
{
public:
	recovery_needed() : stldb_exception() { }
	recovery_needed(const char *why) : stldb_exception(why) { }
};

//!Exception thrown when there is a failure to recover transactions from
//!a log file.  There are several fields of usefull data intended to inform
//!the caller exactly where the problem was encountered.
class recover_from_log_failed : public stldb_exception
{
public:
	recover_from_log_failed() : stldb_exception() { }
	recover_from_log_failed(const char *why, transaction_id_t txn,
			boost::uintmax_t loc, const char *filename)
		: stldb_exception(why), _txnid(txn)
		, _offset(loc), _filename(filename) { }

	virtual ~recover_from_log_failed() throw() { }
	transaction_id_t failed_txn() const { return _txnid; }
	boost::uintmax_t offset_in_file() const { return _offset; }
	const char *filename() const { return _filename.c_str(); }
private:
	transaction_id_t _txnid;
	boost::uintmax_t _offset;
	std::string _filename;
};


} // namespace


#endif /* EXCEPTIONS_H_ */
