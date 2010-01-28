// stldb_test.cpp : Defines the entry point for the console application.
//

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_oarchive boost_oarchive_t;
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_oarchive boost_oarchive_t;
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/allocators/cached_node_allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/indexes/flat_map_index.hpp>

#include <stldb/sync/bounded_interprocess_mutex.h>

#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/trans_map.h>
#include <stldb/containers/detail/map_ops.h>
#include <stldb/containers/string_serialize.h>
#include <stldb/commit_buffer.h>

#include "properties.h"

using boost::interprocess::managed_mapped_file;
using boost::interprocess::file_mapping;
using boost::interprocess::create_only;  // flag
using std::ofstream;

// managed mapped file type with bounded_mutex_family for mutexes.  This is passed instead of
// boost::interprocess::managed_mapped_file to configure the use of bounded mutexes.
typedef boost::interprocess::basic_managed_mapped_file< char,
	boost::interprocess::rbtree_best_fit<
		stldb::bounded_mutex_family, boost::interprocess::offset_ptr<void> >,
	boost::interprocess::flat_map_index>  RegionType;

#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#	include <stldb/allocators/allocator_gnu.h>
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
	trans_map_allocator>  StdTransMap;

// A commit buffer
typedef stldb::commit_buffer_t< boost::interprocess::allocator<void,
		RegionType::segment_manager> > commit_buffer_type;


using namespace stldb::detail;
using std::cout;
using std::endl;


int insert_op(StdTransMap &map, boost_oarchive_t &bstream)
{
	map.baseclass::insert( StdTransMap::value_type(
		shm_string("TestKey0001"), shm_string("TestValue0001")) );
	StdTransMap::baseclass::iterator i = map.baseclass::find("TestKey0001");
	assert(i != map.baseclass::end());
	map_insert_operation<StdTransMap> insert_op(map, i);

	return insert_op.add_to_log( bstream );
}

bool insert_op(StdTransMap &map, boost_iarchive_t &bstream)
{
	std::pair<std::string,int> header = stldb::TransactionalOperation::deserialize_header(bstream);
	assert(header.first == "Map");
	assert(header.second = stldb::Insert_op);
	map_insert_operation<StdTransMap> recovered_insert_op(map);
	recovered_insert_op.recover( bstream, 1 );
	assert( map.baseclass::find("TestKey0001") != map.baseclass::end());

	return true;
}

int update_op(StdTransMap &map, boost_oarchive_t &bstream)
{
	StdTransMap::baseclass::iterator i = map.baseclass::find("TestKey0001");
	if(i == map.baseclass::end()) {
		map.baseclass::insert( StdTransMap::value_type(
			shm_string("TestKey0001"), shm_string("UpdatedTestValue")) );
		i = map.baseclass::find("TestKey0001");
	}
	StdTransMap::value_type newValue(shm_string("TestKey0001")
						            ,shm_string("UpdatedTestValue"));
	map_update_operation<StdTransMap> update_op(map, 1, stldb::No_op, i, newValue );

	return update_op.add_to_log( bstream );
}

bool update_op(StdTransMap &map, boost_iarchive_t &bstream)
{
	std::pair<std::string,int> header = stldb::TransactionalOperation::deserialize_header(bstream);
	assert(header.first == "Map");
	assert(header.second = stldb::Update_op);
	map_update_operation<StdTransMap> recovered_update_op(map);
	recovered_update_op.recover( bstream, 1 );
	assert( map.baseclass::find("TestKey0001")->second == shm_string("UpdatedTestValue"));

	return true;
}

int delete_op(StdTransMap &map, boost_oarchive_t &bstream)
{
	StdTransMap::baseclass::iterator i = map.baseclass::find("TestKey0001");
	if(i == map.baseclass::end()) {
		map.baseclass::insert( StdTransMap::value_type(
			shm_string("TestKey0001"), shm_string("UpdatedTestValue")) );
		i = map.baseclass::find("TestKey0001");
	}
	map_delete_operation<StdTransMap> delete_op(map, 1, stldb::No_op, i );

	return delete_op.add_to_log( bstream );
}

