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
#include <stldb/allocators/allocator_gnu.h>

using stldb::Transaction;
using stldb::scoped_allocation;

#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#	include <stldb/containers/gnu/basic_string.h>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::scope_aware_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;
	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;
#else
// String in shared memory, based on boost::interprocess::basic_string
#	include <boost/interprocess/containers/string.hpp>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::scope_aware_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;
#endif

// A transactional map, using std ref counted strings.
typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
	boost::interprocess::allocator<std::pair<const shm_string, shm_string>,
		managed_mapped_file::segment_manager> >  MapType;


int countRows( MapType *map, Transaction *txn )
{
	MapType::iterator i = map->begin(*txn);
	int count = 0;
	while (i != map->end(*txn)) {
		i++; count++;
	}
	return count;
}


int test2()
{
	TestDatabase<managed_mapped_file,MapType> db("test2");

	typedef TestDatabase<managed_mapped_file,MapType>::db_type         db_type;

	// Get the handles to our now created or loaded containers
	MapType *map = db.getMap();
	db_type *database = db.getDatabase();

	// Initialize map with some data...
	shm_char_allocator_t alloc(db.getRegion().get_segment_manager());

	shm_string key1( "Key0001", alloc );
	shm_string key1_5( "Key00015", alloc );
	shm_string key2( "Key0010", alloc );
	shm_string key3( "Key0100", alloc );
	shm_string key4( "Key0200", alloc );

	Transaction *txn0 = db.beginTransaction(0);
	{//lock scope
		scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
		scoped_allocation<db_type::RegionType> default_region( database->getRegion() );

		// initialize map with 3 values;
		pair<MapType::iterator, bool> result = map->insert( std::make_pair(key1,key1), *txn0 );
		assert(result.second);
		result = map->insert( std::make_pair(key2,key2), *txn0 );
		assert(result.second);
		result = map->insert( std::make_pair(key3,key3), *txn0 );
		assert(result.second);
	}
	db.commit(txn0);

	// Insertion isolation test.
	Transaction *txn1 = db.beginTransaction(0);
	Transaction *txn2 = db.beginTransaction(0);

	{//lock scope
		scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
		scoped_allocation<db_type::RegionType> default_region( database->getRegion() );

		// Have txn insert key1_5, and conform that txn2 doesn't see it.
		pair<MapType::iterator, bool> result = map->insert( std::make_pair(key1_5, key1_5), *txn1 );
		assert(result.second);

		assert(countRows(map,txn1) == 4);
		assert(countRows(map,txn2) == 3);

		// Have txn2 insert key4, and conform that txn2 doesn't see it.
		result = map->insert( std::make_pair(key4, key4), *txn2 );
		assert(result.second);

		assert(countRows(map,txn1) == 4);
		assert(countRows(map,txn2) == 4);

//		assert( map->rbegin(*txn1)++ == map->rbegin()++);
//		assert( map->rbegin(*txn2)++ != map->rbegin()++);

		// A test of find's transaction awareness...
		MapType::iterator i = map->find( key1_5, *txn1 );
		assert( i->second == key1_5 );
		i = map->find( key4, *txn2 );
		assert( i->second == key4 );

		i = map->find( key1_5, *txn2 );
		assert( i == map->end() );
		i = map->find( key4, *txn1 );
		assert( i == map->end() );

	}//lock scope

	// Now let's commit txn1 and confirm that txn2 can immediately
	// see those changes.
	db.commit(txn1);

	{//lock scope
		scoped_lock<MapType::upgradable_mutex_type> lock_holder(map->mutex());
		scoped_allocation<db_type::RegionType> default_region( database->getRegion() );

		assert(countRows(map,txn2) == 5);
		MapType::iterator i = map->find( key1_5, *txn2 );
		assert( i->second == key1_5 );
		i = map->find( key4, *txn2 );
		assert( i->second == key4 );

	}//lock scope

	db.commit(txn2);

	return 0;
}



