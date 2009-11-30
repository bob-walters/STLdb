/*
 * cachetypes.h
 *
 *  Created by Bob Walters on 2/24/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 */

#ifndef CACHETYPES_H_
#define CACHETYPES_H_

#include <cstdlib>

#ifdef NO_STDINT_H
#include <stldb/detail/ints.h>
#else
#include <stdint.h>
#endif

namespace stldb
{

/**
 * A transaction is given a unique ID number which
 *
 * A transaction which has been committed is given a unique integer transaction_id, which in turn can be
 * used to mark the tables to indicate the last committed transaction.  This value is used in snapshots
 * and logs, and effectively indicates the total number of transactions which have been committed against
 * a particular database.  This number can't loop around safely, so a 64-bit int is used.
 *
 * A transaction which is in progress is identified by an integer which I've decided to call
 * a lock_id.  This is because it is most typically used to mark rows in shared data which the
 * transaction has locked.  The "lock_id" is an id assigned to a transaction when it is started,
 * and a 'transaction_id' is an id which is assigned when the transaction commits.  The reason for the
 * two identifiers is because transactions can start and commit in different orders, and it would
 * complicate the tracking of committed transactions if one one ID was used.
 */
typedef int64_t transaction_id_t;

static const transaction_id_t no_transaction = transaction_id_t(-1);

}

#endif /* CACHETYPES_H_ */
