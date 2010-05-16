/*
 * variable_node_allocator.h
 *
 *  Created on: Feb 22, 2010
 *      Author: RWalter3
 */

#ifndef VARIABLE_NODE_ALLOCATOR_H_
#define VARIABLE_NODE_ALLOCATOR_H_

#include <vector>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/allocators/cached_node_allocator.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/detail/utilities.hpp>
#include <boost/interprocess/detail/type_traits.hpp>

#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <stldb/timing/timer.h>

namespace stldb {

using boost::interprocess::bad_alloc;
using boost::interprocess::detail::add_reference;
using boost::interprocess::detail::get_pointer;

using boost::intrusive::set_base_hook;
using boost::intrusive::void_pointer;

// Note: The alignment of the SegmentManager base class is important.
// it must be aligned on SegmentManager::memory_algorithm::Alignment
// bytes.  TODO - make the satisfaction of this requirement assured.
// right now I'm just getting lucky with meeting the requirement.
template<class SegmentManager>
class segment_within_set
	: public set_base_hook<void_pointer<typename SegmentManager::void_pointer> >
	, public SegmentManager
{
public:
	segment_within_set(std::size_t size)
		: SegmentManager(size - (sizeof(segment_within_set) - sizeof(SegmentManager)))
		{ }

	// as intrusive node elements, all comparisons can be based on location
	bool operator<(const segment_within_set &rarg) const {
		return this < &rarg;
	}
};


/**
 * segment_set is a boost::interprocess::set of pointers to SegmentManagers
 * which are each BytesPerChunk bytes in size, and allocated from parts of
 * a larger manageemnt space under the control of SegmentManager.
 */
template<class SegmentManager, std::size_t BytesPerChunk>
class segment_set
	: public boost::intrusive::set< segment_within_set<SegmentManager> >
{
private:
	typedef typename SegmentManager::void_pointer  void_pointer;

public:
	typedef segment_within_set<SegmentManager>         segment_t;
	typedef typename boost::intrusive::set< segment_t> base_t;

	static const std::size_t segment_size = BytesPerChunk;

	segment_set()
		: base_t()
		, m_mutex()
		, m_lastalloc( base_t::end() )
		{ }

	boost::interprocess::interprocess_upgradable_mutex m_mutex;

	typename base_t::iterator m_lastalloc;

	// allocate memory from one of the segments within this set
	// can throw bad_alloc if host segment is out of memory.
	void * allocate(std::size_t size, SegmentManager *host) 
	{
		  boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>
			  shared_guard(m_mutex);

		if (size > BytesPerChunk)
			return NULL;
		// if m_lastalloc is set, then it points to a segment which
		// previously succeeded in memory allocation.  Try it first.
		// the idea here is to amortize segment iteration to O(1) over time.
		if (m_lastalloc != base_t::end()) {
			try {
				return m_lastalloc->allocate(size);
			}
			catch(boost::interprocess::bad_alloc &ex) 
			{ }
		}

		// release shared lock and get exclusive, for insertion.
		shared_guard.unlock();
		stldb::timer("segment_set::advance m_lastalloc");
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>
			  guard(m_mutex);

		void *result = NULL;
		typename base_t::iterator i;
		for (i = base_t::begin(); i != base_t::end(); i++ ) {
			  try {
				  result = i->allocate(size);
				  m_lastalloc = i;
				  return result;
			  }
			  catch(boost::interprocess::bad_alloc &ex) { }
		}

		// new need to allocate a new segment.
		segment_t *sm = new_segment( host );
		try {
			result = sm->allocate(size);
		}
		catch(boost::interprocess::bad_alloc &ex) {
			std::cerr << "Unexpected bad_alloc on new segment " << size << ex.what() << std::endl;
			abort();
		}

		base_t::insert( *sm );
		m_lastalloc = sm;
		return result;
	}

	// return the address of the particular segment containing the memory
	// at address p.
	// caller must hold m_mutex (sharable)
	segment_t* segment_containing(void *p) 
	{
		typename base_t::iterator i( base_t::upper_bound( *reinterpret_cast<segment_t*>(p)) );
		if ( i == this->end() && i != this->begin() )
			i--;
		void *bottom = &(*i);
		void *top = reinterpret_cast<char*>( bottom ) + i->get_size();
		if ( p > bottom && p < top )
			  return &(*i);
		return NULL;
	}

private:
	// allocate a Chunk within host.
	// does not actually insert the allocated chunk into this.
	// can throw bad_alloc if host segment is out of memory.
	segment_t* new_segment(SegmentManager *host) 
	{
		stldb::timer("segment_set::new_segment");

		// Safety Check if there is enough space
		assert(BytesPerChunk > SegmentManager::get_min_size());

		segment_t* new_sm = NULL;

		//Let's construct the allocator in memory
		void *addr = host->allocate(BytesPerChunk);
		new_sm = new(addr) segment_t(BytesPerChunk);
		//assert(new_sm == addr);
		//assert(new_sm->check_sanity());

		// debug: double check that the address of the SegmentManager is
		// aligned according to requirements.
		assert((0 == (((std::size_t)static_cast<SegmentManager*>(new_sm)) & (SegmentManager::memory_algorithm::Alignment - std::size_t(1u)))));

		return new_sm;
	}
};



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
	// lazy initialization is used with m_segments
	variable_node_allocator(SegmentManager *sm)
		: m_segment_manager( sm )
		, m_segments( NULL )
		{ }

