/*
 * test7.cpp
 *
 *  Created on: Jan 17, 2010
 *      Author: bobw
 */

#include <vector>
#include <boost/interprocess/managed_heap_memory.hpp>
#include <boost/interprocess/managed_external_buffer.hpp>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/variable_node_allocator.h>
#include <stldb/allocators/allocator_gnu.h>
#include <stldb/containers/gnu/cast_workaround.h>


using boost::interprocess::offset_ptr;

static const std::size_t clump_size = 16*1024*1024;

typedef boost::interprocess::basic_managed_heap_memory<
	char,
	boost::interprocess::rbtree_best_fit<boost::interprocess::mutex_family, offset_ptr<void> >,
	boost::interprocess::iset_index
>  managed_heap_memory;

typedef boost::interprocess::basic_managed_external_buffer<
	char,
	boost::interprocess::rbtree_best_fit<boost::interprocess::mutex_family, offset_ptr<void> >,
	boost::interprocess::iset_index
>  managed_external_buffer;

typedef managed_heap_memory::segment_manager segment_manager;

typedef boost::interprocess::allocator<
	char, segment_manager>  std_allocator;

// On Unix/Linux/Solaris:
// Using stldb::basic_string for the string data type.  (shm safe, perf optimized)
typedef stldb::region_or_heap_allocator<
	stldb::gnu_adapter<
		stldb::variable_node_allocator<
			char, segment_manager, clump_size> > > clustered_gnu_char_allocator_t;

// On Windows:
// Using boost::interprocess::basic_string for the string data type.  (shm safe, not perf optimized)
typedef stldb::region_or_heap_allocator<
    stldb::variable_node_allocator<
		char, segment_manager, clump_size> > clustered_char_allocator_t;


int main(int argc, const char* argv[])
{
	// create heap-based region
	managed_heap_memory region(256*1024*1024);

	std_allocator alloc1(region.get_segment_manager());
	offset_ptr<char> some = alloc1.allocate(33);
	offset_ptr<char> some_more = alloc1.allocate(330);
	alloc1.deallocate(some, 33);
	alloc1.deallocate(some_more, 330);

	// try an external buffer region
	std::vector<char> heap2(128*1024*1024, char(0));
	managed_external_buffer region2(boost::interprocess::create_only, &heap2[0], 128*1024*1024);
	std_allocator alloc2(region2.get_segment_manager());
	offset_ptr<char> some2 = alloc2.allocate(33);
	offset_ptr<char> some_more2 = alloc2.allocate(330);
	alloc2.deallocate(some2, 33);
	alloc2.deallocate(some_more2, 330);

	// now try an external buffer region within the heap-based region.
	char *big_buffer = static_cast<char*>(region.allocate(32*1024*1024));
	managed_external_buffer region3(boost::interprocess::create_only, big_buffer, 32*1024*1024);
	std_allocator alloc3(region3.get_segment_manager());
	offset_ptr<char> some3 = alloc3.allocate(33);
	offset_ptr<char> some_more3 = alloc3.allocate(330);
	alloc3.deallocate(some3, 33);
	alloc3.deallocate(some_more3, 330);

	char *big_buffer2 = static_cast<char*>(region.allocate(32*1024*1024));
//	segment_manager *sm = new(big_buffer2) segment_manager(32*1024*1024);
	stldb::segment_within_set<segment_manager> *sm = new(big_buffer2)
			stldb::segment_within_set<segment_manager>(32*1024*1024);
	char *sub = static_cast<char*>(sm->allocate(33));

	std_allocator alloc4(sm);
	offset_ptr<char> some4 = alloc4.allocate(33);
	offset_ptr<char> some_more4 = alloc4.allocate(330);
	alloc4.deallocate(some4, 33);
	alloc4.deallocate(some_more4, 330);

	clustered_char_allocator_t alloc5(region.get_segment_manager());
	some = alloc5.allocate(33);
	some_more = alloc5.allocate(330);
	alloc5.deallocate(some, 33);
	alloc5.deallocate(some_more, 330);

	clustered_gnu_char_allocator_t alloc6(region.get_segment_manager());
	typedef clustered_gnu_char_allocator_t::pointer relative_ptr;
	relative_ptr some6 = alloc6.allocate(33);
	relative_ptr some_more6 = alloc6.allocate(330);
	alloc6.deallocate(some6, 33);
	alloc6.deallocate(some_more6, 330);

	return 0;
}
