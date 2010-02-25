/*
 *  transaction.h
 *
 *
 *  Created by Bob Walters on 2/25/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_TRANSACTION_H
#define STLDB_TRANSACTION_H 1

#include <sstream>
#include <set>
#include <map>

#include <boost/intrusive/list.hpp>

#include <stldb/cachetypes.h>
#include <stldb/exceptions.h>
#include <stldb/commit_buffer.h>
#include <stldb/container_proxy.h>
#include <stldb/logger.h>
#include <stldb/timing/timer.h>

#include <boost/any.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_oarchive boost_oarchive_t;
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_oarchive boost_oarchive_t;
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

using boost::intrusive::optimize_size;
using boost::intrusive::list_base_hook;
using boost::interprocess::basic_ovectorstream;


namespace stldb
{

// Forward definition
class Transaction;

/**
 * A transaction accumulates a set of TransactionalOperations, and a TransactionalOperation is any
 * object implementing this interface.  This polymorphic approach is used so that when the
 * transaction is committed, the transaction commit or rollback can be done by a generic
 * bit of code which uses that polymorphism.
 */
class TransactionalOperation: public list_base_hook< optimize_size<false> >
{
public:
	// Essential for this type, as many of the subclasses of this abstract type
	// may want to add their own operator new()/delete() to allow for pooled memory
	// management.
	virtual ~TransactionalOperation()
	{}

	// A TransactionalOperation applies to data in one (or more) containers.
	// This must return the container_name of the [first] container that the operation
	// applies to.  (The value of container_proxy.getName() for the container it applies to)
	virtual const char* get_container_name() const = 0;

	// Every operation has an "op code", which is an int whose meaning is specific to the container
	// type, and thus the operations it supports.  Every operation must implement this method to
	// permit the identification of the operation when it is written to logs.
	virtual int get_op_code() const = 0;

	// Add the appropriate data to the logging buffer stream passed to accurately
	// allow the recovery of this operation in the future via calls to recover()
	// If the operation is one which requires recovery, then it must begin its serialization
	// by calling serialize_header(), followed by any op-specific data.  The subtype should
	// return the number of operations serialized into buffer (either 0 or 1).  0 is valid
	// if the operation is one (like a temporary lock) which required commit-time work, but is
	// not every expected to be written to the log for recovery.
	virtual int add_to_log(boost_oarchive_t &buffer) = 0;

	// Change the container that the operation was performed on to make the change
	// permanent.  This is done while the container is locked.
	virtual void commit(Transaction& trans) = 0;

	// Change the container that the operation was performed on to reverse the
	// operation, leaving the container unchanged.  This is done while the container is locked.
	virtual void rollback(Transaction& trans) = 0;

	// Recover a previously restored transactional operation.  Re-apply
	// the transaction to the container's contents.  If the operation is non-recoverable
	// (i.e. add_to_log() returns 0) it doesn't need to override this method.
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) { }

	// Called when writing an operation out to logs.  Serializes the header of the op
	template <class boost_archive_out>
	void serialize_header(boost_archive_out &s) {
		const char *name = this->get_container_name();
		std::size_t length = strlen(name);
		int opcode = this->get_op_code();
		s & length;
		s.save_binary(name, length);
		s & opcode;
	}

	// Called when restoring operations from disk.  Deserializes the header of the op from the stream,
	// and returns the resulting container name and op code as a pair.
	template <class boost_archive_in>
	static std::pair<std::string,int> deserialize_header(boost_archive_in &s) {
		std::pair<std::string, int> result;
		std::size_t length;
		s & length;
		result.first.resize(length, char(0));
		s.load_binary( const_cast<char*>(result.first.data()), length);
		s & result.second;
		return result;
	}
};

typedef boost::intrusive::list<TransactionalOperation> trans_op_list_type;

struct disposer
{
   void operator()(TransactionalOperation *delete_this)
   {  delete delete_this;  }
};

/**
 * A list of transactional operations, and the code needed to carry out aggregate
 * calls to each item in the list.  Uses boost.intrusive slist<>
 */
class transactional_op_list: public trans_op_list_type
{
public:
	explicit transactional_op_list(void) :
		trans_op_list_type()
	{}

	// Necessary, because this class is used as a value type in a map,
	// and std::map appears to require a copy constructor.  In this case,
	// it is allowed to work as long as the list being copied is empty.
	transactional_op_list(const transactional_op_list &rarg) :
		trans_op_list_type()
	{
		assert(rarg.size()==0);
	}

	// Destructor is needed to see to the proper destruction of all list elements
	// as if std::list::clear() was done.
	~transactional_op_list()
	{
		this->clear();
	}

