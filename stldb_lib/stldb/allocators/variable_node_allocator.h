/*
 * variable_node_allocator.h
 *
 *  Created on: Feb 22, 2010
 *      Author: RWalter3
 */

#ifndef VARIABLE_NODE_ALLOCATOR_H_
#define VARIABLE_NODE_ALLOCATOR_H_

#include <vector>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/detail/utilities.hpp>
#include <boost/interprocess/detail/type_traits.hpp>

using namespace boost::interprocess::detail;
using boost::interprocess::detail::add_reference;
using boost::interprocess::detail::get_pointer;

namespace stldb {


template<class T, class SegmentManager, std::size_t BytesPerChunk>
class variable_node_allocator {
public:

	typedef SegmentManager                        segment_manager;

	typedef typename SegmentManager::void_pointer void_pointer;
	typedef typename boost::pointer_to_other
	   <void_pointer, const void>::type           const_void_pointer;

	typedef T                                    value_type;
	typedef typename boost::pointer_to_other
	   <void_pointer, T>::type                   pointer;
	typedef typename boost::pointer_to_other
	   <pointer, const T>::type                  const_pointer;
	typedef typename add_reference
	                  <value_type>::type         reference;
	typedef typename add_reference
	                  <const value_type>::type   const_reference;
	typedef std::size_t                          size_type;
	typedef std::ptrdiff_t                       difference_type;

private:
	typedef variable_node_allocator<T, SegmentManager, BytesPerChunk> self_t;

	//Not assignable from related allocator
	template<class T2, class SegmentManager2, std::size_t BytesPerChunk2>
	variable_node_allocator& operator=(const variable_node_allocator<T2, SegmentManager2, BytesPerChunk2>&);

	//Not assignable from other allocator
	variable_node_allocator& operator=(const variable_node_allocator&);

public:

	//!Obtains an allocator that allocates
	//!objects of type T2
	template<class T2>
	struct rebind
	{
		typedef variable_node_allocator<T2, SegmentManager, BytesPerChunk> other;
	};

	// Construct a variable_node_allocator from a segment manager
	variable_node_allocator(SegmentManager *sm)
		: m_segment_manager( sm )
		, m_segments( init(sm) )
		{ }

	//!Constructor from other allocator.
	//!Never throws
	variable_node_allocator(const variable_node_allocator &other)
		: m_segment_manager(other.get_segment_manager())
		, m_segments(other.m_segments)
		{ }

	//!Constructor from related allocator.
	//!Never throws
	template<class T2>
	variable_node_allocator(const variable_node_allocator<T2, SegmentManager, BytesPerChunk> &other)
	    : m_segment_manager(other.get_segment_manager())
	    , m_segments(other.m_segments)
	    {}

	~variable_node_allocator();

	//!Returns the segment manager.
	//!Never throws
	SegmentManager* get_segment_manager()const
		{  return get_pointer(m_segment_manager);   }

	//!Allocates memory for an array of count elements.
	//!Throws boost::interprocess::bad_alloc if there is no enough memory
	pointer allocate(size_type count, const_void_pointer hint = 0)
	{
		  (void)hint; // not used
		  if(count > this->max_size())
			 throw bad_alloc();
		  if (count*sizeof(T) > BytesPerChunk)
			  return pointer(static_cast<value_type*>(m_segment_manager->allocate(count*sizeof(T))));

		  boost::interprocess::sharable_lock<typename boost::interprocess::interprocess_upgradable_mutex>
			  shared_guard(m_segments->m_mutex);
		  for (typename segment_set_t<BytesPerChunk>::iterator i = m_segments->begin(); i != m_segments->end(); i++ ) {
			  try {
				  return pointer(static_cast<value_type*>(i->second->allocate(count*sizeof(T))));
			  }
			  catch(bad_alloc &ex) {
				  continue;
			  }
		  }
		  // we must add an additional segment.
		  SegmentManager *sm = new_segment( BytesPerChunk );
		  // release shared lock and get exclusive, for insertion.
		  shared_guard.unlock();
		  boost::interprocess::scoped_lock<typename boost::interprocess::interprocess_upgradable_mutex>
			  guard(m_segments->m_mutex);
		  m_segments->insert( sm );
		  return pointer(static_cast<value_type*>(sm->allocate(count*sizeof(T))));
	}

	//!Deallocates memory previously allocated.
	//!Never throws
	void deallocate(const pointer &ptr, size_type count)
	{
		void *p = get_pointer(ptr);
		if (count*sizeof(T) > BytesPerChunk) {
			m_segment_manager->deallocate(p);
			return;
		}
		// find the specific segment_manager which is likely to
		boost::interprocess::sharable_lock<typename boost::interprocess::interprocess_upgradable_mutex>
			  shared_guard(m_segments->m_mutex);
		SegmentManager *sm = segment_containing(p);
		if ( sm )
			sm->deallocate(p);
		else
			m_segment_manager->deallocate(p);
	}

	//!Returns the number of elements that could be allocated.
	//!Never throws
	size_type max_size() const
	{  return m_segment_manager->get_size()/sizeof(T);   }

	//!Swap segment manager. Does not throw. If each allocator is placed in
	//!different memory segments, the result is undefined.
	friend void swap(self_t &alloc1, self_t &alloc2)
	{
		boost::interprocess::detail::do_swap(alloc1.m_segment_manager, alloc2.m_segment_manager);
		boost::interprocess::detail::do_swap(alloc1.m_segments, alloc2.m_segments);
	}

