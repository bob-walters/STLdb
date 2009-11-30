/*
 * allocator.h
 *
 *  Created on: Feb 2, 2009
 *      Author: bobw
 */

#ifndef REGION_OR_HEAP_ALLOCATOR_H_
#define REGION_OR_HEAP_ALLOCATOR_H_

#include <boost/interprocess/allocators/allocator.hpp>
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/allocator_gnu.h>
#include <stldb/trace.h>

namespace stldb {

/**
 * region_or_heap allocator is an allocator type which can be constructed so
 * as to allocate memory from either the general heap (HeapAllocator) or from
 * a specific region (via WrappedAllocator).  The region can be specified explicitly
 * to the constructor.
 * If no region is provided, the allocator will use the region that is in scope
 * as per scoped_allocation.  If no region is in scope, and no explicit region is
 * specified, then as a last resort, the allocator resorts to using heap-based memory.
 *
 * Because of this behavior, the allocator presents a danger: memory must be deallocated
 * using an allocator which could have created it (either region-based, or heap-based)
 */
template <class WrappedAllocator,
          class HeapAllocator = std::allocator<typename WrappedAllocator::value_type> >
class region_or_heap_allocator : public WrappedAllocator {

private:
	//Not assignable from related region_or_heap_allocator
	template<class SomeAllocType>
	region_or_heap_allocator& operator=(const SomeAllocType&);

	//Not assignable from other region_or_heap_allocator
	region_or_heap_allocator& operator=(const region_or_heap_allocator&);

	HeapAllocator  heap_alloc;

	// triad of overloaded funcs to deal with possible types of WrappedAllocator::pointer.
	template <typename value_type>
	value_type* get_pointer(
		__gnu_cxx::_Pointer_adapter<__gnu_cxx::_Relative_pointer_impl<value_type> > &rarg)
		{ return rarg.get(); }

	template <typename value_type>
	value_type* get_pointer(boost::interprocess::offset_ptr<value_type> &rarg)
		{ return rarg.get(); }

	template <typename value_type>
	value_type* get_pointer(value_type *p)
		{ return p; }

public:
	typedef typename WrappedAllocator::segment_manager segment_manager;
	typedef typename WrappedAllocator::pointer  pointer;
	typedef typename WrappedAllocator::size_type size_type;
	typedef typename WrappedAllocator::template rebind<void>::other::const_pointer const_void_pointer;

	//!Construct an region_or_heap_allocator using the SegmentManager currently in scope
	//!via the scoped_allocation class.  Throws if no segment manager is
	//!in scope
	region_or_heap_allocator()
		: WrappedAllocator( scoped_allocation<segment_manager>::scoped_manager() )
		, heap_alloc() { }

	//!Constructor from an explicit segment manager.
	//!Never throws
	region_or_heap_allocator(segment_manager *segment_mngr)
	    : WrappedAllocator(segment_mngr)
		, heap_alloc() { }

	//!Constructor from other region_or_heap_allocator.
	//!Never throws
	region_or_heap_allocator(const region_or_heap_allocator &other)
	    : WrappedAllocator(other.get_segment_manager())
	    , heap_alloc() { }

	//!Constructor from related region_or_heap_allocator.
	//!Never throws
	template<class T2, template <class Type, class SegMgr> class AllocTempl>
	region_or_heap_allocator(const AllocTempl<T2, segment_manager> &other)
		: WrappedAllocator(other.get_segment_manager())
	    , heap_alloc() { }

	//!Constructor from related region_or_heap_allocator.
	//!Never throws
/* TODO - struggling to get this better match to work.  The one below is a little loose... */
//	template<class T2, template <class Type, class SegMgr> class AllocTempl>
//	region_or_heap_allocator(const stldb::gnu_adapter< AllocTempl<T2, segment_manager> > &other)
//		: WrappedAllocator(other.get_segment_manager())
//		, heap_alloc() { }

	//!Constructor from related allocator.
	//!Never throws
	template<class Alloc2>
	region_or_heap_allocator(const Alloc2 &other)
		: WrappedAllocator(other.get_segment_manager())
	    , heap_alloc() { }

	//!Allocates memory for an array of count elements.
	//!Throws boost::interprocess::bad_alloc if there is no enough memory
	pointer allocate(size_type count, const_void_pointer hint = NULL )
	{
	  (void)hint;
	  if(count > this->max_size())
		 throw boost::interprocess::bad_alloc();
	  if (WrappedAllocator::get_segment_manager() == NULL) {
	  	  STLDB_TRACE(finest_e, "Allocating from heap, " << count << " bytes");
		  return pointer(heap_alloc.allocate(count));
	  }
	  STLDB_TRACE(finest_e, "Allocating from shm, " << count << " bytes");
	  return WrappedAllocator::allocate(count);
	}

	//!Deallocates memory previously allocated.
	//!Never throws
	void deallocate(pointer ptr, size_type s)
	{
		if (WrappedAllocator::get_segment_manager() == NULL) {
	  	    STLDB_TRACE(finest_e, "Deallocating to heap, " << s << " bytes");
			heap_alloc.deallocate(this->get_pointer(ptr), s);
			return;
		}
	  STLDB_TRACE(finest_e, "Deallocating to shm, " << s << " bytes");
		WrappedAllocator::deallocate(ptr, s);
	}

	//!Returns the number of elements that could be allocated.
	//!Never throws
	size_type max_size() const
	{
		return (WrappedAllocator::get_segment_manager() == NULL) ?
			heap_alloc.max_size() : WrappedAllocator::max_size();
	}

	//!Obtains an region_or_heap_allocator that allocates
	//!objects of type T2
	template<class T2>
	struct rebind
	{
		typedef region_or_heap_allocator< typename WrappedAllocator::template rebind<T2>::other >  other;
	};
};

//!Equality test for same type
//!of region_or_heap_allocator
template<class WrappedAllocator> inline
bool operator==(const region_or_heap_allocator<WrappedAllocator> &alloc1,
                const region_or_heap_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() == alloc2.get_segment_manager(); }

//!Inequality test for same type
//!of region_or_heap_allocator
template<class WrappedAllocator> inline
bool operator!=(const region_or_heap_allocator<WrappedAllocator>  &alloc1,
                const region_or_heap_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() != alloc2.get_segment_manager(); }



} // namespace stldb

#endif /* ALLOCATOR_H_ */
