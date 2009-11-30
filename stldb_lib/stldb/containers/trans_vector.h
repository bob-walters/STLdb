/*
 * trans_vector.h
 *
 *  Created on: Feb 3, 2009
 *      Author: bobw
 */

#ifndef TRANS_VECTOR_H_
#define TRANS_VECTOR_H_

#include <vector>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <stldb/transentry.h>

namespace stldb {

/**
 * A transactional, concurrent vector.
 *
 * The vector has a lock on it, which can be held for a short duration when operations are
 * performed which read or modify entries of the vector.  Support for adding new entries or
 * removing entries is provided, but those operations must hold the lock until the transaction
 * is committed, forcing the transactions to serialize.
 *
 * The rationale for this is that the O(1) random access characteristics of the vector are
 * key to the use of it, and the use of a vector typically means that changes in the vectors
 * allocated capacity, and operations like deleting rows, are typically more expensive than on
 * other container types (e.g. list).  Thus, if you are using a vector, those operations are
 * probably rare compared to the use of random or iterative access to the vector's contents.  I have
 * tried to model transactional performance to match this assumption.
 *
 * Ops which read or modify existing entries establish row-level locks via an indicator on the
 * individual entries.  MCC is provided so that readers are not blocked by uncommitted write locks
 * for pending changes to a row.
 */
template <class T, class Alloc,
         class Lock = boost::interprocess::interprocess_upgradable_mutex,
         class CondVar = boost::interprocess::interprocess_condition >
class trans_vector
	: public std::vector< TransEntry<T>, Alloc>
{
public:
	// TODO...
	trans_vector();
};


} // namespace

#endif /* TRANS_VECTOR_H_ */
