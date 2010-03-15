
/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */

#include "partitioned_test_database.h"
#include "perf_test.h"
#include "partitioned_trans_map_ops.h"
#include "properties.h"
#include <stldb/timing/time_tracked.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/variable_node_allocator.h>
#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>

using stldb::Transaction;
using namespace std;

// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
static const std::size_t clump_size = 16*1024*1024;

#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#	include <stldb/containers/gnu/basic_string.h>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
	//typedef stldb::scope_aware_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;

	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;

    typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			stldb::variable_node_allocator<
				char, managed_mapped_file::segment_manager, clump_size> > > clustered_char_allocator_t;

	typedef stldb::basic_string<char, std::char_traits<char>, clustered_char_allocator_t>  clustered_shm_string;

#else
// String in shared memory, based on boost::interprocess::basic_string
#	include <boost/interprocess/containers/string.hpp>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
	//typedef stldb::scope_aware_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;

	typedef stldb::region_or_heap_allocator<
		stldb::variable_node_allocator<
			char, managed_mapped_file::segment_manager, clump_size> > clustered_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, clustered_char_allocator_t> clustered_shm_string;

#endif

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_adaptive_pool<std::pair<const clustered_shm_string, shm_string>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// A transactional map, using ref counted strings, and a cached node allocator.
typedef stldb::trans_map<clustered_shm_string, shm_string, 
	std::less<clustered_shm_string>, trans_map_allocator>  MapType;

static char buffer[1024];

struct entry_maker_11t {
	clustered_shm_string make_key(int keyno) {
		char temp[64];
		sprintf(temp, "Key%08d", keyno);
		return clustered_shm_string(temp, 12);
	}

	shm_string make_value(int keyno) {
		return shm_string(buffer, 256 + ((keyno) % 768));
	}
};

static PartitionedTestDatabase<managed_mapped_file,MapType>** databases(int count) 
{
	char name[256];
	PartitionedTestDatabase<managed_mapped_file,MapType>** dbs = new
		PartitionedTestDatabase<managed_mapped_file,MapType>*[count];
	for (int i=0; i<count; i++) {
		sprintf(name, "test_perf11-%d", i);
		dbs[i] = new PartitionedTestDatabase<managed_mapped_file,MapType>(name);
	}
	return dbs;
}

static void close( PartitionedTestDatabase<managed_mapped_file,MapType>** dbs, int count) 
{
	for (int i=0; i<count; i++) {
		delete dbs[i];
	}
}

int test_perf11load()
{
	timer t11("test_perf11load");

	// Construct the database
	int database_count = properties.getProperty("databases", 1);
	PartitionedTestDatabase<managed_mapped_file,MapType>** dbs = databases(database_count);

	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_11t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of threaded insert loop(" << loop_size << "), with " << thread_count << " threads, "
		 << inserts_per_txn << " inserts per txn, " << endl;

	test_loop test_ops( loop_size );
	for (int i=0; i<database_count; i++) {
		test_ops.add( new insert_trans_operation<PartitionedTestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_11t>(*(dbs[i]), inserts_per_txn, maker), 100 );
	}

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	close(dbs, database_count);
	return 0;
}

int test_perf11()
{
	timer t11("test_perf11");

	// Construct the databases
	int database_count = properties.getProperty("databases", 1);
	PartitionedTestDatabase<managed_mapped_file,MapType>** dbs = databases(database_count);

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_11t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size / database_count;

	cout << "Starting test of threaded find/update, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn, " << updates_per_txn << " updates per txn" << endl;

	test_loop test_ops2( loop_size );
	for (int i=0; i<database_count; i++) {
		test_ops2.add( new find_trans_operation<PartitionedTestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_11t>(
				*(dbs[i]), max_key, finds_per_txn, maker), 60 );
		test_ops2.add( new update_trans_operation<PartitionedTestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_11t>(
				*(dbs[i]), max_key, updates_per_txn, maker), 40 );
	}

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	close(dbs, database_count);
	return 0;
}


int test_perf11_readonly()
{
	timer t11("test_perf11_readonly");

	// Construct the database
	int database_count = properties.getProperty("databases", 1);
	PartitionedTestDatabase<managed_mapped_file,MapType>** dbs = databases(database_count);

	// ensure that default constructors on stldb::scope_aware_allocator know which region to use by default
	::srand( time(NULL) );

	// object which generates key and value types for the map
	entry_maker_11t maker;

	// TEST 2:
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int max_key = loop_size / database_count;

	cout << "Starting test of threaded find, with " << thread_count << " threads, "
		 << finds_per_txn << " finds per txn. " << endl;

	test_loop test_ops2( loop_size );
	for (int i=0; i<database_count; i++) {
		test_ops2.add( new find_trans_operation<PartitionedTestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_11t>(
				*(dbs[i]), max_key, finds_per_txn, maker), 100 );
	}

	// Run the scenario.
	performance_test( thread_count, test_ops2 );
	stldb::time_tracked::print(std::cout, true);

	close(dbs, database_count);
	return 0;
}
