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

#include <cstdlib>

#pragma warning (disable:4503)

#include "test_database.h"
#include "perf_test.h"
#include "trans_map_ops.h"
#include "properties.h"

using stldb::Transaction;
using namespace std;

// Allocator of char in shared memory, with support for default constructor
typedef stldb::scope_aware_allocator<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

// Shared memory String
typedef boost::interprocess::basic_string<char, std::char_traits<char>,
		shm_char_allocator_t> shm_string;

// Allocator of pair<string,string> in shared memory
typedef boost::interprocess::allocator<std::pair<shm_string, shm_string>,
		managed_mapped_file::segment_manager> shm_pairstring_allocator_t;

typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
		shm_pairstring_allocator_t> MapType;


// contains random 1024 bytes.
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


int test_perf1load()
{
	timer t("test_perf1load");

	// Construct the database
	TestDatabase<managed_mapped_file,MapType> db("test_perf1");

	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	::srand( (unsigned int)time(NULL) );

	// object which generates key and value types for the map
	entry_maker_t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of threaded insert loop(" << loop_size << "), with " << thread_count << " threads, "
		 << inserts_per_txn << " inserts per txn, " << endl;

	test_loop test_ops( loop_size );
	test_ops.add( new insert_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_t>(db, inserts_per_txn, maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf1()
{
	timer t("test_perf1");

	// Construct the database
	TestDatabase<managed_mapped_file,MapType> db("test_perf1");

	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	::srand( (unsigned int)time(NULL) );

	// object which generates key and value types for the map
	entry_maker_t maker;

	// TEST 2:
	// Do a multi-threaded test of the find/update pairing (60/40)
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size;

	cout << "Starting test of threaded find/update, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn, " << updates_per_txn << " updates per txn" << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_t>(db, max_key, finds_per_txn, maker), 60 );
	test_ops2.add( new update_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_t>(db, max_key, updates_per_txn, maker), 40 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

