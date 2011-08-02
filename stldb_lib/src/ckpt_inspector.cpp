/*
 * ckpt_reader.cpp
 *
 *  Created on: Jan 18, 2010
 *      Author: bobw
 */
#define BOOST_STLDB_SOURCE

#include <stldb/checkpoint.h>
#include <stldb/detail/db_file_util.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/string_serialize.h>

namespace stldb {
namespace util {

using namespace std;
using boost::interprocess::offset_t;
using boost::interprocess::managed_mapped_file;
using stldb::checkpoint_ifstream;
using stldb::TransEntry;
using stldb::checkpoint_iterator;

#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#   include <stldb/containers/gnu/basic_string.h>
#	include <stldb/allocators/allocator_gnu.h>
    // Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;
	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;
#else
	// String in shared memory, based on boost::interprocess::basic_string
#   include <boost/interprocess/containers/string.hpp>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;
#endif

struct adjacent_regions {
	template <class MapValueType> 
	bool operator()(MapValueType &l, MapValueType &r) {
		return (l.first + (typename MapValueType::first_type)l.second >= r.first);
	}
};

typedef std::pair<offset_t,size_t> checkpoint_loc_t;

int verify_checkpoint( boost::filesystem::path &tempfilepath, 
                       offset_t loc, size_t sz )
{
	stldb::checkpoint_file_info meta;
	stldb::detail::get_checkpoint_file_info( tempfilepath, meta );
	cout << "=====================================================" << endl;
	cout << meta << endl;
	cout << "=====================================================" << endl;

	// sanity check the free region of the file
	std::map<offset_t,size_t> by_offset;
	std::multimap<size_t,offset_t>::iterator i = meta.free_space.begin();
	while (i != meta.free_space.end()) {
		by_offset.insert( make_pair( i->second, i->first ) );
		i++;
	}
	
	// find any adjacent or overlapping regions within the file.
	std::map<offset_t,size_t>::iterator j = by_offset.begin();
	j = adjacent_find(j, by_offset.end(), adjacent_regions());
	while (j != by_offset.end()) {
		std::map<offset_t,size_t>::iterator k = j; k++;
		cout << "ERROR: [" << j->first << "," << j->second << "] "
			<< (k->first < j->first + offset_t(j->second) 
					? "overlaps with [" : " adjacent to [")
			<< k->first << "," << k->second << "]" << endl;
		j = adjacent_find(k, by_offset.end(), adjacent_regions());
	}

exit(0);

	// now scan the checkpoint for duplicate entries and entries
	// which have an erroneous overlap with free space
	checkpoint_ifstream ifile( tempfilepath );
	typedef std::map<shm_string, TransEntry<shm_string> > map_type;
	typedef std::pair<shm_string, TransEntry<shm_string> > value_type;
	map_type records;

	checkpoint_iterator<value_type> iter( ifile.begin<value_type>() );
	while (iter != ifile.end<value_type>() ) {
		checkpoint_loc_t loc( iter.checkpoint_location() );
		std::map<offset_t,size_t>::const_iterator freeb = by_offset.lower_bound(loc.first);
		if (freeb != by_offset.end() && freeb != by_offset.begin() ) {
			--freeb;
			if (freeb->first + (ssize_t)freeb->second > loc.first) {
				cout << "ERROR: Overlap between checkpoint entry [" << loc.first << "," << loc.second << "] and free region [" << freeb->first << "," << freeb->second << "]" << endl;
			}
		}
		std::map<offset_t,size_t>::const_iterator freea = by_offset.upper_bound(loc.first);
		if (freea != by_offset.end()) {
			if (loc.first + (ssize_t)loc.second > freea->first) {
				cout << "ERROR: Overlap between checkpoint entry [" << loc.first << "," << loc.second << "] and free region [" << freea->first << "," << freea->second << "]" << endl;
			}
		}
		value_type val( *iter );
		val.second.setCheckpointLocation( iter.checkpoint_location() );
		std::pair<map_type::iterator,bool> result = records.insert( *iter );
		if (!result.second) {
			cout << "ERROR: Redundant Entries found:" << endl;
			map_type::const_iterator existing = records.find( iter->first );

			checkpoint_loc_t loc = existing->second.checkpointLocation();
			cout << "Existing Entry from [" << loc.first << "," << loc.second << "], txn_id: " << existing->second.getLockId() << endl;
			cout << "Existing Key[" << existing->first.length() << "]" << endl;
			cout << "Existing Value[" << existing->second.length() << "]" << endl;

			loc = iter.checkpoint_location();
			cout << "New Entry from [" << loc.first << "," << loc.second << "], txn_id: " << iter->second.getLockId() << endl;
			cout << "New Key[" << iter->first.length() << "]" << endl;
			cout << "New Value[" << iter->second.length() << "]" << endl;
		}
		++iter;
	}

	if (loc == -1) {
		cout << meta << endl;
		return 0;
	}

	cout << "Check for entry at offset: " << loc << ", size: " << sz << endl;
	std::map<offset_t,size_t>::const_iterator before = by_offset.lower_bound(loc);
	std::map<offset_t,size_t>::const_iterator after = by_offset.upper_bound(loc);
	if (before->first != loc && before != by_offset.begin())
		--before;
	cout << "Free Region before: " << 
		before->first << ":" << before->second << endl;
	cout << "Free Region after: " << 
		(after != by_offset.end() ? after->first : 0 ) << ":" <<
		(after != by_offset.end() ? after->second : 0 ) << endl;
	if (before->first + (ssize_t)before->second < loc &&
		after == by_offset.end() || after->first > loc + (ssize_t)sz) {
		cout << "Looks like an occupied space" << endl;
	}
	else {
		cout << "Error: Conflicts with free space" << endl;
		return -1;
	}

	typedef std::pair<shm_string, TransEntry<shm_string> > value_type;
	checkpoint_iterator<value_type> iter2( ifile.seek<value_type>(loc) );
	cout << "Key[" << iter2->first.length() << "]: " << iter2->first << endl;
	cout << "Value[" << iter2->second.length() << "]: " << iter2->second << endl;

	return 0;
}

} // namespace util
} // namespace stldb

int main(int argc, const char *argv[])
{
	// argv[1] is the name of a checkpoint metafile
	boost::filesystem::path metafile(argv[1]);
	boost::interprocess::offset_t loc = argc>2 ? atol(argv[2]) : -1;
	size_t sz = argc>3 ? atol(argv[3]) : 1;

	return stldb::util::verify_checkpoint( metafile, loc, sz);
}