	// Destroy all contained list elements.
	void clear()
	{
		this->trans_op_list_type::clear_and_dispose<disposer>(disposer());
	}

	/* perform the commit() operation for each contained TransactionalOperation */
	void commit(Transaction& trans)
	{
		for (trans_op_list_type::iterator i = this->begin(); i != this->end(); i++)
		{
			i->commit(trans);
		}
	}

	/* perform the rollback() operation for each contained TransactionalOperation.
	 * Note that rollback is done by applying the rollback call in reverse order.
	 * Some algorithms, like the application of multiple swap()s, must be rolled back
	 * in this way to return to the original contents. */
	void rollback(Transaction& trans)
	{
		for (trans_op_list_type::reverse_iterator i(this->rbegin()); i != this->rend(); i++)
		{
			i->rollback(trans);
		}
	}

	/* perform the log() operation for each contained TransactionalOperation */
	int add_to_log(boost_oarchive_t &buffer)
	{
		int count=0;
		for (trans_op_list_type::iterator i = this->begin(); i != this->end(); i++)
		{
			count += i->add_to_log(buffer);
		}
		return count;
	}

private:
	// The intrusive set is non-copyable.
	transactional_op_list
			& operator=(const transactional_op_list &rarg);
};


/**
 * @brief Transaction represents the details of an in-progress transaction, 
 * including modifications which are in progress.  A transaction is created 
 * by the Database class, acting as a factory.  A Transaction is resolved 
 * by passing it to either the commit or rollback method of the Database.  
 * This was done to prevent the need for the Transaction
 * type to itself be a template class.
 */
class Transaction
{
public:
	/**
	 * Most of the work associated with commit is actually done by the 
	 * polymorphic TransactionalOperation objects which accumulate in the 
	 * Transaction. Commit must lock each container in turn and hold all 
	 * locks until all operations are done.  (This is required to meet the 
	 * ACI part of ACID.)  To ensure that multiple commits don't deadlock, 
	 * they all must consistently lock
	 * containers in the same order.  The ordering of 
	 * _outstandingChanges assures this.
	 */
	template<class ManagedRegionType, class void_alloc_t,class mutex_t>
	void commit( std::map<void*, container_proxy_base<ManagedRegionType>*>& proxies,
				 Logger<void_alloc_t,mutex_t> &logger,
				 bool diskless,
				 commit_buffer_t<void_alloc_t>* buffer);

	/**
	 * Most of the work associated with rollback is likewise done by the 
	 * polymorphic TransactionalOperation objects which accumulate in the 
	 * Transaction.
	 */
	template<class ManagedRegionType>
	void rollback( std::map<void*, container_proxy_base<ManagedRegionType>*>& proxies );

	//! Return the lock_id of this transaction.
	transaction_id_t getLockId() const
	{ return _transId; }

	//! Return the lsn assigned to this transaction.  LSN assignment 
	/// occurs during commit processing.
	transaction_id_t getLSN() const
	{ return _commit_lsn; }

	// To add an operation to the _outstandingChanges, the following is used:
	template<class container_t>
	void insert_work_in_progress(container_t *container, TransactionalOperation *op);

	//! Returns the transactional_op_list of outstanding operations.  
	//! There are some
	//! operations like swap() or clear() which might need to adjust 
	//! information on
	//! previous operations against the same container as part of their 
	//! implementation.
	transactional_op_list& get_work_in_progress()
	{ return _outstandingChanges; }

	/**
	 * Return the pending changes container (some subclass of PendingChangeSet)
	 * for the container ref passed.  If none exists, and create_flag = true, 
	 * create one.
	 */
	template<class T, class container_t>
	T* getContainerSpecificData(container_t *c);

	// Destroy the underlying members.
	virtual ~Transaction() { }

protected:
	// Only allowed by Database or derrived.
	Transaction()
		: _lock(), _transId(0), _commit_lsn(0)
		, _modifiedContainers(), _outstandingChanges(), _containerSpecificData()
		{ }

private:
	// Not allowed/implemented
	Transaction(const Transaction &t);
	Transaction& operator=(const Transaction &t);

	// but Database is allowed to create these
	template<class Region> friend class Database;

	// acquire a sharable lock on the database's transaction mutex.
	virtual void lock_database( boost::interprocess::interprocess_upgradable_mutex &mutex ) {
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex> lock(mutex);
		_lock.swap( lock );
	}

	virtual void unlock_database() {
		_lock.unlock();
	}