bool delete_op(StdTransMap &map, boost_iarchive_t &bstream)
{
	std::pair<std::string,int> header = stldb::TransactionalOperation::deserialize_header(bstream);
	assert(header.first == "Map");
	assert(header.second = stldb::Delete_op);
	map_delete_operation<StdTransMap> recovered_delete_op(map);
	recovered_delete_op.recover( bstream, 1 );
	assert( map.baseclass::find("TestKey0001") == map.baseclass::end());

	return true;
}

int lock_op(StdTransMap &map, boost_oarchive_t &bstream)
{
	StdTransMap::baseclass::iterator i = map.baseclass::find("TestKey0001");
	if(i == map.baseclass::end()) {
		map.baseclass::insert( StdTransMap::value_type(
			shm_string("TestKey0001"), shm_string("TestValue")) );
		i = map.baseclass::find("TestKey0001");
	}
	map_lock_operation<StdTransMap> lock_op(map, 1, stldb::No_op, i );

	return lock_op.add_to_log( bstream );
}

bool test_insert_op(StdTransMap &map, StdTransMap &recovered_map, commit_buffer_type *buffer)
{
	buffer->clear();
	boost::interprocess::basic_ovectorstream<commit_buffer_type> vstream(buffer->get_allocator(), std::ios_base::binary);
	vstream.swap_vector(*buffer);
	boost_oarchive_t bstream(vstream);

	assert( insert_op(map, bstream) == 1);

	vstream.swap_vector(*buffer);
	assert(buffer->size() > 0);

	boost::interprocess::basic_ivectorstream<commit_buffer_type> vstream_in(buffer->get_allocator(), std::ios_base::binary);
	vstream_in.swap_vector(*buffer);
	boost_iarchive_t bstream_in(vstream_in);

	// Now let's try to read back in, and recover the operations
	insert_op( recovered_map, bstream_in );

	return true;
}

bool test_update_op(StdTransMap &map, StdTransMap &recovered_map, commit_buffer_type *buffer)
{
	buffer->clear();
	boost::interprocess::basic_ovectorstream<commit_buffer_type> vstream(buffer->get_allocator(), std::ios_base::binary);
	vstream.swap_vector(*buffer);
	boost_oarchive_t bstream(vstream);

	assert( update_op(map, bstream) == 1);

	vstream.swap_vector(*buffer);
	assert(buffer->size() > 0);

	boost::interprocess::basic_ivectorstream<commit_buffer_type> vstream_in(buffer->get_allocator(), std::ios_base::binary);
	vstream_in.swap_vector(*buffer);
	boost_iarchive_t bstream_in(vstream_in);

	update_op( recovered_map, bstream_in );

	return true;
}

bool test_delete_op(StdTransMap &map, StdTransMap &recovered_map, commit_buffer_type *buffer)
{
	buffer->clear();
	boost::interprocess::basic_ovectorstream<commit_buffer_type> vstream(buffer->get_allocator(), std::ios_base::binary);
	vstream.swap_vector(*buffer);
	boost_oarchive_t bstream(vstream);

	assert( delete_op(map, bstream) == 1);

	vstream.swap_vector(*buffer);
	assert(buffer->size() > 0);

	boost::interprocess::basic_ivectorstream<commit_buffer_type> vstream_in(buffer->get_allocator(), std::ios_base::binary);
	vstream_in.swap_vector(*buffer);
	boost_iarchive_t bstream_in(vstream_in);

	delete_op( recovered_map, bstream_in );

	return true;
}

bool test_lock_op(StdTransMap &map, StdTransMap &recovered_map, commit_buffer_type *buffer)
{
	buffer->clear();
	boost::interprocess::basic_ovectorstream<commit_buffer_type> vstream(buffer->get_allocator(), std::ios_base::binary);
	vstream.swap_vector(*buffer);
	boost_oarchive_t bstream(vstream);

	assert( lock_op(map, bstream) == 0 );

	return true;
}


