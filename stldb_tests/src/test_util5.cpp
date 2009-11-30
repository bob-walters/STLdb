#include <iostream>

#include <stldb/containers/concurrent_trans_map.h>
#include "test_database.h"
#include "perf_test.h"
#include "trans_map_ops.h"
#include "properties.h"

#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>
#include <stldb/timing/time_tracked.h>
#include <stldb/allocators/region_or_heap_allocator.h>

extern properties_t properties;

// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
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

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_adaptive_pool<std::pair<const shm_string, shm_string>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// Finally - the transactional map of <string, string>, which uses cached allocation
// 5419 mutexes used for row-level locking.
typedef stldb::concurrent::trans_map<shm_string, shm_string, std::less<shm_string>,
	trans_map_allocator, stldb::bounded_mutex_family, 5419>  MapType;

using std::cout;
using std::endl;

int test_util5() {
	cout << "sizeof(shm_char_allocator_t) " << sizeof(shm_char_allocator_t);
	cout << "sizeof(shm_string) " << sizeof(shm_string);
	cout << "sizeof(MapType::value_type) " << sizeof(MapType::value_type);

	return 0;
};