	void clear(void) {
		// These routines call destructors on the intrusive lists holding the transactional
		// ops, and thereby eventually end up calling the virtual destructors on the
		// TransactionOp objects, making it possible to use their destructors as final
		// clean-up methods.  This call to clear must always be done, to guarantee that
		// that clean-up can fire before the transaction object itself is subsequently reused.
		_modifiedContainers.clear();
		_outstandingChanges.clear();
		_containerSpecificData.clear();
	}

	// The transactions lock on the db->transaction mutex;
	boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex> _lock;

	// The lock_id used for the transaction prior to commit.
	transaction_id_t _transId;

	// The transaction id used for permanence when the transaction commits.
	transaction_id_t _commit_lsn;

	// The set of containers which have been modified during this transaction.
	std::set<void*> _modifiedContainers;

	// This is a list of all the changes made
	transactional_op_list _outstandingChanges;

	// Each container can ask the transaction to hold some objects of 
	// data which the container is presumed to use when committing.  
	// Where I found this valuable was with MVCC
	// implementations.  Allowing the transaction to hold pending, 
	// uncommited new values related to
	// that transaction.  The value can be anything the container needs.
	std::map<void*, boost::any> _containerSpecificData;
};


/**
 * An exclusive transaction is a transaction, but the database guarantees that only one
 * transaction of this type can be in progress at any one time.
 */
class exclusive_transaction : public Transaction
{
public:
	virtual ~exclusive_transaction() { }

private:
	// Only allowed by Database
	exclusive_transaction()
		: Transaction(), _lock()
		{ }

	// Not allowed/implemented
	exclusive_transaction(const exclusive_transaction &t);
	exclusive_transaction& operator=(const exclusive_transaction &t);

	// but Database is allowed to create these
	template<class Region> friend class Database;

	// acquire an exclusive lock on the database's transaction mutex.
	virtual void lock_database( boost::interprocess::interprocess_upgradable_mutex &mutex ) {
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex> lock(mutex);
		_lock.swap( lock );
	}
	virtual void unlock_database() {
		_lock.unlock();
	}

private:
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex> _lock;
};


/**
 * Return the pending changes container (some subclass of PendingChangeSet) for the
 * container ref passed.  If none exists, and create_flag = true, create one.
 * Note: Although this returns T*, the transaction object retains ownership of the
 * object.  When _pendingChanges is destroyed (by the transaction destructor), the
 * boost::any values within that map are also destroyed which in turn destroys all
 * container specific data held by the transaction.
 */
template<class T, class container_t>
T* Transaction::getContainerSpecificData(container_t *c)
{
	boost::any& value = _containerSpecificData[c];
	if (value.empty())
	{
		value = T();
	}
	return boost::any_cast<T>(&value);
}


template<class container_t>
void Transaction::insert_work_in_progress(container_t *c, TransactionalOperation *op)
{
	stldb::timer t1("Transaction::insert_work_in_progress()");
	_modifiedContainers.insert(c); // may already be there, which is ok.
	_outstandingChanges.push_back( *op );
}


/**
 * Most of the work associated with commit is actually done by the polymorphic
 * TransactionalOperation objects which accumulate in the Transaction.
 * Commit must lock each container in turn and hold all locks until all operations
 * are done.  (This is required to meet the ACI part of ACID.)
 * To ensure that multiple commits don't deadlock, they all must consistently lock
 * containers in the same order.  The ordering of _outstandingChanges assures this.
 */
