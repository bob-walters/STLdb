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
 * A transaction is given a unique ID number representing the transaction.
 *
 * transaction_ids are typically used to mark rows which have been locked
 * by a transaction, and thereby to identify the lock owner.
 */
typedef int64_t transaction_id_t;

/**
 * During the commit of a transaction, the transaction is assigned a
 * logging sequence number (LSN) which corresponds to the order in which the
 * transactions have been serialized to the log.  LSNs on transaction
 * in the log are always in order of ascending LSNs.
 *
 * The last committed LSN value can be stored on entries in containers
 * and used to determine changes during checkpoints.
 * This number can't loop around safely, so a 64-bit int is used.
 */
typedef int64_t log_seq_num_t;

static const transaction_id_t no_transaction = transaction_id_t(-1);

}

#endif /* CACHETYPES_H_ */

