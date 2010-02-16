/*
 * PlainMapExample.h
 *
 *  Created on: Dec 30, 2009
 *      Author: bobw
 */

//[ dbtypes
#include <string>
#include <map>

#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>

#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/Database.h>
#include <stldb/containers/trans_map.h>
// This addresses an outstanding bug with boost::interprocess::map::swap(other) when the map
// uses a cached_node_allocator.
#include <stldb/allocators/swap_workaround.h>
// This provides boost.serialization support for boost.interprocess.basic_string<>
#include <stldb/containers/string_serialize.h>

using boost::interprocess::managed_shared_memory;
using boost::interprocess::scoped_lock;
using boost::interprocess::sharable_lock;

using stldb::Database;
using stldb::Transaction;

typedef long date_t;  // days since epoch.

// String in shared memory, based on boost::interprocess::basic_string
// Allocator of char in shared memory, with support for default constructor
typedef boost::interprocess::basic_string<char, std::char_traits<char>,
	stldb::scope_aware_allocator< boost::interprocess::allocator<
		char, managed_shared_memory::segment_manager> > > shm_string;

// customer record for my shoe store
struct customer_t {
	shm_string name;
	int shoe_size;
	date_t last_purchase_date;

	// serialization support
	template<class Archive>
	void serialize(Archive &ar, unsigned int){
		ar & name & shoe_size & last_purchase_date;
	}
};

// account record for customers.
struct account_t {
	int account_num;
	shm_string customer_name;
	date_t account_start_date;
	long balance;
	date_t last_payment_date;

	void add_purchase( double amount ) {
		balance += amount;
	}
	void add_payment( double amount ) {
		balance -= amount;
	}

	// serialization support
	template<class Archive>
	void serialize(Archive &ar, unsigned int){
		ar & customer_name & account_num & account_start_date
		   & balance & last_payment_date;
	}
};
//]
//[dbmaptypes
// "table" types in my database.
typedef stldb::trans_map<shm_string, customer_t, std::less<shm_string>,
	boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, customer_t>,
		managed_shared_memory::segment_manager> >  customer_map_t;

typedef stldb::trans_map<shm_string, account_t, std::less<shm_string>,
	boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, account_t>,
		managed_shared_memory::segment_manager> >  account_map_t;

typedef customer_map_t::iterator customer_ref;
typedef account_map_t::iterator account_ref;
//]

//[dbmain1
// operations...
int main(int argc, const char *argv[])
{
	// Create/Open/Recover the database region
	// We define the "schema" of the database as an argument for the constructor.
	// This is to support recovery processing or creation during the connection.
	std::list< stldb::container_proxy_base<managed_shared_memory>* > containers;
	containers.push_back( new stldb::container_proxy<managed_shared_memory,customer_map_t>("Customers"));
	containers.push_back( new stldb::container_proxy<managed_shared_memory,account_map_t>("Accounts"));

	// Connect to / Create / Recover the database, as appropriate.
	Database<managed_shared_memory> db( stldb::open_create_or_recover
		, "ShoeDatabase" // the name of this database (used for region name)
		, "/tmp"     // the location of metadata & lock files
		, 268435456  // the maximum database size, determines the region size.
		, NULL       // fixed mapping address (optional)
		, "/tmp"     // where the persistent containers are stored
		, "/tmp"     // where the log files are written
		, 268435456  // maximum individual log file length
		, true       // synchronous_logging
		, containers // the containers you want created/opened in the database
	);
//]
//[dbmain2
	//Get pointers to the maps in the database, permitting direct use of the maps.
	customer_map_t *cust_map = db.getContainer<customer_map_t>("Customers");
	account_map_t *account_map = db.getContainer<account_map_t>("Accounts");

	// This object ensures that stldb::scope_aware_allocators constructed hereafter will use
	// the Database 'segment' for their shared region.  Thus the shm_string members end up properly created in the
	// shared memory.
	stldb::scoped_allocation<boost::interprocess::managed_shared_memory::segment_manager> scope(
			db.getRegion().get_segment_manager());

	shm_string name = "John Smith";  // key for this example.
//]
//[tran0
	// 1) create a customer and account within the database
	// All changes to a map must be done under the control of a transaction.
	Transaction *txn = db.beginTransaction();
	{
		customer_t cust;
		cust.name = name;
		cust.shoe_size = 8;

		// an exclusive lock is required on the map for a call to insert.
		scoped_lock<customer_map_t::upgradable_mutex_type> lock_holder(cust_map->mutex());
		cust_map->insert(std::make_pair(name, cust), *txn);
	}
	{
		account_t account;
		account.account_num = account_map->size();  // next available
		account.customer_name = name;
		account.account_start_date = 39785; // ~2009
		account.balance = 0.0;
		account.last_payment_date = -1; // never

		scoped_lock<account_map_t::upgradable_mutex_type> lock_holder(account_map->mutex());
		account_map->insert(std::make_pair(name, account), *txn);
	}
	db.commit(txn);
//]
//[tran1
	// transaction 1: find a customer by name, and confirm that it
	// has an account with us.
	{
		sharable_lock<customer_map_t::upgradable_mutex_type> lock_holder(cust_map->mutex());
		customer_ref custref = cust_map->find(name);
		assert(custref->second.shoe_size == 8);
	}
	{
		sharable_lock<account_map_t::upgradable_mutex_type> lock_holder(account_map->mutex());
		account_ref accountref = account_map->find(name);
		assert(accountref != account_map->end());
	}
//]
//[tran2
	// transaction 2: add a purchase to an account by name.
	txn = db.beginTransaction();
	{
		sharable_lock<customer_map_t::upgradable_mutex_type> lock_holder(cust_map->mutex());
		account_ref accountref = account_map->find(name, *txn);

		account_map->lock(accountref, *txn);
		account_t localcopy( accountref->second );
		localcopy.add_purchase(100.0);
		account_map->update(accountref, localcopy, *txn); // update the entry with the new value.

		// This works because the iterator is associated with txn (when find() was called above.)
		// So it shows pending changes related to txn.  If it had been created by calling find(name)
		// it would show a balance of 0 (the currently committed value.)
		assert(accountref->second.balance == 100.0);
	}
	db.commit(txn);
//]
//[tran3
	// transaction 3: add a payment to an account.
	txn = db.beginTransaction();
	{
		sharable_lock<customer_map_t::upgradable_mutex_type> lock_holder(cust_map->mutex());
		account_ref accountref = account_map->find(name);
		account_map->lock(accountref, *txn);

		account_t localcopy( accountref->second );
		localcopy.add_payment(60.0);
		account_map->update(accountref, localcopy, *txn); // update the entry with the new value.

		assert(accountref->second.balance == 40.0);
	}
	db.commit(txn);
//]
//[tran4
	// transaction 4: remove the account.
	txn = db.beginTransaction();
	{
		sharable_lock<customer_map_t::upgradable_mutex_type> lock_holder(cust_map->mutex());
		account_ref accountref = account_map->find(name);
		account_map->erase(accountref, *txn);
	}
	db.commit(txn);
}
//]
