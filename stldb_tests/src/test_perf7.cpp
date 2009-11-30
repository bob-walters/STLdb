
/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */

#include <stldb/containers/concurrent_trans_map.h>
#include "test_database.h"
#include "perf_test.h"
#include "trans_map_ops.h"
#include "properties.h"
#include <stldb/timing/time_tracked.h>

using stldb::Transaction;
using namespace std;


extern properties_t properties;

// An example custom value_type.  Note that serializability is required.
struct value7_t {
	double val1;
	int val2;
	long val3;

	value7_t() : val1(10.0), val2(4), val3(50) { }
private:
	friend class boost::serialization::access;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
	    ar & val1;
	    ar & val2;
	    ar & val3;
	}
};

// implementation of this doesn't matter.  GNU std::pair has an operator< which uses the
// result of comparing second as a tie-breaker when pair.first are equal.
bool operator<(const value7_t &larg, const value7_t &rarg) {
	return false;
}

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_adaptive_pool<std::pair<const int, value7_t>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// Finally - the transactional map of <string, string>, which uses cached allocation
typedef stldb::concurrent::trans_map<int, value7_t, std::less<int>,
	trans_map_allocator, stldb::bounded_mutex_family, 1031>  MapType;

struct entry_maker_7t {
	int make_key(int keyno) {
		return keyno;
	}

	value7_t make_value(int keyno) {
		value7_t returnval;
		return returnval;
	}
};

int test_perf7load()
{
	timer t("test_perf7load");

	// Construct the database
	TestDatabase<managed_mapped_file,MapType> db("test_perf7");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	srand( static_cast<unsigned int>(time(NULL)) );

	// object which generates key and value types for the map
	entry_maker_7t maker;

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 100000);
	int inserts_per_txn = properties.getProperty("inserts_per_txn", 10);

	cout << "Starting test of threaded insert loop(" << loop_size << "), with " << thread_count << " threads, "
		 << inserts_per_txn << " inserts per txn, " << endl;

	test_loop test_ops( loop_size );
	test_ops.add( new insert_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_7t>(
			db, inserts_per_txn,maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf7()
{
	timer t("test_perf7");

	// Construct the database
	TestDatabase<managed_mapped_file,MapType> db("test_perf7");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	srand( static_cast<unsigned int>(time(NULL)) );

	// object which generates key and value types for the map
	entry_maker_7t maker;

	// TEST 2:
	// Do a multi-threaded test of the find/update pairing (60/40)
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int updates_per_txn = properties.getProperty("updates_per_txn", finds_per_txn/2);
	int max_key = loop_size;

	test_loop test_ops( loop_size );
	test_ops.add( new find_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_7t>(
			db, max_key,finds_per_txn,maker), 60 );
	test_ops.add( new update_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_7t>(
			db, max_key,updates_per_txn,maker), 40 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf7_readonly()
{
	timer t("test_perf7_readonly");

	// Construct the database
	TestDatabase<managed_mapped_file,MapType> db("test_perf7");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());
	srand( static_cast<unsigned int>(time(NULL)) );

	// object which generates key and value types for the map
	entry_maker_7t maker;

	// TEST 2:
	// Do a multi-threaded test of the find/update pairing (60/40)
	int thread_count = properties.getProperty("threads", 2);
	int loop_size = properties.getProperty("loopsize", 100000);
	int finds_per_txn = properties.getProperty("finds_per_txn", 10);
	int max_key = loop_size;

	test_loop test_ops( loop_size );
	test_ops.add( new find_trans_operation<TestDatabase<managed_mapped_file,MapType>,MapType,entry_maker_7t>(
			db, max_key,finds_per_txn,maker), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}
