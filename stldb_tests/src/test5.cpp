/*
 * test5.cpp
 *
 *  Created on: Jan 17, 2010
 *      Author: bobw
 */

/**
 * @author bobw
 *
 */

#include "test_database.h"
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/allocator_gnu.h>

using stldb::Transaction;
using stldb::scoped_allocation;
using stldb::Database;

#ifdef __GNUC__
	// On Unix/Linux/Solaris:
	// Using stldb::basic_string for the string data type.  (shm safe, perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;

	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;
#else
	// On Windows:
	// Using boost::interprocess::basic_string for the string data type.  (shm safe, not perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;
#endif

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
typedef boost::interprocess::cached_node_allocator<std::pair<const shm_string, shm_string>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// Finally - the transactional map of <string, string>, which uses cached allocation
typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
	trans_map_allocator, stldb::bounded_mutex_family>  MapType;

// global
properties_t properties;


void dumpMap(MapType *map) {
	MapType::iterator i = map->begin();
	while ( i != map->end() ) {
		std::cout << "[" << i->first << "," << i->second << "]" << std::endl;
		i++;
	}
}

void dumpMap(Transaction &txn, MapType *map) {
	MapType::iterator i = map->begin(txn);
	while ( i != map->end(txn) ) {
		std::cout << "[" << i->first << "," << i->second << "]" << std::endl;
		i++;
	}
}

bool reopen_and_check_mapsize(std::size_t expected_size)
{
	// Re-open the database, triggering recovery
	TestDatabase<managed_mapped_file,MapType> db("test5_database");
	assert( db.getDatabase()->check_integrity() );

	MapType *map = db.getMap();
	assert( map != NULL );
	STLDB_TRACE(stldb::fine_e, "Recovered map holds " << map->size() << " entries");
	dumpMap( map );
	return( map->size() == expected_size );
}