	//!Returns maximum the number of objects the previously allocated memory
	//!pointed by p can hold. This size only works for memory allocated with
	//!allocate, allocation_command and allocate_many.
	size_type size(const pointer &p) const
	{
		  boost::interprocess::sharable_lock<typename boost::interprocess::interprocess_upgradable_mutex>
			  shared_guard(m_segments->m_mutex);
		  void *ptr = get_pointer(p);
		  SegmentManager *sm = segment_containing(ptr);
		  if ( sm )
			  return (size_type)sm->size(ptr)/sizeof(T);
		  return (size_type)m_segment_manager->size(ptr)/sizeof(T);
	}

	//!Returns address of mutable object.
	//!Never throws
	pointer address(reference value) const
	{  return pointer(boost::addressof(value));  }

	//!Returns address of non mutable object.
	//!Never throws
	const_pointer address(const_reference value) const
	{  return const_pointer(boost::addressof(value));  }

	//!Copy construct an object
	//!Throws if T's copy constructor throws
	void construct(const pointer &ptr, const_reference v)
	{  new((void*)get_pointer(ptr)) value_type(v);  }

	//!Default construct an object.
	//!Throws if T's default constructor throws
	void construct(const pointer &ptr)
	{  new((void*)get_pointer(ptr)) value_type;  }

	//!Destroys object. Throws if object's
	//!destructor throws
	void destroy(const pointer &ptr)
	{  BOOST_ASSERT(ptr != 0); (*ptr).~value_type();  }


private:

	typedef typename boost::pointer_to_other
	      <void_pointer, SegmentManager>::type     segment_manager_ptr_t;

	typedef typename boost::interprocess::cached_node_allocator<
		SegmentManager*, SegmentManager> segment_set_allocator;

	// A set of SegmentManager*, with a mutex, whose type is also tied to BytesPerChunk
	template<std::size_t BytesPerChunk_>
	struct  segment_set_t : public boost::interprocess::set<SegmentManager*,
		std::less<SegmentManager*>, segment_set_allocator>
	{
		segment_set_t(const std::less<SegmentManager*>& comp, 
		              const segment_set_allocator &alloc)
			: boost::interprocess::set<SegmentManager*,
			  std::less<SegmentManager*>, segment_set_allocator>(comp,alloc)
		{ }

		boost::interprocess::interprocess_upgradable_mutex m_mutex;
	};

	typedef typename boost::pointer_to_other
	      <void_pointer, segment_set_t<BytesPerChunk> >::type     segment_set_ptr_t;


	segment_manager_ptr_t m_segment_manager;
	segment_set_ptr_t m_segments;


	segment_set_t<BytesPerChunk>* init(SegmentManager *sm) {
		// find_or_construct the unique instance of segment_set_t
		segment_set_allocator alloc(sm);
		return sm->template find_or_construct<segment_set_t<BytesPerChunk> >
			(boost::interprocess::unique_instance)
			(std::less<SegmentManager*>(), alloc);
	}

	SegmentManager* new_segment(std::size_t size) {
	      // Safety Check if there is enough space
	      if(size < SegmentManager::get_min_size())
	         return NULL;

	      // This function should not throw. The index construction can
	      // throw if constructor allocates memory. So we must catch it.
	      SegmentManager* new_sm = NULL;
	      BOOST_TRY{
	         //Let's construct the allocator in memory
		     void *addr = m_segment_manager->allocate(size);
	    	 new_sm = new(addr) SegmentManager(size);
	      }
	      BOOST_CATCH(...){
	         return NULL;
	      }
	      BOOST_CATCH_END
	      return new_sm;
	}

	SegmentManager * segment_containing(void *p) {
		typename segment_set_t<BytesPerChunk>::iterator i = m_segments->upper_bound(
				reinterpret_cast<SegmentManager*>(p));
		if ( i == m_segments->end() )
			i--;
		SegmentManager *sm = *i;
		if ( p > sm && p < (reinterpret_cast<char*>(sm) + sm->get_size()) )
			  return sm;
		return NULL;
	}

};

//!Equality test for same type
//!of allocator
template<class T, class SegmentManager, std::size_t BytesPerChunk> inline
bool operator==(const variable_node_allocator<T , SegmentManager, BytesPerChunk>  &alloc1,
                const variable_node_allocator<T, SegmentManager, BytesPerChunk>  &alloc2)
   {  return alloc1.get_segment_manager() == alloc2.get_segment_manager(); }

//!Inequality test for same type
//!of allocator
template<class T, class SegmentManager, std::size_t BytesPerChunk> inline
bool operator!=(const variable_node_allocator<T, SegmentManager, BytesPerChunk>  &alloc1,
                const variable_node_allocator<T, SegmentManager, BytesPerChunk>  &alloc2)
   {  return alloc1.get_segment_manager() != alloc2.get_segment_manager(); }

} // namespace stldb {


namespace boost {

template<class T>
struct has_trivial_destructor;

template<class T, class SegmentManager, std::size_t BytesPerChunk>
struct has_trivial_destructor
   <stldb::variable_node_allocator <T, SegmentManager, BytesPerChunk> >
{
   enum { value = true };
};
/// @endcond

} // namespace boost

#endif /* VARIABLE_NODE_ALLOCATOR_H_ */
