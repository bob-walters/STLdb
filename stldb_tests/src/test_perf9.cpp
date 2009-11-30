
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

#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>

#include <stldb/timing/time_tracked.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/containers/concurrent_trans_map.h>

using stldb::Transaction;
using namespace std;

// Allocator of char in shared memory, with support for default constructor
typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, fixed_managed_mapped_file::segment_manager> > shm_char_allocator_t;

// Shared memory String
typedef std::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, shm_string>,
	fixed_managed_mapped_file::segment_manager>  trans_map_allocator;

// Finally - the transactional map of <string, string>, which uses cached allocation
typedef stldb::concurrent::trans_map<shm_string, shm_string, std::less<shm_string>,
	trans_map_allocator, stldb::bounded_mutex_family>  MapType;

static char buffer[1024];

struct entry_maker_9t {
	shm_string make_key(int keyno) {
		char temp[64];
		sprintf(temp, "Key%08d", keyno);
		return shm_string(temp);
	}

	shm_string make_value(int keyno) {
		return shm_string(buffer, 256 + ((keyno) % 768));
	}
};

int test_perf9load()
{
	timer t9("test_perf9load");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf9");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	shm_string test("abc");

	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_9t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of threaded insert loop(" << loop_size << "), with " << thread_count << " threads, "
		 << inserts_per_txn << " inserts per txn, " << endl;

	test_loop test_ops( loop_size );
	test_ops.add( new insert_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_9t>(db, inserts_per_txn, maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf9()
{
	timer t9("test_perf9");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf9");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_9t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size;

	cout << "Starting test of threaded find/update, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn, " << updates_per_txn << " updates per txn" << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_9t>(
			db, max_key, finds_per_txn, maker), 60 );
	test_ops2.add( new update_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_9t>(
			db, max_key, updates_per_txn, maker), 40 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}


int test_perf9_readonly()
{
	timer t9("test_perf9_readonly");

	// Construct the database
	TestDatabase<fixed_managed_mapped_file,MapType> db("test_perf9");

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	stldb::scoped_allocation<fixed_managed_mapped_file::segment_manager> a(db.getRegion().get_segment_manager());
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_9t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int max_key = loop_size;

	cout << "Starting test of threaded find, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn. " << endl;

	test_loop test_ops2( loop_size );
	test_ops2.add( new find_trans_operation<TestDatabase<fixed_managed_mapped_file,MapType>,MapType,entry_maker_9t>(
			db, max_key, finds_per_txn, maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}
