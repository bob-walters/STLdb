//============================================================================
// Name        : stldb-tests.cpp
// Author      : Bob Walters
// Version     :
// Copyright   : Copyright 2009 by Bob Walters
// Description : Hello World in C++, Ansi-style
//============================================================================

/**
 * This test case is focused on confirming the recoverability of transactions, via
 * both the logging mechanism, and also the checkpoint mechanism.
 *      Author: bobw
 */

#include "test_database.h"
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/allocator_gnu.h>

#include <boost/thread.hpp>
#include <boost/interprocess/indexes/flat_map_index.hpp>

using stldb::Transaction;
using stldb::scoped_allocation;

// managed mapped file type with bounded_mutex_family for mutexes.  This is passed instead of
// boost::interprocess::managed_mapped_file to configure the use of bounded mutexes.
typedef boost::interprocess::basic_managed_mapped_file< char,
	boost::interprocess::rbtree_best_fit<
		stldb::bounded_mutex_family, boost::interprocess::offset_ptr<void> >,
	boost::interprocess::flat_map_index>  RegionType;


#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#	include <stldb/containers/gnu/basic_string.h>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, RegionType::segment_manager> > > shm_char_allocator_t;

	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;
#else
// String in shared memory, based on boost::interprocess::basic_string
#	include <boost/interprocess/containers/string.hpp>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, RegionType::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;
#endif

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_node_allocator<std::pair<const shm_string, shm_string>,
	RegionType::segment_manager>  trans_map_allocator;

// A transactional map, using std ref counted strings.
typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
	trans_map_allocator>  MapType;


using boost::interprocess::sharable_lock;

static int countRows( MapType *map, Transaction *txn )
{
	MapType::iterator i = map->begin(*txn);
	int count = 0;
	while (i != map->end(*txn)) {
		std::cout << "[" << i->first.c_str() << "," << i->second.c_str() << "]" << std::endl;
		i++; count++;
	}
	return count;
}


class test3_thread {
public:
	test3_thread(int thread_no, int loopsize)
		: thread_no(thread_no), loopsize(loopsize)
	{ }

	void operator()()
	{
		for (int counter=0; counter<loopsize; counter++)
		{
			std::ostringstream db_name;
			db_name << "test3_" << thread_no;
			std::string dbname = db_name.str();

			// Make sure the test database is properly cleaned up.
			std::string root_dir( properties.getProperty<std::string>("rootdir",std::string(".")) );
			std::string db_dir(root_dir + "\\" + dbname);
			std::string checkpoint_dir(db_dir + "\\checkpoint");
			std::string log_dir(db_dir + "\\log");


			// Remove any previous database instance for this test.
			Database<RegionType>::remove(dbname.c_str(), db_dir.c_str(),
					checkpoint_dir.c_str(), log_dir.c_str());

			// Pass 1
			TestDatabase<RegionType,MapType> db( dbname.c_str(), db_dir.c_str(),
				checkpoint_dir.c_str(), log_dir.c_str() );

			typedef TestDatabase<RegionType,MapType>::db_type  db_type;

			// Get the handles to our now created or loaded containers
			MapType *map = db.getMap();
			db_type *database = db.getDatabase();

			Transaction *txn = db.beginTransaction(0);

			{//lock scope
				// Initialize map with some data...
				shm_char_allocator_t alloc(db.getRegion().get_segment_manager());

				// Insert some data into the test database.
				shm_string key1( "Key0001", alloc );
				shm_string key2( "Key0010", alloc );
				shm_string key3( "Key0100", alloc );

				scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());

				// initialize map with 3 values;
				pair<MapType::iterator, bool> result = map->insert( std::make_pair(key1,key1), *txn );
				assert(result.second);
				result = map->insert( std::make_pair(key2,key2), *txn );
				assert(result.second);
				result = map->insert( std::make_pair(key3,key3), *txn );
				assert(result.second);
			}
			// commit
			db.commit(txn);

			// Now close the database.  No automatic checkpoint is done here...
			db.close();

			/**************************
			 * Pass 2 - reopen, and confirm presence of committed data.
			 */
			// Because our last close was done properly, this should simply
			// be re-attaching to our region, and not performing any
			// recovery at all from log files.
			TestDatabase<RegionType,MapType> db_2( dbname.c_str(), db_dir.c_str(),
				checkpoint_dir.c_str(), log_dir.c_str() );

			// Get the handles to our now created or loaded containers
			map = db_2.getMap();
			database = db_2.getDatabase();

			// Re-read the data.
			txn = db_2.beginTransaction(0);

			{//lock scope
				sharable_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
				int count = countRows(map, txn);
				assert( count == 3);
			}//lock scope

			{//lock scope
				// Initialize map with some data...
				shm_char_allocator_t alloc(db_2.getRegion().get_segment_manager());

				shm_string key4( "Key1000", alloc );

				scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());

				// insert a 4th value;
				pair<MapType::iterator, bool> result = map->insert( std::make_pair(key4,key4), *txn );
				assert(result.second);

				// update some of the original values, so we have some updates logged as well.
				shm_string key1( "Key0001", alloc );
				shm_string key1_newval( "Key0001.updated", alloc );
				shm_string key2( "Key0010", alloc );
				shm_string key2_newval( "Key0010.updated", alloc );
				MapType::iterator i = map->find( key1, *txn );
				assert( i != map->end() );
				map->update( i, key1_newval, *txn );
				i = map->find( key2, *txn );
				assert( i != map->end() );
				map->update( i, key2_newval, *txn );
			}
			db_2.commit(txn);

