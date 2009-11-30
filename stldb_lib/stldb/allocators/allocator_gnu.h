/*
 * allocator.h
 *
 *  Created on: Feb 2, 2009
 *      Author: bobw
 */

#ifndef STLDB_GNU_ALLOCATOR_H_
#define STLDB_GNU_ALLOCATOR_H_

#include <boost/interprocess/allocators/allocator.hpp>
#include <stldb/allocators/scoped_allocation.h>

#if (__GNUC__ > 4 || (__GNUC__==4 && __GNUC_MINOR__ >=4))
#include <ext/cast.h>
#include <ext/pointer.h>
#else
#include <stldb/containers/gnu/pointer.h>
#include <stldb/containers/gnu/cast.h>
#endif

namespace stldb {

/**
 * Wraps a standard boost::interprocess allocator, but changes the pointer
 * type to _Relative_pointer from the GNU libstdc++ library.  This creates an
 * allocator which can be used with GNU containers from the 4.4.0+ version of
 * libstdc++.  Note that some GNU containers (e.g. basic_string) also require
 * that the allocator have a default constructor and equality comparison operators.
 * In that case, this wrapper must be combined with the scope_aware_allocator wrapper
 * to create a workable allocator.
 */
template <class WrappedAllocator>
class gnu_adapter : public WrappedAllocator {

private:
	//Not assignable from related gnu_adapter
	template<class SomeAllocType>
	gnu_adapter& operator=(const SomeAllocType&);

	//Not assignable from other gnu_adapter
	gnu_adapter& operator=(const gnu_adapter&);

public:
	typedef typename WrappedAllocator::segment_manager segment_manager;
	typedef typename WrappedAllocator::value_type  value_type;

	// overload the pointer typedefs to use the GNU _Pointer_adapter.
	typedef typename __gnu_cxx::_Pointer_adapter<__gnu_cxx::_Relative_pointer_impl<void> >  void_pointer;
	typedef typename __gnu_cxx::_Pointer_adapter<__gnu_cxx::_Relative_pointer_impl<const void> >  cvoid_ptr;

	typedef typename __gnu_cxx::_Pointer_adapter<__gnu_cxx::_Relative_pointer_impl<value_type> >       pointer;
	typedef typename __gnu_cxx::_Pointer_adapter<__gnu_cxx::_Relative_pointer_impl<const value_type> > const_pointer;

	typedef typename WrappedAllocator::reference       reference;
	typedef typename WrappedAllocator::const_reference const_reference;
	typedef typename WrappedAllocator::size_type       size_type;
	typedef typename WrappedAllocator::difference_type difference_type;

	//!Constructor from an explicit segment manager.
	//!Never throws
	gnu_adapter(segment_manager *segment_mngr)
	    : WrappedAllocator(segment_mngr) { }

	//!Constructor from other allocator.
	//!Never throws
	gnu_adapter(const gnu_adapter &other)
	    : WrappedAllocator(other.get_segment_manager()) { }

	//!Constructor from WrappedAllocator
	//!Never throws
	gnu_adapter(const WrappedAllocator &other)
    : WrappedAllocator(other.get_segment_manager()) { }

	//!Constructor from any allocator with same segment manager type
	//!Never throws
	template<class T2, template <class Type, class SegMgr> class AllocTempl>
	gnu_adapter(const AllocTempl<T2, segment_manager> &other)
		: WrappedAllocator(other.get_segment_manager()) { }

	//!Obtains an scope_aware_allocator that allocates
	//!objects of type T2
	template<class T2>
	struct rebind
	{
		typedef gnu_adapter< typename WrappedAllocator::template rebind<T2>::other >  other;
	};

	//!Allocates memory for an array of count elements.
	//!Throws boost::interprocess::bad_alloc if there is no enough memory
	pointer allocate(size_type count, cvoid_ptr hint = 0)
	{ return boost::interprocess::detail::get_pointer(WrappedAllocator::allocate(count,
			__gnu_cxx::__static_pointer_cast<typename WrappedAllocator::const_pointer>(hint))); }

	//!Deallocates memory previously allocated.
	//!Never throws
	void deallocate(pointer ptr, size_type s)
	{  WrappedAllocator::deallocate(__gnu_cxx::__static_pointer_cast<
			typename WrappedAllocator::pointer>(ptr),s);  }


};

//!Equality test for same type
//!of allocator
template<class WrappedAllocator> inline
bool operator==(const gnu_adapter<WrappedAllocator> &alloc1,
                const gnu_adapter<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() == alloc2.get_segment_manager(); }

//!Inequality test for same type
//!of scope_aware_allocator
template<class WrappedAllocator> inline
bool operator!=(const gnu_adapter<WrappedAllocator>  &alloc1,
                const gnu_adapter<WrappedAllocator>  &alloc2)
   {  return alloc1.get_segment_manager() != alloc2.get_segment_manager(); }




} // namespace stldb

#endif /* ALLOCATOR_H_ */
