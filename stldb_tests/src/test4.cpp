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
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/allocator_gnu.h>

using stldb::Transaction;
using stldb::scoped_allocation;


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


void load_checkpoint(MapType *map, std::istream &in)
{
	boost_iarchive_t archive(in);
	MapType::value_type entry;
	// loading a checkpoint is done as an exclusive operation.  There's no reason for it not to be.
	// Strictly speaking the lock is not required.
	while (in) {
		try {
			archive & entry;
			map->baseclass::insert(entry);
		}
		// Boost::Serialization archives signal eof with an exception.
	    catch (boost::archive::archive_exception& error) {
	        // Make sure that this due to EOF.  Annoyingly, when
	    	// this type of exception is thrown, the eof() bit on 'in'
	    	// won't be set.  We need to try reading one more byte to
	    	// make sure it is set.
	        char tmp;
	        in >> tmp;
	        if (!in.eof())
	          throw error;
	      }
	 }
}

int test4()
{
	TestDatabase<managed_mapped_file,MapType> db("test2");

	typedef TestDatabase<managed_mapped_file,MapType>::db_type  db_type;

	// Get the handles to our now created or loaded containers
	MapType *map = db.getMap();
	db_type *database = db.getDatabase();

	// Initialize map with some data...
	shm_char_allocator_t alloc(database->getRegion().get_segment_manager());

	scoped_allocation<db_type::RegionType> default_region( database->getRegion() );

	std::cout << "Starting size: " << map->size() << std::endl;

	// Attempt to load the map from a checkpoint file.
	std::string filename = properties.getProperty("filename",
			std::string("table1.0000000000000003-0000000000000003.ckpt"));
	std::ifstream in( filename.c_str() );
	load_checkpoint(map, in);

	std::cout << "Ending size: " << map->size() << std::endl;

	return 0;
}

// run-time configuration in the form of name/value pairs.
properties_t properties;

int main(int argc, const char* argv[])
{
	properties.parse_args(argc, argv);

    stldb::timer_configuration config;
    config.enabled_percent = properties.getProperty("timing_percent", 0.0);
    config.report_interval_seconds = properties.getProperty("report_freq", 60);
    config.reset_after_print = properties.getProperty("report_reset", true);
    stldb::timer::configure( config );

	stldb::tracing::set_trace_level(stldb::finest_e);
	test4();
}
