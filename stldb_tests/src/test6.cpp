/*
 * test6.cpp
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
#include <stldb/allocators/variable_node_allocator.h>
#include <stldb/allocators/allocator_gnu.h>

using stldb::Transaction;
using stldb::scoped_allocation;
using stldb::Database;

static const std::size_t clump_size = 16*1024*1024;

#ifdef __GNUC__
	// On Unix/Linux/Solaris:
	// Using stldb::basic_string for the string data type.  (shm safe, perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
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
	// On Windows:
	// Using boost::interprocess::basic_string for the string data type.  (shm safe, not perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;

    typedef stldb::region_or_heap_allocator<
	    stldb::variable_node_allocator<
			char, managed_mapped_file::segment_manager, clump_size> > clustered_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, clustered_char_allocator_t> clustered_shm_string;
#endif

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
typedef boost::interprocess::cached_node_allocator<std::pair<const clustered_shm_string, shm_string>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// Finally - the transactional map of <string, string>, which uses cached allocation
typedef stldb::trans_map<clustered_shm_string, shm_string, std::less<clustered_shm_string>,
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


bool insert1()
{
	// Re-open the database, triggering recovery
	TestDatabase<managed_mapped_file,MapType> db("test6_database");
	assert( db.getDatabase()->check_integrity() );

	MapType *map = db.getMap();
	assert( map != NULL );

	Transaction *txn = db.beginTransaction();

	stldb::scoped_allocation<managed_mapped_file::segment_manager>  a(db.getRegion().get_segment_manager());

	char rawkey[256];
	char rawvalue[256];
	for (int i=10; i<20; i++) {
		sprintf(rawkey, "key%05d", i);
		sprintf(rawvalue, "the value for key%05d", i);
		map->insert( std::make_pair(rawkey,rawvalue), *txn );
	}

	db.commit( txn );

	STLDB_TRACE(stldb::fine_e, "map now holds " << map->size() << " entries");
	dumpMap( map );

	return (map->size()==10);
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
		TestDatabase<managed_mapped_file,MapType> db("test6_database");
		db_dir = db.getDatabase()->get_database_directory();
		checkpoint_dir = db.getDatabase()->get_checkpoint_directory();
		logging_dir = db.getDatabase()->get_logging_directory();
	}
	stldb::Database<managed_mapped_file>::remove("test6_database",
 			db_dir.c_str(), checkpoint_dir.c_str(), logging_dir.c_str() );

	assert(insert1());

	return 0;
}
