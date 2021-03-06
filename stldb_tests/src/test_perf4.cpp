//============================================================================
// Name        : stldb-tests.cpp
// Author      : Bob Walters
// Version     :
// Copyright   : Copyright 2009 by Bob Walters
// Description : Hello World in C++, Ansi-style
//============================================================================

/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */

#include "test_database.h"
#include "perf_test.h"
#include "map_ops.h"
#include "properties.h"

using stldb::Transaction;
using namespace std;

static boost::interprocess::interprocess_upgradable_mutex map_mutex;

// Allocator of char in shared memory, with support for default constructor
typedef stldb::scope_aware_allocator<
			boost::interprocess::allocator<
				char, fixed_managed_mapped_file::segment_manager> > shm_char_allocator_t;

// Shared memory String
typedef std::basic_string<char, std::char_traits<char>,
		shm_char_allocator_t> shm_string;

// A combination of boost-interprocess map holding std::string (which are ref counted strings.)
typedef boost::interprocess::map<shm_string, shm_string, std::less<shm_string>,
	boost::interprocess::allocator<std::pair<const shm_string, shm_string>,
		fixed_managed_mapped_file::segment_manager> >  MapType;

static char entry_buffer[1024];

struct entry_maker_t {
	shm_string make_key(int keyno) {
		char temp[64];
		sprintf(temp, "Key%08d", keyno);
		return (temp);
	}

	shm_string make_value(int keyno) {
		return shm_string(entry_buffer, 256 + ((keyno) % 768));
	}

};

int test_perf4()
{
	timer t("test_perf4");

	// test database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf4");

	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	::srand( time(NULL) );
	entry_maker_t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = 1;
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of single-threaded insert (load) for " << loop_size << endl;

	test_loop test_ops( loop_size );
	test_ops.add( new insert_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_t>(
			db, inserts_per_txn, maker, &map_mutex), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	// TEST 2:
	// Do a multi-threaded test of the find/update pairing (60/40)
	thread_count = properties.getProperty("threads", 2);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size;

	cout << "Starting test of threaded find/update, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn, " << updates_per_txn << " updates per txn" << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_t>(
			db, max_key, finds_per_txn, maker, &map_mutex), 60 );
	test_ops2.add( new update_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_t>(
			db, max_key, updates_per_txn, maker, &map_mutex), 40 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}