			// now we're going to simulate disconnecting uncleanly. (Close to it)
			database->set_invalid(true);
			db_2.close();

			/**************************
			 * Pass 3 - recover from log files.
			 */
			// Because our last close was done improperly, this will
			// destroy the existing region, and perform recovery on it.
			TestDatabase<RegionType,MapType> db_3( dbname.c_str(), db_dir.c_str(),
				checkpoint_dir.c_str(), log_dir.c_str() );

			// Get the handles to our now created or loaded containers
			map = db_3.getMap();
			database = db_3.getDatabase();

			txn = db_3.beginTransaction(0);

			{//lock scope
				sharable_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
				int count = countRows(map, txn);
				assert(count == 4);
			}//lock scope

			{//lock scope
				// Initialize map with some data...
				shm_char_allocator_t alloc(db_3.getRegion().get_segment_manager());

				// OK.  Insert a few more rows, as this will start a second log file.
				shm_string key5( "Key1001", alloc );
				shm_string key6( "Key1010", alloc );

				scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());

				// initialize map with 3 values;
				pair<MapType::iterator, bool> result = map->insert( std::make_pair(key5,key5), *txn );
				assert(result.second);
				result = map->insert( std::make_pair(key6,key6), *txn );
				assert(result.second);
			}
			db_3.commit(txn);

			// Now checkpoint the database
			uint64_t txn_id = database->checkpoint();
			std::cout << "checkpointed through " << txn_id << std::endl;

			// now we're going to simulate disconnecting uncleanly. (Close to it)
			database->set_invalid(true);
			db_3.close();

			/**************************
			 * Pass 4 - recover from checkpoint (0 new log records)
			 */
			// Because our last close was done improperly, this will
			// destroy the existing region, and perform recovery on it.
			// Recovery will come entirely from the checkpoint in this
			// case.  There are no log records following the checkpoint.
			TestDatabase<RegionType,MapType> db_4( dbname.c_str(), db_dir.c_str(),
				checkpoint_dir.c_str(), log_dir.c_str() );

			// Get the handles to our now created or loaded containers
			map = db_4.getMap();
			database = db_4.getDatabase();

			txn = db_4.beginTransaction(0);

			{//lock scope
				sharable_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
				int count = countRows(map, txn);
				assert(count == 6);
			}//lock scope

			{//lock scope
				// Initialize map with some data...
				shm_char_allocator_t alloc(db_4.getRegion().get_segment_manager());

				// OK.  Insert a few more rows.
				shm_string key7( "Key1100", alloc );
				shm_string key8( "Key2000", alloc );

				scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());

				// initialize map with 3 values;
				pair<MapType::iterator, bool> result = map->insert( std::make_pair(key7,key7), *txn );
				assert(result.second);
				result = map->insert( std::make_pair(key8,key8), *txn );
				assert(result.second);
			}
			db_4.commit(txn);

			// now we're going to simulate disconnecting uncleanly. (Close to it)
			database->set_invalid(true);
			db_4.close();

			/**************************
			 * Pass 5 - recover from checkpoint (+2 new log records)
			 */
			// Because our last close was done improperly, this will
			// destroy the existing region, and perform recovery on it.
			// Recovery will come initially from the checkpoint in this
			// case.  Then the log records for key7 & key8 will be
			// applied as well.
			TestDatabase<RegionType,MapType> db_5( dbname.c_str(), db_dir.c_str(),
				checkpoint_dir.c_str(), log_dir.c_str() );

			// Get the handles to our now created or loaded containers
			map = db_5.getMap();
			database = db_5.getDatabase();

			txn = db_5.beginTransaction(0);

			{//lock scope
				sharable_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
				int count = countRows(map, txn);
				assert(count == 8);
			}//lock scope
		}
	}

private:
	int thread_no;
	int loopsize;
};


int test3() {

	int thread_count = properties.getProperty("threads", 2);
	int loopsize = properties.getProperty("loopsize", 100);

	// Start the threads which are going to run operations:
	boost::thread **workers = new boost::thread *[thread_count];
	for (int i=0; i<thread_count; i++) {
		workers[i] = new boost::thread( test3_thread(i,loopsize) );
	}

	// now await their completion
	for (int i=0; i<thread_count; i++) {
		workers[i]->join();
		delete workers[i];
	}

	return 0;
}
