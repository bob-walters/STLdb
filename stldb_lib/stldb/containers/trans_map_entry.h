/*
 *  TransEntry.h
 *  ACIDCache
 *
 *  Created by Bob Walters on 2/24/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_TRANSENTRY_H
#define STLDB_TRANSENTRY_H 1

#include <stldb/cachetypes.h>
#include <stldb/logging.h>
#include <stldb/transaction.h>
#include <boost/serialization/base_object.hpp>

namespace stldb
{

enum TransactionalOperations
{
	No_op = 0, // no outstanding operation.
	Lock_op = 1, // row was locked.  No other change.
	Insert_op, // row was inserted into the container.
	Update_op, // row has a pending update planned.
	Delete_op, // row has a pending delete planned.
	Deleted_Insert_op, // delete of a pending insert.
	Clear_op,
	Swap_op
};

#ifdef STLDB_TROUBLESHOOT
#define SET_CHECKSUM()  _checksum = _checkpoint_location.first*19 + (_checkpoint_location.second * 131);
#define CHECK_CHECKSUM()  BOOST_ASSERT( _checksum == _checkpoint_location.first*19 + (_checkpoint_location.second * 131) );
#else
#define SET_CHECKSUM()
#define CHECK_CHECKSUM()
#endif

/**
 * @brief Helper class for representing a value of type T in a transactional STL container.
 * TransEntry<T> is a public T.  It adds a txn_id, and operation members that represent
 * the transactional state of the data in T.
 * If op_id != No_op, then the row has some pending operation in effect on it.
 */
template<typename T>
class TransEntry: public T
{
public:
	typedef T actual_type;

	// Default Constructor
	TransEntry() :
		T(), _op(No_op), _txn_id(0), _checkpoint_location(0,0)
	{
		SET_CHECKSUM();
	}

	// Well formed copy constructor
	TransEntry(const TransEntry<T> &rarg) :
		T(rarg), _op(rarg._op), _txn_id(rarg._txn_id), _checkpoint_location(rarg._checkpoint_location)
	{
		SET_CHECKSUM();
#ifdef STLDB_TROUBLESHOOT
		BOOST_ASSERT(rarg._checksum == _checksum);
#endif
	}

	// Copy from T.
	TransEntry(const T& rarg) :
		T(rarg), _op(No_op), _txn_id(0), _checkpoint_location(0,0)
	{
		SET_CHECKSUM();
	}

	~TransEntry()
	{
	}

	TransEntry<T>&
	operator=(const TransEntry<T>& value)
	{
		T::operator=(value);
		_op = value._op;
		_txn_id = value._txn_id;
		// protect against loss of checkpoint info via assignment
		BOOST_ASSERT(!(_checkpoint_location.first != 0 && value._checkpoint_location.first==0));
		_checkpoint_location = value._checkpoint_location;
		SET_CHECKSUM();
#ifdef STLDB_TROUBLESHOOT
		BOOST_ASSERT(value._checksum == _checksum);
#endif
		return *this;
	}

	// update to new value without changing checkpoint, _op, etc.
	TransEntry<T>&
	operator=(const T& value)
	{
		T::operator=(value);
		CHECK_CHECKSUM();
		return *this;
	}

	T&
	base() {
		return *this;
	}

	const T&
	base() const {
		return *this;
	}

	// Lock the row, setting its op_id to the op code provided, and its
	// txn_id to the value of the transaction.
	transaction_id_t lock(Transaction &t, TransactionalOperations op)
	{
	    transaction_id_t current_value = _txn_id;
		_op = op;
		_txn_id = t.getLockId();
		return current_value;
	}

	// Unlock the row, setting its transaction_id to the value passed.
	// Set the _op value back to No_op.  On a commit, the txn_id passed
	// is the commit txn_id of the transaction.  On a rollback, it will
	// be the previous txn_id returned from lock().
	void unlock(transaction_id_t txn_id, TransactionalOperations op = No_op)
	{
		_txn_id = txn_id;
		_op = op;
	}

	TransactionalOperations getOperation() const
	{
		return _op;
	}

	transaction_id_t getLockId() const
	{
		return _txn_id;
	}

	std::pair<boost::interprocess::offset_t,std::size_t> checkpointLocation() const
	{
		CHECK_CHECKSUM();
		return _checkpoint_location;
	}

	void setCheckpointLocation(std::pair<boost::interprocess::offset_t,std::size_t> loc)
	{
		_checkpoint_location = loc;
		SET_CHECKSUM();
	}

	// Boost::serialization
	template<class Archive>
	void save(Archive &ar, const unsigned int version) const
	{
		transaction_id_t txn_id = _txn_id;
	    ar & boost::serialization::base_object<T>(*this) & txn_id;
	}

	template<class Archive>
	void load(Archive &ar, const unsigned int version)
	{
		transaction_id_t txn_id;
	    ar & boost::serialization::base_object<T>(*this) & txn_id;
	    _txn_id = txn_id;
	}

	BOOST_SERIALIZATION_SPLIT_MEMBER()

private:
	// Pending operation.
	TransactionalOperations _op :4;

	// If the entry is currently locked for update, then this is the
	// transaction_id_t of the transaction which has the lock in place.
	// Otherwise, it is the transaction_id_t of the last committed
	// transaction.
	transaction_id_t _txn_id :60;

	// the location and size of this entry in the checkpoint file. (0,0 = not in checkpoint)
	std::pair<boost::interprocess::offset_t, std::size_t> _checkpoint_location;

#ifdef STLDB_TROUBLESHOOT
	// for testing...
	uint32_t _checksum;
#endif

};

} // namespace

#endif
