/*
 * allocator.h
 *
 *  Created on: Feb 2, 2009
 *      Author: bobw
 */

#ifndef SCOPE_AWARE_ALLOCATOR_H_
#define SCOPE_AWARE_ALLOCATOR_H_

#include <boost/interprocess/allocators/allocator.hpp>
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/allocator_gnu.h>

namespace stldb {

/**
 * scope_aware_allocator is a wrapper for the allocator types which come
 * with boost::interprocess.  It adds a default constructor
 * which constructs the base allocator using the segment manager which has been
 * designated by a scoped_allocation object.
 */
template <class WrappedAllocator>
class scope_aware_allocator : public WrappedAllocator {

private:
	//Not assignable from related scope_aware_allocator
	template<class SomeAllocType>
	scope_aware_allocator& operator=(const SomeAllocType&);

	//Not assignable from other scope_aware_allocator
	scope_aware_allocator& operator=(const scope_aware_allocator&);

public:
	typedef typename WrappedAllocator::segment_manager segment_manager;

	//!Construct an scope_aware_allocator using the SegmentManager currently in scope
	//!via the scoped_allocation class.  Throws if no segment manager is
	//!in scope
	scope_aware_allocator()
		: WrappedAllocator( scoped_allocation<segment_manager>::scoped_manager() )
	{
		if (WrappedAllocator::get_segment_manager()==NULL) {
			throw boost::interprocess::bad_alloc();
		}
	}

	//!Constructor from an explicit segment manager.
	//!Never throws
	scope_aware_allocator(segment_manager *segment_mngr)
	    : WrappedAllocator(segment_mngr) { }

	//!Constructor from other scope_aware_allocator.
	//!Never throws
	scope_aware_allocator(const scope_aware_allocator &other)
	    : WrappedAllocator(other.get_segment_manager()) { }

	//!Constructor from related scope_aware_allocator.
	//!Never throws
	template<class T2, template <class Type, class SegMgr> class AllocTempl>
	scope_aware_allocator(const AllocTempl<T2, segment_manager> &other)
		: WrappedAllocator(other.get_segment_manager()) { }

	//!Constructor from related scope_aware_allocator.
	//!Never throws
/* TODO - struggling to get this better match to work.  The one below is a little loose... */
//	template<class T2, template <class Type, class SegMgr> class AllocTempl>
//	scope_aware_allocator(const stldb::gnu_adapter< AllocTempl<T2, segment_manager> > &other)
//		: WrappedAllocator(other.get_segment_manager()) { }

	//!Constructor from related allocator.
	//!Never throws
	template<class Alloc2>
	scope_aware_allocator(const Alloc2 &other)
		: WrappedAllocator(other.get_segment_manager()) { }

	//!Obtains an scope_aware_allocator that allocates
	//!objects of type T2
	template<class T2>
	struct rebind
	{
		typedef scope_aware_allocator< typename WrappedAllocator::template rebind<T2>::other >  other;
	};

	friend void swap(scope_aware_allocator &alloc1, scope_aware_allocator &alloc2)
	{
		swap(static_cast<WrappedAllocator &>(alloc1), static_cast<WrappedAllocator &>(alloc2));
	}
};

//!Equality test for same type
//!of scope_aware_allocator
template<class WrappedAllocator> inline
bool operator==(const scope_aware_allocator<WrappedAllocator> &alloc1,
                const scope_aware_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() == alloc2.get_segment_manager(); }

//!Inequality test for same type
//!of scope_aware_allocator
template<class WrappedAllocator> inline
bool operator!=(const scope_aware_allocator<WrappedAllocator>  &alloc1,
                const scope_aware_allocator<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() != alloc2.get_segment_manager(); }



} // namespace stldb

#endif /* ALLOCATOR_H_ */
