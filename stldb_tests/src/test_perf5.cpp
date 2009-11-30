
/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */

#include "test_database.h"
#include "perf_test.h"
#include "trans_map_ops.h"
#include "properties.h"
#include <stldb/timing/time_tracked.h>

using stldb::Transaction;
using namespace std;

// Allocator of char in shared memory, with support for default constructor
typedef stldb::scope_aware_allocator<
		boost::interprocess::allocator<
				char, fixed_managed_mapped_file::segment_manager> > shm_char_allocator_t;

// Shared memory String
typedef std::basic_string<char, std::char_traits<char>,
		shm_char_allocator_t> shm_string;

// A transactional map, using std ref counted strings.
typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
	boost::interprocess::allocator< std::pair<const shm_string, shm_string>,
		fixed_managed_mapped_file::segment_manager> >  MapType;

static char buffer[1024];

struct entry_maker_5t {
	shm_string make_key(int keyno) {
		char temp[64];
		sprintf(temp, "Key%08d", keyno);
		return shm_string(temp);
	}

	shm_string make_value(int keyno) {
		return shm_string(buffer, 256 + ((keyno) % 768));
	}
};

int test_perf5load()
{
	timer t5("test_perf5load");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf5");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	shm_string test("abc");

	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_5t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of threaded insert loop(" << loop_size << "), with " << thread_count << " threads, "
		 << inserts_per_txn << " inserts per txn, " << endl;

	test_loop test_ops( loop_size );
	test_ops.add( new insert_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_5t>(db, inserts_per_txn, maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf5()
{
	timer t5("test_perf5");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf5");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_5t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size;

	cout << "Starting test of threaded find/update, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn, " << updates_per_txn << " updates per txn" << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_5t>(
			db, max_key, finds_per_txn, maker), 60 );
	test_ops2.add( new update_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_5t>(
			db, max_key, updates_per_txn, maker), 40 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}


int test_perf5_readonly()
{
	timer t5("test_perf5_readonly");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf5");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_5t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int max_key = loop_size;

	cout << "Starting test of threaded find, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn. " << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_5t>(
			db, max_key, finds_per_txn, maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}