	//!Constructor from other allocator.
	//!Never throws
	variable_node_allocator(const variable_node_allocator &other)
		: m_segment_manager(other.m_segment_manager)
		, m_segments(other.m_segments)
		{ }

	//!Constructor from related allocator.
	//!Never throws
	template<class T2>
	variable_node_allocator(const variable_node_allocator<T2, SegmentManager, BytesPerChunk> &other)
	    : m_segment_manager(other.m_segment_manager)
	    , m_segments(other.m_segments)
	    { }


	//!Returns the segment manager.
	//!Never throws
	SegmentManager* get_segment_manager()const
		{  return get_pointer(m_segment_manager);   }

	//!Allocates memory for an array of count elements.
	//!Throws boost::interprocess::bad_alloc if there is no enough memory
	pointer allocate(size_type count, const_void_pointer hint = 0)
	{
		stldb::timer("variable_node_allocator::allocate");
		  (void)hint; // not used
		  if(count*sizeof(T) > this->max_size())
			 throw boost::interprocess::bad_alloc();
		  if (count*sizeof(T) > BytesPerChunk)
			  return pointer(static_cast<value_type*>(m_segment_manager->allocate(count*sizeof(T))));
		if (!m_segments)
			init_segments();
		  return pointer(static_cast<value_type*>(m_segments->allocate(count*sizeof(T), get_pointer(m_segment_manager))));
	}

	//!Deallocates memory previously allocated.
	//!Never throws
	void deallocate(const pointer &ptr, size_type count)
	{
		stldb::timer("variable_node_allocator::deallocate");
		void *p = get_pointer(ptr);
		if (count*sizeof(T) > BytesPerChunk) {
			m_segment_manager->deallocate(p);
			return;
		}
		// find the specific segment_manager which is likely to
		if (!m_segments)
			init_segments();
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>
			  shared_guard(m_segments->m_mutex);
		SegmentManager *sm = m_segments->segment_containing(p);
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
		if (!m_segments)
			init_segments();
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>
			shared_guard(m_segments->m_mutex);
		void *ptr = get_pointer(p);
		SegmentManager *sm = m_segments->segment_containing(ptr);
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

	typedef segment_set<SegmentManager,BytesPerChunk>  segment_set_t;

	typedef typename boost::pointer_to_other
	      <void_pointer, segment_set_t >::type     segment_set_ptr_t;

	segment_manager_ptr_t m_segment_manager;
	segment_set_ptr_t m_segments;

	inline void init_segments() {
		stldb::timer("variable_node_allocator::init_segments");
		// find_or_construct the unique instance of segment_set_t
		m_segments = m_segment_manager->
			template find_or_construct<segment_set_t >
			(boost::interprocess::unique_instance)();
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