template<class ManagedRegionType, class void_alloc_t, class mutex_t>
void Transaction::commit( std::map<void*, container_proxy_base<ManagedRegionType>*>& proxies,
						  Logger<void_alloc_t,mutex_t> &logger,
						  bool diskless,
						  commit_buffer_t<void_alloc_t>* buffer )
{
	if (_outstandingChanges.size()==0) {
		logger.record_diskless_commit();
		STLDB_TRACE(finer_e, "Committed transaction " << _transId << " with 0 operations");
		return ;
	}

	stldb::timer t1("Transaction::commit()");
	if (!diskless) {
		/* Phase 1 - serialize the commit buffer content.  
		 * Done concurrently - no locks. */

		stldb::timer t2("add_to_log(all changes)");
		/* There's no direct way to construct vstream so that it will use 
		 * buffer.  However it can initially construct a vector of size 0, 
		 * and then we can swap() with buffer so that it will use that. This 
		 * is aimed at minimizing shared mem allocations
		 * It assumes the initial constructor does not allocate anything since 
		 * the size()==0
		 */
		boost::interprocess::basic_ovectorstream<commit_buffer_t<void_alloc_t> >
			vstream(buffer->get_allocator());
		vstream.swap_vector(*buffer);
		boost_oarchive_t bstream(vstream);

		buffer->op_count = _outstandingChanges.add_to_log(bstream);

		// after this, buffer is itself once again, and now contains our data
		vstream.swap_vector(*buffer);
		STLDB_TRACE(finer_e, "Committing transaction " << _transId << " with " << buffer->op_count << " operations, " << buffer->size() << " bytes of log");
	}

	std::set<void*>::iterator j;
	//bool committed_containers = false;

	/* Phase 2 - get a unique commit seq#, and put our commit buffer into
	 * the logging queue.  The queue is used to allow I/O aggregation
	 * within the log() call.  At this point, we are reserving our place 
	 * in the sequence of the log file.
	 */
	if (!diskless) {
		_commit_lsn = logger.queue_for_commit(buffer);
	}

	if (diskless) {
		logger.record_diskless_commit();
	}
	else {
		/* Now write all pending log records into the log, and sync, if
		 * if appropriate.
		 */
		STLDB_TRACE(finer_e, "Committing transaction " << _transId << " to log with LSN  " << _commit_lsn);
		logger.log(_commit_lsn);
	}

	/* An exception in the code above we have already queued for commit
	 * suggests that the disk is full, or some other exception beyond
	 * our control is at work.  We let those exceptions go up to the caller.
	 * The client can either commit without disk (diskless=true) and
	 * risk loosing transactions if they can't complete another
	 * checkpoint, or they can call rollback and report failure.
	 */

	try {
		/* Phase 3 - make the changes within the various containers permanent,
		 * by getting all needed container locks (in predictable order), 
		 * to thereby
		 * assure the order in which changes become visible.
		 */
		stldb::timer t3a("container commit work (container lock duration)");
		stldb::timer t3b("acquiring container locks");
		for ( j = _modifiedContainers.begin(); j != _modifiedContainers.end(); j++ )
		{
			// acquire locks on the container.
			proxies.find(*j)->second->initializeCommit(*this);
		}
		t3b.end();

		stldb::timer t3c("committing transactional operations");
		_outstandingChanges.commit(*this);
		t3c.end();

		/**
		 * Release all container locks.  It is now acceptable for other
		 * threads to begin doing work on top of our committed data because
		 * they can't jump ahead of us in the logging queue.
		 */
		do {
			proxies.find(*(--j))->second->completeCommit(*this);
		} while ( j != _modifiedContainers.begin() );
		t3a.end();

		// Now clean up.
		clear();
	}
	catch (std::exception &ex) {
		// in the event of any exception, step 4 might still need to be done
		// to release the locks on the containers, and avoid hung locks.
		// An exception during commit is generally bad.  However, a
		// lock timeout exception during phase 2 is recoverable.
		if ( j != _modifiedContainers.begin() )
			do {
				proxies.find(*(--j))->second->completeCommit(*this);
			} while ( j != _modifiedContainers.begin() );

		// if (committed_containers) then
		// we passed the point of no return, having made the changes
		// to the containers visible to other threads, but we failed
		// to write the needed disk log.  Currently, the only way to
		// recover from that discrepancy is via recovery.
		throw stldb::recovery_needed( ex.what() );
	}
}



/**
 * Most of the work associated with rollback is likewise done by the polymorphic
 * TransactionalOperation objects which accumulate in the Transaction.
 */
template<class ManagedRegionType>
void Transaction::rollback(std::map<void*, container_proxy_base<ManagedRegionType>*>& proxies)
{
	stldb::timer t1("Transaction::rollback()");

	std::set<void*>::iterator j;
	try {
		/* Phase 1 - make the changes to the various containers revert to their original state. */
		for ( j = _modifiedContainers.begin(); j != _modifiedContainers.end(); j++ ) {
			proxies.find(*j)->second->initializeRollback(*this);
		}

		/* Phase 2 - rollback container changes */
		_outstandingChanges.rollback(*this);

		/* Phase 3 - release locks */
		do {
			proxies.find(*(--j))->second->completeRollback(*this);
		} while ( j != _modifiedContainers.begin() );

		// Now clean up.
		clear();
	}
	catch (...) {
		// In the event of any exception, step 3 might still need to be done
		// to release the locks on the containers, and avoid hung locks.
		// An exception during commit is generally bad.  However, a
		// lock timeout exception during phase 2 is recoverable.
		if ( j != _modifiedContainers.begin() )
			do {
				proxies.find(*(--j))->second->completeRollback(*this);
			} while ( j != _modifiedContainers.begin() );

		throw;
	}

}


}; // stldb namespace

#endif