bool test_random_assortment(StdTransMap &map, StdTransMap &recovered_map, int N, commit_buffer_type *buffer)
{
	int* opcodes = new int[N];

	// Let's serialize the ops to a file.
	buffer->clear();
	boost::interprocess::basic_ovectorstream<commit_buffer_type> vstream(buffer->get_allocator(), std::ios_base::binary);
	vstream.swap_vector(*buffer);
	boost_oarchive_t bstream(vstream);

	for (int i=0; i<N; i++) {
		int op = static_cast<int>(::rand() * (((double)4) / (double)RAND_MAX));
		opcodes[i] = op;
		switch (op) {
			case 0:
				insert_op(map, bstream);
				break;
			case 1:
				update_op(map, bstream);
				break;
			case 2:
				delete_op(map, bstream);
				break;
			case 3:
				lock_op(map, bstream);
				break;
		}
	}

	// Now let's try to read back in, and recover the operations
	vstream.swap_vector(*buffer);
	assert(buffer->size() > 0);
	std::size_t size = buffer->size();

	boost::interprocess::basic_ivectorstream<commit_buffer_type> vstream_in(buffer->get_allocator(), std::ios_base::binary);
	vstream_in.swap_vector(*buffer);
	boost_iarchive_t bstream_in(vstream_in);

	for (int i=0; i<N; i++) {
		switch ( opcodes[i] ) {
			case 0:
				insert_op(recovered_map, bstream_in);
				break;
			case 1:
				update_op(recovered_map, bstream_in);
				break;
			case 2:
				delete_op(recovered_map, bstream_in);
				break;
			case 3:
				// nothing in log buffer
				break;
		}
	}

	for (int i=0; i<N; i++) {
		cout << opcodes[i] << " ";
	}
	cout << size << endl;

	delete [] opcodes;
	return true;
}

bool test_value_type(StdTransMap &map)
{
	file_mapping::remove("ValueType.log");

	stldb::TransactionalOperations op;
	int container_id = 1;

	StdTransMap::value_type v( shm_string("TestKey0001"), shm_string("TestValue") );
	StdTransMap::value_type v2( shm_string("TestKey0001"), shm_string("UpdatedValue") );
	StdTransMap::value_type v3, v4;

	map.baseclass::insert( v );
	StdTransMap::baseclass::iterator i = map.baseclass::find("TestKey0001");
	assert( i != map.baseclass::end() );

	// Write out some entries
	ofstream filestream( "ValueType.log", ios::binary | ios_base::out );
	boost_oarchive_t bstream( filestream );
	op = stldb::Insert_op;
	bstream & container_id & op & (*i);  // insert behavior (ref to entry)
	op = stldb::Update_op;
	bstream & container_id & op & v2;   // update behavior (value)
	filestream.close();

	// Now let's try to read back in, and recover the operations
	ifstream filestream_in( "ValueType.log", ios::binary | ios_base::in );
	boost_iarchive_t bstream_in( filestream_in );
	bstream_in & container_id & op;
	assert(container_id == 1);
	assert(op = stldb::Insert_op);
	bstream_in & v3;  // insert restore - not by ref
	assert( v3 == v );
	bstream_in & container_id & op;
	assert(container_id == 1);
	assert(op = stldb::Update_op);
	bstream_in & v4; // update restore - not by ref 
	assert( v4 == v2 );

	map.baseclass::clear();
	return true;
}


