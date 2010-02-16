/*
 * PlainMapExample.h
 *
 *  Created on: Dec 30, 2009
 *      Author: bobw
 */
//[sharedtypes
#include <string>
#include <map>

#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>

using boost::interprocess::managed_shared_memory;

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
};
//]

//[sharedmaptypes
// "table" types in my database.
typedef boost::interprocess::map<shm_string, customer_t, std::less<shm_string>,
	boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, customer_t>,
		managed_shared_memory::segment_manager> >  customer_map_t;

typedef boost::interprocess::map<shm_string, account_t, std::less<shm_string>,
	boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, account_t>,
		managed_shared_memory::segment_manager> >  account_map_t;

typedef customer_map_t::iterator customer_ref;
typedef account_map_t::iterator account_ref;
//]

//[sharedmain1
// operations...
int main(int argc, const char *argv[])
{
	// Create the shared region
	managed_shared_memory segment(boost::interprocess::create_only,
		"ShoeDatabaseSegment", 65536);

	//Create the cust_map in shared memory
	customer_map_t *cust_map = segment.construct<customer_map_t>
		("Customers")  //name of the object
	    (std::less<shm_string>(), segment.get_segment_manager() );  //constructor arguments

	// Create the account_map in shared memory
	account_map_t *account_map = segment.construct<account_map_t>
	    ("Accounts")     //name of the object
	    (std::less<shm_string>(), segment.get_segment_manager() );  //constructor arguments
//]
//[sharedmain2
	// This object ensures that stldb::scope_aware_allocators constructed hereafter will use
	// 'segment' for their shared region.  Thus the shm_string members end up properly created in the
	// shared memory.
	stldb::scoped_allocation<boost::interprocess::managed_shared_memory::segment_manager> scope(segment.get_segment_manager());
	shm_string name = "John Smith";
//]
//[sharedmain3
	// create a customer and account within the database
	customer_t cust;
	cust.name = name;
	cust.shoe_size = 8;
	cust_map->insert(std::make_pair(name, cust));

	assert( account_map->find(name) == account_map->end() );
	account_t account;
	account.account_num = account_map->size();  // next available
	account.customer_name = name;
	account.account_start_date = 39785; // ~2009
	account.balance = 0.0;
	account.last_payment_date = -1; // never
	account_map->insert(std::make_pair(name, account));

	// transaction 1: find a customer by name, and confirm that it
	// has an account with us.
	customer_ref custref = cust_map->find(name);
	assert(custref->second.shoe_size == 8);
	account_ref accountref = account_map->find(name);
	assert(accountref != account_map->end());

	// transaction 2: add a purchase to an account by name.
	accountref = account_map->find(name);
	accountref->second.add_purchase(100.0);
	assert(accountref->second.balance == 100.0);

	// transaction 3: add a payment to an account.
	accountref->second.add_payment(60.0);
	assert(accountref->second.balance == 40.0);

	// transaction 4: remove the account.
	account_map->erase(accountref);
}
//]