bool recover_empty_database()
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( map->size() == 0 ); // exists, and is empty

	assert( db.getDatabase()->check_integrity() );

	assert( db.getDatabase()->get_archivable_logs().size() == 0 );
	assert( db.getDatabase()->get_current_logs().size() == 0 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 0 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_from_one_log_only()
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( map->size() == 0 ); // exists, and is empty
	assert( db.getDatabase()->check_integrity() );

	Transaction *txn = db.beginTransaction();

	char rawkey[256];
	char rawvalue[256];
	for (int i=0; i<10; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "the value for key%05d", i);
		map->insert( std::make_pair(rawkey,rawvalue), *txn );
	}

	db.commit( txn );

	assert( db.getDatabase()->get_archivable_logs().size() == 0 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 0 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_from_multiple_logs( )
{
	// looks the same as the previous test, because it is.  After recovery, the
	// first commit always starts a new log, so when we commit here there are 2 logs.

	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( map->size() == 10 ); // exists, and is empty
	assert( db.getDatabase()->check_integrity() );

	Transaction *txn = db.beginTransaction();

	char rawkey[256];
	char rawvalue[256];
	for (int i=10; i<20; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "the value for key%05d", i);
		map->insert( std::make_pair(rawkey,rawvalue), *txn );
	}

	db.commit( txn );

	assert( db.getDatabase()->get_archivable_logs().size() == 0 );
	assert( db.getDatabase()->get_current_logs().size() == 2 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 0 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_from_checkpoint_one( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( map->size() == 20 );
	assert( db.getDatabase()->check_integrity() );

	// checkpoint the database.  Of the two log files which exist at this moment,
	// one of them becomes archivable because of the checkpoint
	db.getDatabase()->checkpoint();

	assert( db.getDatabase()->get_archivable_logs().size() == 1 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 1 );

	Transaction *txn = db.beginTransaction();

	char rawkey[256];
	char rawvalue[256];
	for (int i=20; i<30; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "the value for key%05d", i);
		map->insert( std::make_pair(rawkey,rawvalue), *txn );
	}

	db.commit( txn );

	assert( db.getDatabase()->get_archivable_logs().size() == 2 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 1 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_from_checkpoint_two( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( map->size() == 30 );
	assert( db.getDatabase()->check_integrity() );

	// checkpoint the database.  This is a second checkpoint, resulting in a
	// second meta file.
	db.getDatabase()->checkpoint();

	assert( db.getDatabase()->get_archivable_logs().size() == 2 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 1 );

	Transaction *txn = db.beginTransaction();

	// This loop is deliberately replacing existing entries
	char rawkey[256];
	char rawvalue[256];
	for (int i=20; i<25; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "updated-2 value for key%05d", i);
		MapType::iterator iter = map->find(rawkey);
		map->update(iter, rawvalue, *txn);
	}
	for (int i=25; i<30; i++) {
		sprintf(rawkey, "key%05d", i);
		MapType::iterator iter = map->find(rawkey);
		map->erase(iter, *txn);
	}

	db.commit( txn );

	assert( db.getDatabase()->get_archivable_logs().size() == 3 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 1 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

static std::pair<boost::interprocess::offset_t, std::size_t> pre_lock_loc;
static std::pair<boost::interprocess::offset_t, std::size_t> pre_update_loc;
static std::pair<boost::interprocess::offset_t, std::size_t> pre_erase_loc;
static stldb::transaction_id_t ckpt_lsn;

#define CONFIRM_UNLOCK { boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex> lock(map->mutex()); STLDB_TRACE(stldb::fine_e, "map unlock confirmed: " << __LINE__); }

bool recover_from_checkpoint_three( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();
	assert( db.getDatabase()->check_integrity() );

	// checkpoint the database.  This is a third checkpoint.  Also, based on the
	// changes made in the previous method, this should be yielding free space as
	// existing checkpointed entries have new values being written out.
	db.getDatabase()->checkpoint();

	assert( db.getDatabase()->get_archivable_logs().size() == 3 );
	assert( db.getDatabase()->get_current_logs().size() == 1 );
	assert( db.getDatabase()->get_current_checkpoints().size() == 1 );

	// This is the first time some of the free space is being reused.
	// let's make sure that happens correctly...
	Transaction *txn = db.beginTransaction();

	char rawkey[256];
	char rawvalue[256];
	for (int i=19; i<21; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "yet another value for key%05d", i);
		MapType::iterator iter = map->find(rawkey);
		map->update(iter, rawvalue, *txn);
	}

	db.commit( txn );

	txn = db.beginTransaction();

	// Test that checkpoint handles in-progress changes by writing update and
	// ease records tagged as work-in-progress, because the lack of a visible
	// lsn on the record prevents it from knowing for sure if it should change it.
	int i = 21;
	sprintf(rawkey, "key%05d", i);
	sprintf(rawvalue, "yet another value for key%05d", i);
	MapType::iterator itr = map->find(rawkey);
	pre_update_loc = itr->second.checkpointLocation();
	map->update(itr, rawvalue, *txn);

	i = 22;
	sprintf(rawkey, "key%05d", i);
	itr = map->find(rawkey);
	pre_erase_loc = itr->second.checkpointLocation();
	map->erase(itr, *txn);

	i = 23;
	sprintf(rawkey, "key%05d", i);
	itr = map->find(rawkey);
	pre_lock_loc = itr->second.checkpointLocation();
	map->lock(itr, *txn);

	// Now also test that the checkpoint doesn't write out pending inserts.
	// The only 'in progress' rows it should write out are pending updates, and
	// pending deletes.  deletes of previously inserted rows shouldn't happen.
	for (int i=31; i<40; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "uncommitted value for key%05d", i);
		map->insert( std::make_pair(rawkey, rawvalue), *txn );
	}
	// weird, but possible.  A pending, undeleted update to a pending insert.
	for (int i=38; i<40; i++) {
		sprintf(rawkey, "key%05d", i);
		MapType::iterator iter = map->find(rawkey, *txn);
		map->erase(iter, *txn);
	}

	// NOTE!  The txn transaction hasn't been committed yet...
	ckpt_lsn = db.getDatabase()->checkpoint();

	// DO NOT commit the above transaction.  Let it die with the database.
	// Then we shall see what recovery does,
	// and whether the checkpoint persisted the right records.
	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_from_checkpoint_four( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	MapType *map = db.getMap();
	assert( db.getDatabase()->check_integrity() );

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	// Confirm that the uncommitted lock did NOT go into the checkpoint
	char rawkey[256];
	sprintf(rawkey, "key%05d", 21);
	MapType::iterator i = map->find(rawkey);
	assert( pre_update_loc != i->second.checkpointLocation() );

	// Confirm that the uncommitted update did go into the checkpoint
	sprintf(rawkey, "key%05d", 22);
	i = map->find(rawkey);
	assert( pre_erase_loc != i->second.checkpointLocation() );

	// Confirm that the uncommitted delete did go into the checkpoint
	sprintf(rawkey, "key%05d", 23);
	i = map->find(rawkey);
	STLDB_TRACE(stldb::fine_e, "locked entry prior checkpoint: [" << pre_lock_loc.first << "," << pre_lock_loc.second << "]");
	STLDB_TRACE(stldb::fine_e, "locked entry current checkpoint: [" << i->second.checkpointLocation().first << "," << i->second.checkpointLocation().second << "]");
	//assert( pre_lock_loc == i->second.checkpointLocation() );

	// Confirm that the uncommitted inserts did NOT go into the checkpoint.
	// Confirm that the uncommitted delete of the uncommitted insert did NOT go into the checkpoint.
	for (int i=31; i<40; i++) {
		sprintf(rawkey, "key%05d", i);
		assert( map->find(rawkey) == map->end() );
	}

	stldb::transaction_id_t lsn2 = db.getDatabase()->checkpoint();
	assert( ckpt_lsn == lsn2 );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_clear( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");

	MapType *map = db.getMap();
	assert( db.getDatabase()->check_integrity() );

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	stldb::exclusive_transaction *txn = db.getDatabase()->begin_exclusive_transaction();

	map->clear(*txn);

	db.commit( txn );

	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool checkpoint_clear( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");
	assert( db.getDatabase()->check_integrity() );

	// checkpoint.
	db.getDatabase()->checkpoint();

	// and now recover, should still have 0 rows.
	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool recover_after_checkpoint( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");
	assert( db.getDatabase()->check_integrity() );

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	MapType *map = db.getMap();

	char rawkey[256];
	char rawvalue[256];
	for (int i=40; i<50; i++) {
		Transaction *txn = db.beginTransaction();

		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "the value for key%05d", i);
		map->insert( std::make_pair(rawkey,rawvalue), *txn );

		db.commit( txn );
	}

	// checkpoint.
	db.getDatabase()->checkpoint();

	// and now recover, should still have 0 rows.
	db.getDatabase()->set_invalid(true);
	assert( !db.getDatabase()->check_integrity() );

	return true;
}

bool checkpoint_after_checkpoint( )
{
	// Construct the database, opening it in the process
	TestDatabase<managed_mapped_file,MapType> db("test5_database");
	assert( db.getDatabase()->check_integrity() );

	// checkpoint immediately after restart (which in turn followed checkpoint)
	// this risks the chance of repeating a checkpoint fo rthe same start_lsn;
	db.getDatabase()->checkpoint();

	return true;
}


// Log Tester - tests log throughput
int main(int argc, const char* argv[])
{
	properties.parse_args(argc, argv);

	stldb::timer::enabled = properties.getProperty("timing", true);

	int trace_level = properties.getProperty("tracing", (int)stldb::finer_e);
	stldb::tracing::set_trace_level( (stldb::trace_level_t)trace_level );

	// clean-up after prior runs
	std::string db_dir, checkpoint_dir, logging_dir;
	{
		// Construct the database, opening it in the process
		TestDatabase<managed_mapped_file,MapType> db("test5_database");
		db_dir = db.getDatabase()->get_database_directory();
		checkpoint_dir = db.getDatabase()->get_checkpoint_directory();
		logging_dir = db.getDatabase()->get_logging_directory();
	}
	stldb::Database<managed_mapped_file>::remove("test5_database",
 			db_dir.c_str(), checkpoint_dir.c_str(), logging_dir.c_str() );

	assert( recover_empty_database() );
	assert( reopen_and_check_mapsize(0) );

	assert( recover_from_one_log_only() );
	assert( reopen_and_check_mapsize(10) );

	assert( recover_from_multiple_logs() );
	assert( reopen_and_check_mapsize(20) );

	assert( recover_from_checkpoint_one() );
	assert( reopen_and_check_mapsize(30) );

	assert( recover_from_checkpoint_two() );
	assert( reopen_and_check_mapsize(25) );

	assert( recover_from_checkpoint_three() );
	assert( reopen_and_check_mapsize(25) );

	assert( recover_from_checkpoint_four() );
	assert( reopen_and_check_mapsize(25) );

	assert( recover_clear() );
	assert( reopen_and_check_mapsize(0) );

	assert( checkpoint_clear() );
	assert( reopen_and_check_mapsize(0) );

	assert( recover_after_checkpoint( ) );
	assert( reopen_and_check_mapsize(10) );

	assert( checkpoint_after_checkpoint( ) );

	return 0;
}