int test_util2()
{
	file_mapping::remove("MyMmap");
	RegionType region(create_only, "MyMmap", 65536);

	// establish region as the memory for new shm_strings
	stldb::scoped_allocation<RegionType::segment_manager>
			alloc_scope( region.get_segment_manager() );

	// Commit buffer as the target of all serialization and de-serialization
	// just as with the full database.  The buffer is in the shared region.
	commit_buffer_type *buffer = region.construct<commit_buffer_type>
		(boost::interprocess::anonymous_instance)( region.get_segment_manager() );

	// Create two maps.  One to set up the ops, one to test recovery on.
	StdTransMap map(std::less<shm_string>(), region.get_segment_manager(), "Map" );
	StdTransMap recovered_map(std::less<shm_string>(), region.get_segment_manager(), "MapRecovered");

	// Now test the different transactional op types
	// Fails: assert( test_value_type(map) );
	assert( test_insert_op(map, recovered_map, buffer) );
	assert( test_update_op(map, recovered_map, buffer) );
	assert( test_delete_op(map, recovered_map, buffer) );
	assert( test_lock_op(map, recovered_map, buffer) );

	int loopsize = properties.getProperty("loopsize", 100);
	for (int i=0; i<loopsize; i++) {
		assert( test_random_assortment(map, recovered_map, 30, buffer) );
	}

#if 0
	char buffer[2056];
	strcpy(buffer, "TestValue001ArealTestOfTheValuesLength");
	//strcpy(buffer, "TestValue0012345");

	// This is the data type map actually tries to serialize
	//StdTransMap::value_type sample(shm_string("TestKey0001"), shm_string(buffer, 16));
	StdTransMap::value_type sample(shm_string("TestKey0001", 12), shm_string(buffer, strlen(buffer)));

	std::cout << "key: " << sample.first.c_str() << " [" << sample.first.length() << "]" << std::endl;
	std::cout << "value: " << sample.second.c_str() << " [" << sample.second.length() << "]" << std::endl;

	// Let's try to serialize it
	ofstream filestream( "L00000.log", ios::binary | ios_base::out );
	boost_oarchive_t bstream( filestream );
	bstream & sample; 
	filestream.close();

	StdTransMap::value_type* entry = &sample;
	ofstream filestream2( "L00001.log", ios::binary | ios_base::out );
	boost_oarchive_t bstream2( filestream2 );
	bstream2 & entry->first & entry->second;
	filestream2.close();

	ofstream filestream3( "L00002.log",ios::binary | ios_base::out );
	boost_oarchive_t bstream3( filestream3 );
	std::pair<std::string, std::string> apair( "TestKey0001", buffer );
	bstream3 & apair; 
	filestream3.close();

	ofstream filestream4( "L00003.log",ios::binary | ios_base::out );
	boost_oarchive_t bstream4( filestream4 );
	std::pair<shm_string, shm_string> bpair(
		shm_string("TestKey0001", 12),
		shm_string(buffer, strlen(buffer)));
	bstream4 & bpair; 
	filestream4.close();

	for (int i=0; i<2056; i++) {
		buffer[i] = (i==0 ? 1 : i);
	}

	ofstream filestream5( "L00004.log",ios::binary | ios_base::out );
	boost_oarchive_t bstream5( filestream5 );
//	std::pair< StdTransMap::key_type, StdTransMap::mapped_type > sample2
	StdTransMap::value_type sample2
		(shm_string("TestKey0001",12), shm_string(buffer,2056) );
	bstream5 & sample2; 
	filestream5.close();

	std::cout << "key2: " << sample2.first.c_str() << " [" << sample2.first.length() << "]" << std::endl;
	std::cout << "value2:... [" << sample2.second.length() << "]" << std::endl;

	ifstream filestream6( "L00004.log", ios::binary | ios_base::in );
	boost_iarchive_t bstream6( filestream6 );
	StdTransMap::value_type sample3;
//	std::pair< StdTransMap::key_type, StdTransMap::mapped_type > sample3;
	bstream6 & sample3;
	filestream6.close();

	std::cout << "key3: " << sample3.first.c_str() << " [" << sample3.first.length() << "]" << std::endl;
	std::cout << "value3:... [" << sample3.second.length() << "]" << std::endl;
#endif

	return 0;
}

