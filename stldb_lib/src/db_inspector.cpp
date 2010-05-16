/*
 * db_inspector.cpp
 *
 *  Created on: Feb 5, 2010
 *      Author: bobw
 */

#define BOOST_STLDB_SOURCE

#include <iostream>

#include <boost/archive/xml_oarchive.hpp>
#include <boost/interprocess/indexes/flat_map_index.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>

#include <stldb/stldb.hpp>
#include <stldb/Database.h>
#include <stldb/sync/bounded_interprocess_mutex.h>

using namespace std;

template <class ManagedRegionType>
int dump(int argc, const char *argv[])
{
	if (argc < 3 || argc > 4) {
		cout << "usage: db_inspector <db_dir> <db_name> [fixed_mapping_addr]" << endl;
		return 0;
	}

	const char *db_dir = argv[1];
	const char *db_name = argv[2];
	void *addr = 0;
	if (argc == 4)
		addr = reinterpret_cast<void*>( atol(argv[3]) );

//	stldb::tracing::set_trace_level( stldb::finest_e );
	STLDB_TRACE(stldb::fine_e, "db_dir: " << db_dir);
	STLDB_TRACE(stldb::fine_e, "db_name: " << db_name);
	STLDB_TRACE(stldb::fine_e, "mapping_addr: " << addr);

    boost::archive::xml_oarchive archive(cout);
    try {
    	stldb::Database<ManagedRegionType>::dump_metadata(archive, db_name, db_dir, addr);
    }
    catch (boost::interprocess::lock_exception e) {
    	cerr << e.what() << endl;
    	return -1;
    }
	return 0;
}

int main(int argc, const char *argv[])
{
//	return dump<boost::interprocess::managed_mapped_file>(argc, argv);

	// mememory_mapped_file database_type when constructed form java.
	return dump< boost::interprocess::basic_managed_mapped_file< char,
	    boost::interprocess::rbtree_best_fit<stldb::bounded_mutex_family, 
			boost::interprocess::offset_ptr<void> >
		, boost::interprocess::flat_map_index> >(argc, argv);
}


