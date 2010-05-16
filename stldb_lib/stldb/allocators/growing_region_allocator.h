/*
 * allocator.h
 *
 *  Created on: Feb 2, 2009
 *      Author: bobw
 */

#ifndef GROWING_REGION_ALLOCATOR_H_
#define GROWING_REGION_ALLOCATOR_H_

#include <boost/interprocess/allocators/allocator.hpp>
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/allocator_gnu.h>
#include <stldb/trace.h>

namespace stldb {

/**
 * growing_region allocator is an allocator type which can be used with datatypes
 * stored in a dynamic_managed_mapped_file.  It allows the allocator to deal with
 * bad_alloc exceptions by increasing the region size, and retrying the allocation.
 */
template <class WrappedAllocator, class ManagedRegion>
class growing_region_allocator : public WrappedAllocator {

private:
	//Not assignable from related growing_region_allocator
	template<class SomeAllocType>
	growing_region_allocator& operator=(const SomeAllocType&);

	//Not assignable from other growing_region_allocator
	growing_region_allocator& operator=(const growing_region_allocator&);

public:
	typedef typename WrappedAllocator::segment_manager segment_manager;
	typedef typename WrappedAllocator::pointer   pointer;
	typedef typename WrappedAllocator::size_type size_type;
	typedef typename WrappedAllocator::template rebind<void>::other::const_pointer const_void_pointer;

	//!Construct an growing_region_allocator using the SegmentManager currently in scope
	//!via the scoped_allocation class.  Throws if no segment manager is
	//!in scope
	growing_region_allocator()
		: WrappedAllocator( scoped_allocation<segment_manager>::scoped_manager() )
		{ }

	//!Constructor from an explicit segment manager.
	//!Never throws
	growing_region_allocator(segment_manager *segment_mngr)
	    : WrappedAllocator(segment_mngr)
		{ }

	//!Constructor from other growing_region_allocator.
	//!Never throws
	growing_region_allocator(const growing_region_allocator &other)
	    : WrappedAllocator(other.get_segment_manager())
	    { }

	//!Constructor from related growing_region_allocator.
	//!Never throws
	template<class T2, template <class Type, class SegMgr> class AllocTempl>
	growing_region_allocator(const AllocTempl<T2, segment_manager> &other)
		: WrappedAllocator(other.get_segment_manager())
	    { }

	//!Constructor from related growing_region_allocator.
	//!Never throws
/* TODO - struggling to get this better match to work.  The one below is a little loose... */
//	template<class T2, template <class Type, class SegMgr> class AllocTempl>
//	growing_region_allocator(const stldb::gnu_adapter< AllocTempl<T2, segment_manager> > &other)
//		: WrappedAllocator(other.get_segment_manager())
//		{ }

	//!Constructor from related allocator.
	//!Never throws
	template<class Alloc2>
	growing_region_allocator(const Alloc2 &other)
		: WrappedAllocator(other.get_segment_manager())
	    { }

	//!Allocates memory for an array of count elements.
	//!Throws boost::interprocess::bad_alloc if there is no enough memory
	pointer allocate(size_type count, const_void_pointer hint = NULL )
	{
	  while (true) {
		  std::size_t region_size = get_segment_manager()->get_size();
		  try {
			  return WrappedAllocator::allocate(count, hint);
		  }
		  catch( boost::interprocess::bad_alloc ) {
			  segment_manager *segment = this->get_segment_manager();
			  ManagedRegion *db = ManagedRegion::region_at(segment);
			  if (!db || !db->grow_in_place(region_size))
				  throw;
		  }
	  }
	}

	//!Obtains an growing_region_allocator that allocates
	//!objects of type T2
	template<class T2>
	struct rebind
	{
		typedef growing_region_allocator< typename WrappedAllocator::template rebind<T2>::other >  other;
	};
};

//!Equality test for same type
//!of growing_region_allocator
template<class WrappedAllocator> inline
bool operator==(const growing_region_allocator<WrappedAllocator> &alloc1,
                const growing_region_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() == alloc2.get_segment_manager(); }

//!Inequality test for same type
//!of growing_region_allocator
template<class WrappedAllocator> inline
bool operator!=(const growing_region_allocator<WrappedAllocator>  &alloc1,
                const growing_region_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() != alloc2.get_segment_manager(); }


} // namespace stldb

#endif /* ALLOCATOR_H_ */
