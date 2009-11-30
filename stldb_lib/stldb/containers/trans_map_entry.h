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
	Clear_op,
	Swap_op
};

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
	// Default Constructor
	TransEntry() :
		T(), _op(No_op), _txn_id(0)
	{
	}

	// Well formed copy constructor
	TransEntry(const TransEntry<T> &rarg) :
		T(rarg), _op(rarg._op), _txn_id(rarg._txn_id)
	{
	}

	// Copy from T.
	TransEntry(const T& rarg) :
		T(rarg), _op(No_op), _txn_id(0)
	{
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
		return *this;
	}

	TransEntry<T>&
	operator=(const T& value)
	{
		T::operator=(value);
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

	// Boost::serialization
	template<class Archive>
	void serialize(Archive &ar, const unsigned int version)
	{
	    // invoke serialization of the base class
	    ar & boost::serialization::base_object<T>(*this);
	}

private:
	// Pending operation.
	TransactionalOperations _op :4;

	// If the entry is currently locked for update, then this is the
	// transaction_id_t of the transaction which has the lock in place.
	// Otherwise, it is the transaction_id_t of the last committed
	// transaction.
	transaction_id_t _txn_id :60;
};

} // namespace

#endif
