/*
 *  rbtree.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 12/29/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */

#ifndef STLDB_RBTREE_H
#define STLDB_RBTREE_H 1

#include <iterator>
#include <string>
#include <map>
#include <vector>
#include <utility>   // std::pair
#include <functional>
#include <stdint.h> // uint32_t, etc.

#include <boost/intrusive/detail/assert.hpp>
#include <boost/static_assert.hpp>
#include <boost/intrusive/intrusive_fwd.hpp>
#include <boost/intrusive/set_hook.hpp>
#include <boost/intrusive/detail/rbtree_node.hpp>
#include <boost/intrusive/detail/tree_node.hpp>
#include <boost/intrusive/detail/ebo_functor_holder.hpp>
#include <boost/intrusive/detail/mpl.hpp>
#include <boost/intrusive/detail/pointer_to_other.hpp>
#include <boost/intrusive/detail/clear_on_destructor_base.hpp>
#include <boost/intrusive/detail/function_detector.hpp>
#include <boost/intrusive/options.hpp>
#include <boost/intrusive/rbtree_algorithms.hpp>
#include <boost/intrusive/link_mode.hpp>

#include <boost/interprocess/containers/string.hpp>

#include <stldb/exceptions.h>
#include <stldb/container_proxy.h>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/trans_iterator.h>
#include <stldb/containers/iter_less.h>
#include <stldb/containers/detail/rbtree_ops.h>
#include <stldb/containers/detail/utilities.h>
#include <stldb/transaction.h>
//#include <stldb/checkpoint.h>
#include <stldb/trace.h>

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

namespace stldb {

	using boost::interprocess::scoped_lock;
	using namespace std;
	using boost::interprocess::map;
	
	namespace bi = boost::intrusive;
	namespace bidetail = boost::intrusive::detail;
	
	
	// forward declaration of rbtree...
#if !defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED) && !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
	template
	< class T
	, class Allocator
	, class O1  = bi::none
	, class O2  = bi::none
	, class O3  = bi::none
	, class O4  = bi::none
	, class O5  = bi::none
	, class O6  = bi::none
	>
#else
	template<class T, class Allocator, class ...Options>
#endif
	class rbtree;
	
	
	// stldb::rbtree uses an extended set of attributes, adding allocator_type
	// and mutex_family.
	template <class ValueTraits, class Compare, class SizeType, bool ConstantTimeSize, class MutexFamily, class AllocatorType>
	struct setopt
	{
		typedef ValueTraits  value_traits;
		typedef Compare      compare;
		typedef SizeType     size_type;
		static const bool constant_time_size = ConstantTimeSize;
		typedef MutexFamily  mutex_family;
		typedef AllocatorType allocator_type;
	};
	
	//!This option setter specifies the allocator type that
	//!the container will use for memory allocation and destruction.
	template<class Allocator>
	struct allocator_type
	{
		/// @cond
		template<class Base>
		struct pack : Base
		{
			typedef Allocator allocator_type;
		};
		/// @endcond
	};
	
	//!This option setter specifies the type that
	//!the container will use for mutexes and condition variables.
	template<class MutexFamily>
	struct mutex_family
	{
		/// @cond
		template<class Base>
		struct pack : Base
		{
			typedef MutexFamily mutex_family;
		};
		/// @endcond
	};
	
	// The default options for a stldb::rbtree.  Unfortunately, there is no possible default for
	// the allocator_type, because its type depends on the Database type the tree is in. 
	template <class T>
	struct set_defaults
	:  bi::pack_options
		< bi::none
		, bi::base_hook<bi::detail::default_set_hook>
		, bi::constant_time_size<true>
		, bi::size_type<std::size_t>
		, bi::compare<std::less<T> >
		, mutex_family<stldb::bounded_mutex_family>
		>::type
	{};
	
	template <class IteratorType>
	class pending_change_lookup {
		typedef typename IteratorType::pointer pointer;
		typedef typename IteratorType::reference reference;
		
		inline pointer get_pending_update( IteratorType &location ) { }
		
		inline reference addPendingUpdate( Transaction &trans, reference newVal ) { }
	};
	
	template <class T>
	struct fake_copy_constructor : public T {
		fake_copy_constructor()
			: T() { }
		fake_copy_constructor(const fake_copy_constructor &)
			: T() { }
	};
	
	/**
	 * A transactional version of boost::intrusive::rbtree, implemented as a wrapper over
	 * a standard rbtree.
	 */
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	template<class T, class Allocator, class ...Options>
#else
	template<class Config>
#endif
	class rbtree_impl
	: public boost::intrusive::rbtree_impl<Config>
	{
		/// @cond
		template<class C> friend class bidetail::clear_on_destructor_base;
		/// @endcond
		
		// mutex and condition types used within map.
		typedef typename Config::mutex_family                 			mutex_family;
		typedef typename mutex_family::upgradable_mutex_type			upgradable_mutex_type;
		typedef typename mutex_family::mutex_type           			mutex_type;
		typedef typename mutex_family::condition_type       			condition_type;

	public:
		typedef boost::intrusive::rbtree_impl<Config>					baseclass;
		typedef typename Config::allocator_type                         allocator_type;
		
		// these typedefs are public in rbtree, and thus must all be defined
		typedef typename baseclass::value_traits						value_traits;
		typedef typename baseclass::real_value_traits					real_value_traits;
		typedef typename baseclass::pointer								pointer;
		typedef typename baseclass::const_pointer						const_pointer;
		typedef typename baseclass::value_type							value_type;
		typedef typename baseclass::key_type							key_type;
		typedef typename baseclass::reference							reference;
		typedef typename baseclass::const_reference						const_reference;
		typedef typename baseclass::difference_type						difference_type;
		typedef typename baseclass::size_type							size_type;
		typedef typename baseclass::value_compare						value_compare;
		typedef typename baseclass::key_compare							key_compare;

		typedef trans_iterator<rbtree_impl, typename baseclass::iterator, false>		iterator;
		typedef trans_iterator<rbtree_impl, typename baseclass::const_iterator, true>	const_iterator;

		typedef std::reverse_iterator<iterator>							reverse_iterator;
		typedef std::reverse_iterator<const_iterator>					const_reverse_iterator;

		typedef typename baseclass::node_traits							node_traits;
		typedef typename baseclass::node                                node;
		typedef typename baseclass::node_ptr                            node_ptr;
		typedef typename baseclass::const_node_ptr                      const_node_ptr;
		typedef typename baseclass::node_algorithms                     node_algorithms;
		
		static const bool constant_time_size = baseclass::constant_time_size;
		static const bool stateful_value_traits = baseclass::stateful_value_traits;
		
	/// @cond
	private:
		//non-assignable
		rbtree_impl(const rbtree_impl&);
		rbtree_impl operator =(const rbtree_impl&);
		
		// the container's lock.  insert, swap, clear, and commit processing must use
		// a scoped(exclusive) lock, but all others can use a shared lock.
		upgradable_mutex_type _lock;
		
		// mutex & condition used for row-level locking.  This only needs to be acquired
		// when seeking to acquire/release a row level lock, and only for the duration of
		// the internal operation, so they are exclusively internal
		mutex_type            _row_level_lock;
		condition_type        _row_level_lock_released;
		
		// _ver_num is used to determine when iterators might have gone invalid after a cond_wait.
		// when iterators are created, they are stamped with the _ver_num on the container at that
		// time.  Thereafter, the iterators can compare their value to the containers current value
		// to determine if they might be invalid.  For methods of trans_map which block waiting on
		// a row lock, this is used to help optimize out the need to refresh the iterators after
		// re-acquiring the mutex.
		uint64_t _ver_num;

		
		allocator_type        _allocator;
		
		// The following is in support of the transaction system.
		typename boost::interprocess::basic_string<char, typename std::char_traits<char>, 
			typename allocator_type::template rebind<char>::other>  _container_name;
		
	protected:
		typedef fake_copy_constructor<boost::intrusive::rbtree_impl<Config> > pending_change_map_t;
		
		// Support for MVCC:
		// Return the pending node (newVal) which corresponds to the value at iterator i.
		// This method is only called when a transaction rereads an entry in the rbtree
		// which it already has locked for Update_op.  Otherwise, there is never a need
		// for this overhead.
		inline pointer pendingUpdate( Transaction &trans, typename baseclass::const_iterator location ) {
			typename baseclass::iterator i = 
				trans.getContainerSpecificData<pending_change_map_t>(this)->find(*location, baseclass::prot_comp());
			return &(*i);
		}
		
		// Record the fact that there is a update of newValue which is to replace
		// the current value at currVal.
		inline reference addPendingUpdate( Transaction &trans, reference newVal )
		{
			// The transaction retains memory management responsibility for changes.
			pending_change_map_t *changes = trans.getContainerSpecificData<pending_change_map_t>(this);
			std::pair<typename baseclass::iterator,bool> result = 
				changes->insert_unique(newVal);
			if (result.second==false) {
				*result.first = newVal;
			}
			return *result.first;
		}
		
		template <class ManagedRegionType, class MapType> friend class stldb::container_proxy;
		
		// friendship is used to allow these to directly access methods of rbtree
		friend struct detail::assoc_transactional_operation<rbtree_impl>;
		friend struct detail::rbtree_insert_operation<rbtree_impl>;
		friend struct detail::rbtree_update_operation<rbtree_impl>;
		friend struct detail::rbtree_delete_operation<rbtree_impl>;
		friend struct detail::rbtree_deleted_insert_operation<rbtree_impl>;
		friend struct detail::rbtree_lock_operation<rbtree_impl>;
		friend struct detail::rbtree_clear_operation<rbtree_impl>;
		friend struct detail::rbtree_swap_operation<rbtree_impl>;
		
		friend class trans_iterator<rbtree_impl, typename baseclass::iterator, false>;
		friend class trans_iterator<rbtree_impl, typename baseclass::const_iterator, true>;
		
		// convenience, henceforth
		typedef struct detail::rbtree_insert_operation<rbtree_impl>				insert_op;
		typedef struct detail::rbtree_update_operation<rbtree_impl>				update_op;
		typedef struct detail::rbtree_delete_operation<rbtree_impl>				delete_op;
		typedef struct detail::rbtree_deleted_insert_operation<rbtree_impl>		deleted_insert_op;
		typedef struct detail::rbtree_lock_operation<rbtree_impl>				lock_op;
		typedef struct detail::rbtree_clear_operation<rbtree_impl>				clear_op;
		typedef struct detail::rbtree_swap_operation<rbtree_impl>				swap_op;
	/// @endcond
		
	public:
		
		typedef typename baseclass::insert_commit_data insert_commit_data;
		
		// Returns the lock which guards this map, so that the caller can use standard locking
		// conventions with the map.
		upgradable_mutex_type& mutex() { return _lock; }
		
		// Returns the condition variable used to signal when row-level locks are released.
		// (Called in the course of commit by Transaction class.)
		condition_type& condition() { return _row_level_lock_released; }
		
		// returns the allocator which used for erase_and_dispose and recovery operations.
		allocator_type get_allocator() const { return _allocator; }
		
		// Returns the name of this container, as it is known by within the database
		const char *get_name() { return _container_name.c_str(); }

		// mark that a node has been erased from the container.  This causes all iterators
		// created prior to this call to return false from their valid() method.
		void node_erased() {
			_ver_num++;
		}
		
		//! <b>Effects</b>: Constructs an empty tree. 
		//!   
		//! <b>Complexity</b>: Constant. 
		//! 
		//! <b>Throws</b>: If value_traits::node_traits::node
		//!   constructor throws (this does not happen with predefined Boost.Intrusive hooks)
		//!   or the copy constructor of the value_compare object throws. Basic guarantee.
		rbtree_impl( const char *container_name
					, const value_compare &cmp = value_compare()
					, const value_traits &v_traits = value_traits()
					, const allocator_type &alloc = allocator_type() ) 
		:  boost::intrusive::rbtree_impl<Config>(cmp, v_traits)
		, _allocator(alloc)
		, _container_name( container_name, typename allocator_type::template rebind<char>::other(alloc) )
		{ }

		
/*		//! <b>Effects</b>: Constructs an tree which contains a copy of the elements in rarg. 
		//!   
		//! <b>Complexity</b>: Linear in N.
		//! 
		//! <b>Throws</b>: If value_traits::node_traits::node
		//!   constructor throws (this does not happen with predefined Boost.Intrusive hooks)
		//!   or the copy constructor of the value_compare object throws. Basic guarantee.
		//noncopyable
		rbtree_impl(const rbtree_impl &rarg)
		: boost::intrusive::rbtree_impl<Config>( rarg.value_compare, rarg.value_traits )
		, _allocator(rarg.alloc)
		{ 
			baseclass &rbase = rarg;
			typename baseclass::iterator i( rbase.begin() );
			typename baseclass::iterator iend( this->end() );
			while (i != rbase.end()) {
				value_type *node = _allocator.allocate(1);
				new (node) value_type(*i);
				this->insert_unique(*node, iend);
				++i;
			}
		}
*/
		
		//! <b>Requires</b>: Dereferencing iterator must yield an lvalue of type value_type.
		//!   cmp must be a comparison function that induces a strict weak ordering.
		//!
		//! <b>Effects</b>: Constructs an empty tree and inserts elements from
		//!   [b, e).
		//!
		//! <b>Complexity</b>: Linear in N if [b, e) is already sorted using
		//!   comp and otherwise N * log N, where N is the distance between first and last.
		//! 
		//! <b>Throws</b>: If value_traits::node_traits::node
		//!   constructor throws (this does not happen with predefined Boost.Intrusive hooks)
		//!   or the copy constructor/operator() of the value_compare object throws. Basic guarantee.
		template<class Iterator>
		rbtree_impl( bool unique, Iterator b, Iterator e
					, const char *container_name
					, const value_compare &cmp     = value_compare()
					, const value_traits &v_traits = value_traits()
					, const allocator_type &alloc = allocator_type() )
		: boost::intrusive::rbtree_impl<Config>(unique, b, e, cmp, v_traits)
		, _allocator(alloc)
		, _container_name( container_name, typename allocator_type::template rebind<char>::other(alloc) )
		{ }
		
		//! <b>Effects</b>: Detaches all elements from this. The objects in the set 
		//!   are not deleted (i.e. no destructors are called), but the nodes according to 
		//!   the value_traits template parameter are reinitialized and thus can be reused. 
		//! 
		//! <b>Complexity</b>: Linear to elements contained in *this. 
		//! 
		//! <b>Throws</b>: Nothing.
		~rbtree_impl() 
		{}
		
		//! <b>Effects</b>: Returns an iterator pointing to the beginning of the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator begin()
		{  return iterator(baseclass::begin(), this); }

		//! <b>Effects</b>: Returns a const_iterator pointing to the beginning of the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator begin() const
		{  return cbegin();   }
		
		//! <b>Effects</b>: Returns a const_iterator pointing to the beginning of the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator cbegin() const
		{  return const_iterator(baseclass::cbegin(), this);   }
		
		//! <b>Effects</b>: Returns an iterator pointing to the end of the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator end()
		{  return iterator(baseclass::end(), this);  }
		
		//! <b>Effects</b>: Returns a const_iterator pointing to the end of the tree.
		//!
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator end() const
		{  return cend();  }
		
		//! <b>Effects</b>: Returns a const_iterator pointing to the end of the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator cend() const
		{  return const_iterator(baseclass::cend(), this);  }
		
		//! <b>Effects</b>: Returns a reverse_iterator pointing to the beginning of the
		//!    reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		reverse_iterator rbegin()
		{  return reverse_iterator(end());  }
		
		//! <b>Effects</b>: Returns a const_reverse_iterator pointing to the beginning
		//!    of the reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_reverse_iterator rbegin() const
		{  return const_reverse_iterator(end());  }
		
		//! <b>Effects</b>: Returns a const_reverse_iterator pointing to the beginning
		//!    of the reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_reverse_iterator crbegin() const
		{  return const_reverse_iterator(end());  }
		
		//! <b>Effects</b>: Returns a reverse_iterator pointing to the end
		//!    of the reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		reverse_iterator rend()
		{  return reverse_iterator(begin());   }
		
		//! <b>Effects</b>: Returns a const_reverse_iterator pointing to the end
		//!    of the reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_reverse_iterator rend() const
		{  return const_reverse_iterator(begin());   }
		
		//! <b>Effects</b>: Returns a const_reverse_iterator pointing to the end
		//!    of the reversed tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_reverse_iterator crend() const
		{  return const_reverse_iterator(begin());   }
		
		//! <b>Precondition</b>: end_iterator must be a valid end iterator
		//!   of rbtree.
		//! 
		//! <b>Effects</b>: Returns a const reference to the rbtree associated to the end iterator
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Constant.
		static rbtree_impl &container_from_end_iterator(iterator end_iterator)
		{  return baseclass::container_from_iterator(end_iterator.container_end());   }
		
		//! <b>Precondition</b>: end_iterator must be a valid end const_iterator
		//!   of rbtree.
		//! 
		//! <b>Effects</b>: Returns a const reference to the rbtree associated to the iterator
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Constant.
		static const rbtree_impl &container_from_end_iterator(const_iterator end_iterator)
		{  return baseclass::container_from_iterator(end_iterator.container_end());   }
		
		//! <b>Precondition</b>: it must be a valid iterator
		//!   of rbtree.
		//! 
		//! <b>Effects</b>: Returns a const reference to the tree associated to the iterator
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		static rbtree_impl &container_from_iterator(iterator it)
		{  return baseclass::container_from_iterator(it.base());   }
		
		//! <b>Precondition</b>: it must be a valid end const_iterator
		//!   of rbtree.
		//! 
		//! <b>Effects</b>: Returns a const reference to the tree associated to the end iterator
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		static const rbtree_impl &container_from_iterator(const_iterator it)
		{  return baseclass::container_from_iterator(it.base());   }
		
		//! <b>Effects</b>: Returns the value_compare object used by the tree.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: If value_compare copy-constructor throws.
		value_compare value_comp() const
		{  return baseclass::value_comp();   }
		
		//! <b>Effects</b>: Returns true if the container is empty.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		bool empty() const
		{  return baseclass::empty();
			// TODO - make it report committed contents, and make transactional variation 
		}
		
		//! <b>Effects</b>: Returns the number of elements stored in the tree.
		//! 
		//! <b>Complexity</b>: Linear to elements contained in *this
		//!   if constant-time size option is disabled. Constant time otherwise.
		//! 
		//! <b>Throws</b>: Nothing.
		size_type size() const
		{	return baseclass::size(); 
			// TODO - make it report current committed size, and make variant which is transactional	
		}
		
		//! <b>Effects</b>: Swaps the contents of two rbtrees.
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: If the comparison functor's swap call throws.
		void swap(rbtree_impl& other)
		{	baseclass::swap(other);	
			// TODO - make transactional 
		}
		
		//! <b>Requires</b>: value must be an lvalue
		//! 
		//! <b>Effects</b>: Inserts value into the tree before the upper bound.
		//! 
		//! <b>Complexity</b>: Average complexity for insert element is at
		//!   most logarithmic.
		//! 
		//! <b>Throws</b>: If the internal value_compare ordering function throws. Strong guarantee.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		iterator insert_equal(reference value, Transaction &trans)
		{
			stldb::timer t("rbtree_impl::insert_equal(value,trans)");
			value.lock(trans, Insert_op);
			iterator i( baseclass::insert_equal(value) );
			trans.insert_work_in_progress(this, new insert_op(*this, baseclass::get_real_value_traits().to_node_ptr(value)) );
			return i;
		}
		
		//! <b>Requires</b>: value must be an lvalue, and "hint" must be
		//!   a valid iterator.
		//! 
		//! <b>Effects</b>: Inserts x into the tree, using "hint" as a hint to
		//!   where it will be inserted. If "hint" is the upper_bound
		//!   the insertion takes constant time (two comparisons in the worst case)
		//! 
		//! <b>Complexity</b>: Logarithmic in general, but it is amortized
		//!   constant time if t is inserted immediately before hint.
		//! 
		//! <b>Throws</b>: If the internal value_compare ordering function throws. Strong guarantee.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		iterator insert_equal(const_iterator hint, reference value, Transaction &trans)
		{
			stldb::timer t("rbtree_impl::insert_equal(hint,value,trans)");
			value.lock(trans, Insert_op);
			iterator i( baseclass::insert_equal(hint,value) );
			trans.insert_work_in_progress(this, new insert_op(*this, baseclass::get_real_value_traits().to_node_ptr(value)));
			return i;
		}
		
		//! <b>Requires</b>: Dereferencing iterator must yield an lvalue 
		//!   of type value_type.
		//! 
		//! <b>Effects</b>: Inserts a each element of a range into the tree
		//!   before the upper bound of the key of each element.
		//! 
		//! <b>Complexity</b>: Insert range is in general O(N * log(N)), where N is the
		//!   size of the range. However, it is linear in N if the range is already sorted
		//!   by value_comp().
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		template<class Iterator>
		void insert_equal(Iterator b, Iterator e, Transaction &trans)
		{
			iterator iend(this->end());
			for (; b != e; ++b) {
				this->insert_equal(iend, *b, trans);
			}
		}
		
		//! <b>Requires</b>: value must be an lvalue
		//! 
		//! <b>Effects</b>: Inserts value into the tree if the value
		//!   is not already present.
		//! 
		//! <b>Complexity</b>: Average complexity for insert element is at
		//!   most logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		std::pair<iterator, bool> insert_unique(reference value, Transaction &trans)
		{
			stldb::timer t("rbtree_impl::insert_unique(value,trans)");
			insert_commit_data commit_data;
			std::pair<iterator, bool> ret = insert_unique_check(value, baseclass::prot_comp(), commit_data, trans);
			if(!ret.second)
				return ret;
			return std::pair<iterator, bool> (insert_unique_commit(value, commit_data, trans), true);
		}
		
		//! <b>Requires</b>: value must be an lvalue, and "hint" must be
		//!   a valid iterator
		//! 
		//! <b>Effects</b>: Tries to insert x into the tree, using "hint" as a hint
		//!   to where it will be inserted.
		//! 
		//! <b>Complexity</b>: Logarithmic in general, but it is amortized
		//!   constant time (two comparisons in the worst case)
		//!   if t is inserted immediately before hint.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		iterator insert_unique(const_iterator hint, reference value, Transaction &trans)
		{
			insert_commit_data commit_data;
			std::pair<iterator, bool> ret = insert_unique_check(hint, value, baseclass::prot_comp(), commit_data, trans);
			if(!ret.second)
				return ret;
			return std::pair<iterator, bool> (insert_unique_commit(value, commit_data, trans), true);
		}
		
		//! <b>Requires</b>: Dereferencing iterator must yield an lvalue 
		//!   of type value_type.
		//! 
		//! <b>Effects</b>: Tries to insert each element of a range into the tree.
		//! 
		//! <b>Complexity</b>: Insert range is in general O(N * log(N)), where N is the 
		//!   size of the range. However, it is linear in N if the range is already sorted 
		//!   by value_comp().
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Does not affect the validity of iterators and references.
		//!   No copy-constructors are called.
		template<class Iterator>
		void insert_unique(Iterator b, Iterator e)
		{
			if(this->empty()){
				iterator iend(this->end());
				for (; b != e; ++b)
					this->insert_unique(iend, *b);
			}
			else{
				for (; b != e; ++b)
					this->insert_unique(*b);
			}
		}
		
		
	public:
		//! <b>Requires</b>: key_value_comp must be a comparison function that induces 
		//!   the same strict weak ordering as value_compare. The difference is that
		//!   key_value_comp compares an arbitrary key with the contained values.
		//! 
		//! <b>Effects</b>: Checks if a value can be inserted in the container, using
		//!   a user provided key instead of the value itself.
		//!
		//! <b>Returns</b>: If there is an equivalent value
		//!   returns a pair containing an iterator to the already present value
		//!   and false. If the value can be inserted returns true in the returned
		//!   pair boolean and fills "commit_data" that is meant to be used with
		//!   the "insert_commit" function.
		//! 
		//! <b>Complexity</b>: Average complexity is at most logarithmic.
		//!
		//! <b>Throws</b>: If the key_value_comp ordering function throws. Strong guarantee.
		//! 
		//! <b>Notes</b>: This function is used to improve performance when constructing
		//!   a value_type is expensive: if there is an equivalent value
		//!   the constructed object must be discarded. Many times, the part of the
		//!   node that is used to impose the order is much cheaper to construct
		//!   than the value_type and this function offers the possibility to use that 
		//!   part to check if the insertion will be successful.
		//!
		//!   If the check is successful, the user can construct the value_type and use
		//!   "insert_commit" to insert the object in constant-time. This gives a total
		//!   logarithmic complexity to the insertion: check(O(log(N)) + commit(O(1)).
		//!
		//!   "commit_data" remains valid for a subsequent "insert_commit" only if no more
		//!   objects are inserted or erased from the container.
		template<class KeyType, class KeyValueCompare>
		std::pair<iterator, bool> insert_unique_check
		(const KeyType &key, KeyValueCompare key_value_comp, insert_commit_data &commit_data, Transaction &trans)
		{
			typedef typename detail::insert_key_nodeptr_comp<KeyType,KeyValueCompare, baseclass> TransKeyValueCompare;
			TransKeyValueCompare comp(key_value_comp, key, trans.getLockId(), this);
			
			std::pair<typename baseclass::iterator, bool> ret = this->baseclass::template insert_unique_check
				(key, comp, commit_data);
			
			return std::pair<iterator,bool>(iterator(ret.first, this, trans), ret.second);
		}
		
		//! <b>Requires</b>: key_value_comp must be a comparison function that induces 
		//!   the same strict weak ordering as value_compare. The difference is that
		//!   key_value_comp compares an arbitrary key with the contained values.
		//! 
		//! <b>Effects</b>: Checks if a value can be inserted in the container, using
		//!   a user provided key instead of the value itself, using "hint" 
		//!   as a hint to where it will be inserted.
		//!
		//! <b>Returns</b>: If there is an equivalent value
		//!   returns a pair containing an iterator to the already present value
		//!   and false. If the value can be inserted returns true in the returned
		//!   pair boolean and fills "commit_data" that is meant to be used with
		//!   the "insert_commit" function.
		//! 
		//! <b>Complexity</b>: Logarithmic in general, but it's amortized
		//!   constant time if t is inserted immediately before hint.
		//!
		//! <b>Throws</b>: If the key_value_comp ordering function throws. Strong guarantee.
		//! 
		//! <b>Notes</b>: This function is used to improve performance when constructing
		//!   a value_type is expensive: if there is an equivalent value
		//!   the constructed object must be discarded. Many times, the part of the
		//!   constructing that is used to impose the order is much cheaper to construct
		//!   than the value_type and this function offers the possibility to use that key 
		//!   to check if the insertion will be successful.
		//!
		//!   If the check is successful, the user can construct the value_type and use
		//!   "insert_commit" to insert the object in constant-time. This can give a total
		//!   constant-time complexity to the insertion: check(O(1)) + commit(O(1)).
		//!   
		//!   "commit_data" remains valid for a subsequent "insert_commit" only if no more
		//!   objects are inserted or erased from the container.
		template<class KeyType, class KeyValueCompare>
		std::pair<iterator, bool> insert_unique_check
		(const_iterator hint, const KeyType &key
		 ,KeyValueCompare key_value_comp, insert_commit_data &commit_data, Transaction &trans)
		{
			detail::insert_key_nodeptr_comp<KeyType, KeyValueCompare, rbtree_impl> comp(key_value_comp, key, trans.getLockId(), this);
			std::pair<node_ptr, bool> ret = (node_algorithms::insert_unique_check
											 (node_ptr(&baseclass::prot_header_node()), hint.pointed_node(), key, comp, commit_data));
			return std::pair<iterator, bool>(iterator(ret.first, this), ret.second);
		}
		
		//! <b>Requires</b>: value must be an lvalue of type value_type. commit_data
		//!   must have been obtained from a previous call to "insert_check".
		//!   No objects should have been inserted or erased from the container between
		//!   the "insert_check" that filled "commit_data" and the call to "insert_commit".
		//! 
		//! <b>Effects</b>: Inserts the value in the avl_set using the information obtained
		//!   from the "commit_data" that a previous "insert_check" filled.
		//!
		//! <b>Returns</b>: An iterator to the newly inserted object.
		//! 
		//! <b>Complexity</b>: Constant time.
		//!
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Notes</b>: This function has only sense if a "insert_check" has been
		//!   previously executed to fill "commit_data". No value should be inserted or
		//!   erased between the "insert_check" and "insert_commit" calls.
		iterator insert_unique_commit(reference value, const insert_commit_data &commit_data, Transaction &trans)
		{
			value.lock(trans, Insert_op);
			iterator i( baseclass::insert_unique_commit(value, commit_data), this, trans );
			trans.insert_work_in_progress(this, new insert_op(*this, value));
			return i;
		}
		
		//! <b>Requires</b>: value must be an lvalue, "pos" must be
		//!   a valid iterator (or end) and must be the succesor of value
		//!   once inserted according to the predicate
		//!
		//! <b>Effects</b>: Inserts x into the tree before "pos".
		//! 
		//! <b>Complexity</b>: Constant time.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This function does not check preconditions so if "pos" is not
		//! the successor of "value" tree ordering invariant will be broken.
		//! This is a low-level function to be used only for performance reasons
		//! by advanced users.
		iterator insert_before(const_iterator pos, reference value, Transaction &trans)
		{
			value.lock(trans, Insert_op);
			node_ptr to_insert(baseclass::get_real_value_traits().to_node_ptr(value));
			iterator i( baseclass::insert_before(pos, value) );
			trans.insert_work_in_progress(this, new insert_op(*this, to_insert));
			return i;
		}
		
		//! <b>Requires</b>: value must be an lvalue, and it must be no less
		//!   than the greatest inserted key
		//!
		//! <b>Effects</b>: Inserts x into the tree in the last position.
		//! 
		//! <b>Complexity</b>: Constant time.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This function does not check preconditions so if value is
		//!   less than the greatest inserted key tree ordering invariant will be broken.
		//!   This function is slightly more efficient than using "insert_before".
		//!   This is a low-level function to be used only for performance reasons
		//!   by advanced users.
		void push_back(reference value, Transaction &trans)
		{
			value.lock(trans, Insert_op);
			node_ptr to_insert(baseclass::get_real_value_traits().to_node_ptr(value));
			baseclass::push_back(value);
			trans.insert_work_in_progress(this, new insert_op(*this, to_insert));
		}
		
		//! <b>Requires</b>: value must be an lvalue, and it must be no greater
		//!   than the minimum inserted key
		//!
		//! <b>Effects</b>: Inserts x into the tree in the first position.
		//! 
		//! <b>Complexity</b>: Constant time.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This function does not check preconditions so if value is
		//!   greater than the minimum inserted key tree ordering invariant will be broken.
		//!   This function is slightly more efficient than using "insert_before".
		//!   This is a low-level function to be used only for performance reasons
		//!   by advanced users.
		void push_front(reference value, Transaction &trans)
		{
			value.lock(trans, Insert_op);
			node_ptr to_insert(baseclass::get_real_value_traits().to_node_ptr(value));
			baseclass::push_front(value);
			trans.insert_work_in_progress(this, new insert_op(*this, to_insert));
		}
		
		//! <b>Effects</b>: Erases the element pointed to by pos.  The element is
		//! initially marked as deleted, and actually removed from the rbtree only
		//! upon commit.
		//! 
		//! <b>Complexity</b>: Average complexity for erase element is constant time. 
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
		iterator erase(const_iterator i, Transaction &trans)
		{
			const_iterator ret(i);
			++ret;
			private_erase(i, trans);
			return ret.unconst();
		}
		
		//! <b>Effects</b>: Erases the range pointed to by b end e. 
		//! 
		//! <b>Complexity</b>: Average complexity for erase range is at most 
		//!   O(log(size() + N)), where N is the number of elements in the range.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
		iterator erase(const_iterator b, const_iterator e, Transaction &trans)
		{  size_type n;   return private_erase(b, e, n, trans);   }
		
		//! <b>Effects</b>: Erases all the elements with the given value.
		//! 
		//! <b>Returns</b>: The number of erased elements.
		//! 
		//! <b>Complexity</b>: O(log(size() + N).
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
		size_type erase(const_reference value, Transaction &trans)
		{  return this->erase(value, baseclass::prot_comp(), trans);   }
		
		//! <b>Effects</b>: Erases all the elements with the given key.
		//!   according to the comparison functor "comp".
		//!
		//! <b>Returns</b>: The number of erased elements.
		//! 
		//! <b>Complexity</b>: O(log(size() + N).
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
		template<class KeyType, class KeyValueCompare>
		size_type erase(const KeyType& key, KeyValueCompare comp, Transaction &trans
						/// @cond
						, typename bidetail::enable_if_c<!bidetail::is_convertible<KeyValueCompare, const_iterator>::value >::type * = 0
						/// @endcond
						)
		{
			std::pair<iterator,iterator> p = this->equal_range(key, comp);
			size_type n;
			private_erase(p.first, p.second, n, trans);
			return n;
		}

		//! <b>Effects</b>: Erases the element pointed to by pos.  The erase
		//! occurs initially by marking the entry erased within the context of
		//! the indicated transaction.  When (and if) the erase is committed,
		//! the entry is at that time, truly removed from the rbtree, and is
		//! disposed of using the allocator.
		//! 
		//! <b>Complexity</b>: Average complexity for erase element is constant time. 
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators 
		//!    to the erased elements.
		iterator erase_and_dispose(const_iterator i, Transaction &trans )
		{
			iterator ret(i.base().unconst(), this, trans );
			private_erase(ret, trans);
			++ret;
			return ret;
		}
		
#if !defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		iterator erase_and_dispose(iterator i, Transaction &trans)
		{  const_iterator const_i( typename baseclass::const_iterator(i.base()), this, trans );
			return this->erase_and_dispose(const_i, trans );
		}
#endif
		
		//! <b>Requires</b>: Disposer::operator()(pointer) shouldn't throw.
		//!
		//! <b>Effects</b>: Erases all the elements with the given value.
		//!   Disposer::operator()(pointer) is called for the removed elements.
		//! 
		//! <b>Returns</b>: The number of erased elements.
		//! 
		//! <b>Complexity</b>: O(log(size() + N).
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
		template<class Disposer>
		size_type erase_and_dispose(const_reference value, Transaction &trans)
		{
			std::pair<iterator,iterator> p = this->equal_range(value);
			size_type n;
			private_erase(p.first, p.second, n, trans);
			return n;
		}
		
		//! <b>Requires</b>: Disposer::operator()(pointer) shouldn't throw.
		//!
		//! <b>Effects</b>: Erases the range pointed to by b end e.
		//!   Disposer::operator()(pointer) is called for the removed elements.
		//! 
		//! <b>Complexity</b>: Average complexity for erase range is at most 
		//!   O(log(size() + N)), where N is the number of elements in the range.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators
		//!    to the erased elements.
		template<class Disposer>
		iterator erase_and_dispose(const_iterator b, const_iterator e, Transaction &trans)
		{  size_type n;   return private_erase(b, e, n, trans);   }
		
		//! <b>Requires</b>: Disposer::operator()(pointer) shouldn't throw.
		//!
		//! <b>Effects</b>: Erases all the elements with the given key.
		//!   according to the comparison functor "comp".
		//!   Disposer::operator()(pointer) is called for the removed elements.
		//!
		//! <b>Returns</b>: The number of erased elements.
		//! 
		//! <b>Complexity</b>: O(log(size() + N).
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators
		//!    to the erased elements.
		template<class KeyType, class KeyValueCompare, class Disposer>
		size_type erase_and_dispose(const KeyType& key, KeyValueCompare comp, Transaction &trans
									/// @cond
									, typename bidetail::enable_if_c<!bidetail::is_convertible<KeyValueCompare, const_iterator>::value >::type * = 0
									/// @endcond
									)
		{
			std::pair<iterator,iterator> p = this->equal_range(key, comp);
			size_type n;
			private_erase(p.first, p.second, n, trans);
			return n;
		}
		
		//! <b>Effects</b>: Erases all of the elements. 
		//! 
		//! <b>Complexity</b>: Linear to the number of elements on the container.
		//!   if it's a safe-mode or auto-unlink value_type. Constant time otherwise.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. No destructors are called.
//TODO - this version of clear may not be possible for stldb::rbtree.
		void clear(Transaction &trans)
		{
			if(baseclass::safemode_or_autounlink){
				this->clear_and_dispose(bidetail::null_disposer());
			}
			else{
				node_algorithms::init_header(&baseclass::prot_header_node());
				this->priv_size_traits().set_size(0);
			}
		}
		
		//! <b>Effects</b>: Erases all of the elements calling allocator.delete(p) for
		//!   each node to be erased.
		//! <b>Complexity</b>: Average complexity for is at most O(log(size() + N)),
		//!   where N is the number of elements in the container.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: Invalidates the iterators (but not the references)
		//!    to the erased elements. Calls N times to disposer functor.
		template<class Disposer>
		void clear_and_dispose(Transaction &trans)
		{
			node_algorithms::clear_and_dispose(node_ptr(&baseclass::prot_header_node())
											   , bidetail::node_disposer<Disposer, rbtree_impl>(trans, this));
			node_algorithms::init_header(&baseclass::prot_header_node());
			this->priv_size_traits().set_size(0);
		}
		
		//! <b>Effects</b>: Returns the number of contained elements with the given value
		//! 
		//! <b>Complexity</b>: Logarithmic to the number of elements contained plus lineal
		//!   to number of objects with the given value.
		//! 
		//! <b>Throws</b>: Nothing.
		size_type count(const_reference value) const
		{  return this->count(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Returns the number of contained elements with the given key
		//! 
		//! <b>Complexity</b>: Logarithmic to the number of elements contained plus lineal
		//!   to number of objects with the given key.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		size_type count(const KeyType &key, KeyValueCompare comp) const
		{
			std::pair<const_iterator, const_iterator> ret = this->equal_range(key, comp);
			return std::distance(ret.first, ret.second);
		}
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is not less than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator lower_bound(const_reference value)
		{  return this->lower_bound(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is not less than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator lower_bound(const_reference value) const
		{  return this->lower_bound(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is not less than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		iterator lower_bound(const KeyType &key, KeyValueCompare comp)
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return iterator(node_algorithms::lower_bound
							(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}
		
		//! <b>Effects</b>: Returns a const iterator to the first element whose
		//!   key is not less than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		const_iterator lower_bound(const KeyType &key, KeyValueCompare comp) const
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return const_iterator(node_algorithms::lower_bound
								  (const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is greater than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator upper_bound(const_reference value)
		{  return this->upper_bound(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is greater than k according to comp or end() if that element
		//!   does not exist.
		//!
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		iterator upper_bound(const KeyType &key, KeyValueCompare comp)
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return iterator(node_algorithms::upper_bound
							(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is greater than k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator upper_bound(const_reference value) const
		{  return this->upper_bound(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Returns an iterator to the first element whose
		//!   key is greater than k according to comp or end() if that element
		//!   does not exist.
		//!
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		const_iterator upper_bound(const KeyType &key, KeyValueCompare comp) const
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return const_iterator(node_algorithms::upper_bound
								  (const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}
		
		//! <b>Effects</b>: Finds an iterator to the first element whose key is 
		//!   k or end() if that element does not exist.
		//!
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator find(const_reference value)
		{  return this->find(value, baseclass::prot_comp()); }
		
		//! <b>Effects</b>: Finds an iterator to the first element whose key is 
		//!   k or end() if that element does not exist.
		//!
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		iterator find(const KeyType &key, KeyValueCompare comp)
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return iterator
			(node_algorithms::find(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}

		//! <b>Effects</b>: Finds an iterator to the first element whose key is 
		//!   k or end() if that element does not exist.
		//!
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		iterator find(const KeyType &key, KeyValueCompare comp, Transaction &trans)
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			typename baseclass::iterator i(
				node_algorithms::find(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
			if ( i == baseclass::end() ||
				(i->getOperation() == Delete_op && i->getLockId() == trans.getLockId() ) ||
				(i->getOperation() == Insert_op && i->getLockId() != trans.getLockId() ))
				return iterator( baseclass::end(), this, trans );
			else
				return iterator( i, this, trans );
		}

		
		//! <b>Effects</b>: Replaces the entry (i) with newValue.
		//!
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: row_level_lock_contention if the entry (i)
		//! is already locked by another transaction.  row_deleted_exception
		//! if the entry (i) has been erased.
		void update( iterator i, reference newValue, Transaction &trans )
		{
			stldb::timer t1("rbtree_impl::update(iterator,newvalue,trans)");
			boost::interprocess::scoped_lock<mutex_type> holder(_row_level_lock);
			return private_update(i, newValue, trans);
		}
		
		
		//! <b>Effects</b>: Finds a const_iterator to the first element whose key is 
		//!   k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator find(const_reference value) const
		{  return this->find(value, baseclass::prot_comp()); }
		
		//! <b>Effects</b>: Finds a const_iterator to the first element whose key is 
		//!   k or end() if that element does not exist.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		const_iterator find(const KeyType &key, KeyValueCompare comp) const
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			return const_iterator
			(node_algorithms::find(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp), this);
		}
		
		//! <b>Effects</b>: Finds a range containing all elements whose key is k or
		//!   an empty range that indicates the position where those elements would be
		//!   if they there is no elements with key k.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		std::pair<iterator,iterator> equal_range(const_reference value)
		{  return this->equal_range(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Finds a range containing all elements whose key is k or
		//!   an empty range that indicates the position where those elements would be
		//!   if they there is no elements with key k.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		std::pair<iterator,iterator> equal_range(const KeyType &key, KeyValueCompare comp)
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			std::pair<node_ptr, node_ptr> ret
			(node_algorithms::equal_range(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp));
			return std::pair<iterator, iterator>(iterator(ret.first, this), iterator(ret.second, this));
		}
		
		//! <b>Effects</b>: Finds a range containing all elements whose key is k or
		//!   an empty range that indicates the position where those elements would be
		//!   if they there is no elements with key k.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		std::pair<const_iterator, const_iterator>
		equal_range(const_reference value) const
		{  return this->equal_range(value, baseclass::prot_comp());   }
		
		//! <b>Effects</b>: Finds a range containing all elements whose key is k or
		//!   an empty range that indicates the position where those elements would be
		//!   if they there is no elements with key k.
		//! 
		//! <b>Complexity</b>: Logarithmic.
		//! 
		//! <b>Throws</b>: Nothing.
		template<class KeyType, class KeyValueCompare>
		std::pair<const_iterator, const_iterator>
		equal_range(const KeyType &key, KeyValueCompare comp) const
		{
			bidetail::key_nodeptr_comp<KeyValueCompare, rbtree_impl>
			key_node_comp(comp, this);
			std::pair<node_ptr, node_ptr> ret
			(node_algorithms::equal_range(const_node_ptr(&baseclass::prot_header_node()), key, key_node_comp));
			return std::pair<const_iterator, const_iterator>(const_iterator(ret.first, this), const_iterator(ret.second, this));
		}
		
		//! <b>Requires</b>: Disposer::operator()(pointer) shouldn't throw.
		//!   Cloner should yield to nodes equivalent to the original nodes.
		//!
		//! <b>Effects</b>: Erases all the elements from *this
		//!   calling Disposer::operator()(pointer), clones all the 
		//!   elements from src calling Cloner::operator()(const_reference )
		//!   and inserts them on *this. Copies the predicate from the source container.
		//!
		//!   If cloner throws, all cloned elements are unlinked and disposed
		//!   calling Disposer::operator()(pointer).
		//!   
		//! <b>Complexity</b>: Linear to erased plus inserted elements.
		//! 
		//! <b>Throws</b>: If cloner throws or predicate copy assignment throws. Basic guarantee.
		template <class Cloner, class Disposer>
		void clone_from(const rbtree_impl &src, Cloner cloner, Disposer disposer)
		{
			this->clear_and_dispose(disposer);
			if(!src.empty()){
				bidetail::exception_disposer<rbtree_impl, Disposer>
				rollback(*this, disposer);
				node_algorithms::clone
				(const_node_ptr(&src.prot_header_node())
				 ,node_ptr(&this->prot_header_node())
				 ,bidetail::node_cloner<Cloner, rbtree_impl>(cloner, this)
				 ,bidetail::node_disposer<Disposer, rbtree_impl>(disposer, this));
				this->priv_size_traits().set_size(src.priv_size_traits().get_size());
				this->prot_comp() = src.prot_comp();
				rollback.release();
			}
		}
		
		//! <b>Effects</b>: Unlinks the leftmost node from the tree.
		//! 
		//! <b>Complexity</b>: Average complexity is constant time.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Notes</b>: This function breaks the tree and the tree can
		//!   only be used for more unlink_leftmost_without_rebalance calls.
		//!   This function is normally used to achieve a step by step
		//!   controlled destruction of the tree.
		pointer unlink_leftmost_without_rebalance()
		{
			node_ptr to_be_disposed(node_algorithms::unlink_leftmost_without_rebalance
									(node_ptr(&baseclass::prot_header_node())));
			if(!to_be_disposed)
				return 0;
			this->priv_size_traits().decrement();
			if(baseclass::safemode_or_autounlink)//If this is commented does not work with normal_link
				node_algorithms::init(to_be_disposed);
			return baseclass::get_real_value_traits().to_value_ptr(to_be_disposed);
		}
		
		//! <b>Requires</b>: replace_this must be a valid iterator of *this
		//!   and with_this must not be inserted in any tree.
		//! 
		//! <b>Effects</b>: Replaces replace_this in its position in the
		//!   tree with with_this. The tree does not need to be rebalanced.
		//! 
		//! <b>Complexity</b>: Constant. 
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This function will break container ordering invariants if
		//!   with_this is not equivalent to *replace_this according to the
		//!   ordering rules. This function is faster than erasing and inserting
		//!   the node, since no rebalancing or comparison is needed.
		void replace_node(iterator replace_this, reference with_this)
		{
			node_algorithms::replace_node( baseclass::get_real_value_traits().to_node_ptr(*replace_this)
										  , node_ptr(&baseclass::prot_header_node())
										  , baseclass::get_real_value_traits().to_node_ptr(with_this));
		}
		
		//! <b>Requires</b>: value must be an lvalue and shall be in a set of
		//!   appropriate type. Otherwise the behavior is undefined.
		//! 
		//! <b>Effects</b>: Returns: a valid iterator i belonging to the set
		//!   that points to the value
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This static function is available only if the <i>value traits</i>
		//!   is stateless.
		static iterator s_iterator_to(reference value)
		{
			BOOST_STATIC_ASSERT((!stateful_value_traits));
			return iterator (value_traits::to_node_ptr(value), 0);
		}
		
		//! <b>Requires</b>: value must be an lvalue and shall be in a set of
		//!   appropriate type. Otherwise the behavior is undefined.
		//! 
		//! <b>Effects</b>: Returns: a valid const_iterator i belonging to the
		//!   set that points to the value
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Note</b>: This static function is available only if the <i>value traits</i>
		//!   is stateless.
		static const_iterator s_iterator_to(const_reference value) 
		{
			BOOST_STATIC_ASSERT((!stateful_value_traits));
			return const_iterator (value_traits::to_node_ptr(const_cast<reference> (value)), 0);
		}
		
		//! <b>Requires</b>: value must be an lvalue and shall be in a set of
		//!   appropriate type. Otherwise the behavior is undefined.
		//! 
		//! <b>Effects</b>: Returns: a valid iterator i belonging to the set
		//!   that points to the value
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		iterator iterator_to(reference value)
		{  return iterator (value_traits::to_node_ptr(value), this); }
		
		//! <b>Requires</b>: value must be an lvalue and shall be in a set of
		//!   appropriate type. Otherwise the behavior is undefined.
		//! 
		//! <b>Effects</b>: Returns: a valid const_iterator i belonging to the
		//!   set that points to the value
		//! 
		//! <b>Complexity</b>: Constant.
		//! 
		//! <b>Throws</b>: Nothing.
		const_iterator iterator_to(const_reference value) const
		{  return const_iterator (value_traits::to_node_ptr(const_cast<reference> (value)), this); }
		
		//! <b>Requires</b>: value shall not be in a tree.
		//! 
		//! <b>Effects</b>: init_node puts the hook of a value in a well-known default
		//!   state.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Constant time.
		//! 
		//! <b>Note</b>: This function puts the hook in the well-known default state
		//!   used by auto_unlink and safe hooks.
		static void init_node(reference value)
		{ node_algorithms::init(value_traits::to_node_ptr(value)); }
		
		//! <b>Effects</b>: removes "value" from the container.
		//! 
		//! <b>Throws</b>: Nothing.
		//! 
		//! <b>Complexity</b>: Logarithmic time.
		//! 
		//! <b>Note</b>: This static function is only usable with non-constant
		//! time size containers that have stateless comparison functors.
		//!
		//! If the user calls
		//! this function with a constant time size container or stateful comparison
		//! functor a compilation error will be issued.
		static void remove_node(reference value)
		{
			BOOST_STATIC_ASSERT((!constant_time_size));
			node_ptr to_remove(value_traits::to_node_ptr(value));
			node_algorithms::unlink(to_remove);
			if(baseclass::safemode_or_autounlink)
				node_algorithms::init(to_remove);
		}
		
		/// @cond
	private:
		// Complete a unique insert, taking into account the possibility of a pending insert by another trans
		// or a pending delete by this or another trans
		std::pair<iterator, bool> private_insert_unique_commit(reference value, insert_commit_data &commit_data,
															   std::pair<iterator, bool> result, Transaction &trans) {
			// insert might not work, in the case of a duplicate
			if (result.second) {
				trans.insert_work_in_progress(this, new insert_op(*this, result.first) );
				return std::pair<iterator, bool> (insert_unique_commit(value, commit_data), true);
			}
			else {
				// Insert failed, but it might still take if the entry is deleted.
				switch (result.first->getOperation()) {
					case No_op:
					case Lock_op:
					case Update_op:
						// we failed because there was an existing committed value
						break;
					case Insert_op:
						// we failed because another thread has a pending insert,
						if (result.first->getLockId() != trans.getLockId())
							throw row_level_lock_contention(); // nowait.
															   // or because this transaction has a pending insert, already.
						break;
					case Delete_op:
						// we can insert on top of a previously performed erase operation.
						if (result.first->getLockId() == trans.getLockId()) {
							result.first->lock( trans, Update_op );
							result.second = true;
							this->addPendingUpdate(trans, value);
							trans.insert_work_in_progress(this, new update_op														  (*this,trans.getLockId(),Delete_op,result.first, value) );
						}
						else
							throw row_level_lock_contention();
						break;
					default:
						// sign of internal corruption.
						throw recovery_needed();
				}
			}
			return std::make_pair<iterator,bool>(iterator(result.first, this, trans), result.second);
		}
		
		void private_update(iterator i, reference newValue, Transaction &trans )
		{
			stldb::timer t1("rbtree_impl::private_update(iterator,newValue,trans)");

			if ( i->getOperation() != No_op && i->getLockId() != trans.getLockId() )
				throw row_level_lock_contention();
			
			// Either op==No_op or I already have the lock.
			transaction_id_t oldlock= i->getLockId();
			TransactionalOperations oldop = i->getOperation();
			switch ( oldop )
			{
				case Insert_op:
					// we've updated a row we already have a pending insert on.  optimize...
					// no need to create additional transactions ops.  The one for insert can cover this too.
					*i = newValue;
					break;
				case Delete_op:
					// invalid.  Someone has passed an iterator they previously passed to erase.
					throw row_deleted_exception();
				case No_op:
				case Lock_op:
					i->lock( trans, Update_op );
					// deliberate fallthrough! (no break)
				case Update_op:
					// The update is not stored on the row directly.  We store it in a heap-based array on the
					// transaction, keyed off the iterator, which contains the new value_type to be
					// stored in the map at the time of commit.
					this->addPendingUpdate(trans, newValue);
					trans.insert_work_in_progress( this, new update_op
												  (*this, oldlock, oldop, *i, newValue ) 
												  );
					break;
				default:
					// sign of internal corruption.
					throw recovery_needed();
			}
		}
		
		void private_erase(iterator& i, Transaction& trans )
		{
			stldb::timer t1("rbtree_impl::private_erase(iterator,trans)");

			if ( i->getOperation() != No_op && i->getLockId() != trans.getLockId() )
				throw row_level_lock_contention();
			
			// Either op is No_op, or we are already a locker of this row.
			transaction_id_t oldlock = i->getLockId();
			TransactionalOperations oldop = i->getOperation();
			switch (oldop) {
				case Insert_op:
					i->lock(trans, Deleted_Insert_op);
					// Record an operation for the transaction which will indicate that this row has been locked.
					trans.insert_work_in_progress( this, new deleted_insert_op(*this, oldlock, oldop, i) );
					break;
				case No_op:
				case Lock_op:
				case Update_op:
					i->lock(trans, Delete_op);
					// Record an operation for the transaction which will indicate that this row has been locked.
					trans.insert_work_in_progress( this, new delete_op(*this, oldlock, oldop, i) );
					break;
				case Delete_op:
					// called erase_and_dispose() twice during a transaction on the same iterator.
					throw row_deleted_exception();
				default:
					// sign of internal corruption.
					throw recovery_needed();
			}
		};
		
		/// @endcond
	};
	
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	template<class T, class Allocator, class ...Options>
#else
	template<class Config>
#endif
	inline bool operator<
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
	(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
	{  return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());  }
	
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	template<class T, class Allocator, class ...Options>
#else
	template<class Config>
#endif
	bool operator==
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
	(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
	{
		typedef rbtree_impl<Config> tree_type;
		typedef typename tree_type::const_iterator const_iterator;
		
		if(tree_type::constant_time_size && x.size() != y.size()){
			return false;
		}
		const_iterator end1 = x.end();
		const_iterator i1 = x.begin();
		const_iterator i2 = y.begin();
		if(tree_type::constant_time_size){
			while (i1 != end1 && *i1 == *i2) {
				++i1;
				++i2;
			}
			return i1 == end1;
		}
		else{
			const_iterator end2 = y.end();
			while (i1 != end1 && i2 != end2 && *i1 == *i2) {
				++i1;
				++i2;
			}
			return i1 == end1 && i2 == end2;
		}
	}
	
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	template<class T, class Allocator, class ...Options>
#else
	template<class Config>
#endif
	inline bool operator!=
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
	(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
	{  return !(x == y); }
	
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	template<class T, class Allocator, class ...Options>
#else
	template<class Config>
#endif
	inline bool operator>
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
	(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
	(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
	{  return y < x;  }
		
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		template<class T, class Allocator, class ...Options>
#else
		template<class Config>
#endif
		inline bool operator<=
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
		(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
		{  return !(y < x);  }
		
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		template<class T, class Allocator, class ...Options>
#else
		template<class Config>
#endif
		inline bool operator>=
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		(const rbtree_impl<T, Allocator, Options...> &x, const rbtree_impl<T, Allocator, Options...> &y)
#else
		(const rbtree_impl<Config> &x, const rbtree_impl<Config> &y)
#endif
		{  return !(x < y);  }
		
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		template<class T, class Allocator, class ...Options>
#else
		template<class Config>
#endif
		inline void swap
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED)
		(rbtree_impl<T, Allocator, Options...> &x, rbtree_impl<T, Allocator, Options...> &y)
#else
		(rbtree_impl<Config> &x, rbtree_impl<Config> &y)
#endif
		{  x.swap(y);  }

		
		/// @cond
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
		template<class T, class Allocator
						, class O1 = bi::none, class O2 = bi::none
						, class O3 = bi::none, class O4 = bi::none
						, class O5 = bi::none, class O6 = bi::none
				>
#else
		template<class T, class Allocator, class ...Options>
#endif
		struct make_rbtree_opt
		{
			typedef typename bi::pack_options
				< set_defaults<T>,
				  allocator_type<Allocator>,
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
				O1, O2, O3, O4, O5, O6
#else
				Options...
#endif
				>::type packed_options;
			
			typedef typename bidetail::get_value_traits
				<T, typename packed_options::value_traits>::type value_traits;
			
			typedef setopt
				< value_traits
				, typename packed_options::compare
				, typename packed_options::size_type
				, packed_options::constant_time_size
				, typename packed_options::mutex_family
				, typename packed_options::allocator_type
				> type;
		};
		/// @endcond
		
		//! Helper metafunction to define a \c rbtree that yields to the same type when the
		//! same options (either explicitly or implicitly) are used.
#if defined(BOOST_INTRUSIVE_DOXYGEN_INVOKED) || defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
		template<class T, class Allocator, class ...Options>
#else
		template<class T, class Allocator
						, class O1 = bi::none, class O2 = bi::none
						, class O3 = bi::none, class O4 = bi::none
						, class O5 = bi::none, class O6 = bi::none
				>
#endif
		struct make_rbtree
		{
			/// @cond
			typedef rbtree_impl
				< typename make_rbtree_opt<T, Allocator,
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
					O1, O2, O3, O4, O5, O6
#else
					Options...
#endif
					>::type
				> implementation_defined;
			/// @endcond
			
			typedef implementation_defined type;
		};
		
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
		template<class T, class Allocator, class O1, class O2, class O3, class O4, class O5, class O6>
#else
		template<class T, class Allocator, class ...Options>
#endif
		class rbtree
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
		:  public make_rbtree<T, Allocator, O1, O2, O3, O4, O5, O6>::type
#else
		:  public make_rbtree<T, Allocator, Options... >::type
#endif
		{
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
			typedef typename make_rbtree<T, Allocator, O1, O2, O3, O4, O5, O6>::type Base;
#else
			typedef typename make_rbtree<T, Allocator, Options... >::type    Base;
#endif
		public:
			typedef typename Base::value_compare      value_compare;
			typedef typename Base::value_traits       value_traits;
			typedef typename Base::real_value_traits  real_value_traits;
			typedef typename Base::iterator           iterator;
			typedef typename Base::const_iterator     const_iterator;
			typedef typename Base::allocator_type     allocator_type;
			
			//Assert if passed value traits are compatible with the type
			BOOST_STATIC_ASSERT((bidetail::is_same<typename real_value_traits::value_type, T>::value));
			
			rbtree( const char *container_name
				   , const value_compare &cmp = value_compare()
				   , const value_traits &v_traits = value_traits()
				   , const allocator_type &alloc = allocator_type() )
			:  Base(container_name, cmp, v_traits, alloc)
			{}
			
			template<class Iterator>
			rbtree( bool unique, Iterator b, Iterator e
				   , const char *container_name 
				   , const value_compare &cmp = value_compare()
				   , const value_traits &v_traits = value_traits()
				   , const allocator_type &alloc = allocator_type() )
			:  Base(unique, b, e, container_name, cmp, v_traits, alloc)
			{}
			
			static rbtree &container_from_end_iterator(iterator end_iterator)
			{  return static_cast<rbtree &>(Base::container_from_end_iterator(end_iterator));   }
			
			static const rbtree &container_from_end_iterator(const_iterator end_iterator)
			{  return static_cast<const rbtree &>(Base::container_from_end_iterator(end_iterator));   }
			
			static rbtree &container_from_it(iterator it)
			{  return static_cast<rbtree &>(Base::container_from_iterator(it));   }
			
			static const rbtree &container_from_it(const_iterator it)
			{  return static_cast<const rbtree &>(Base::container_from_iterator(it));   }
			
			friend struct detail::rbtree_insert_operation<rbtree>;
			friend struct detail::rbtree_update_operation<rbtree>;
			friend struct detail::rbtree_delete_operation<rbtree>;
			friend struct detail::rbtree_deleted_insert_operation<rbtree>;
			friend struct detail::rbtree_lock_operation<rbtree>;
			friend struct detail::rbtree_clear_operation<rbtree>;
			friend struct detail::rbtree_swap_operation<rbtree>;			
		};
		

		/// @cond
		//!Specialization of container_proxy for rbtree.
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
		template <class ManagedRegionType, class T, class Allocator, class O1, class O2, class O3, class O4, class O5, class O6>
		class container_proxy<ManagedRegionType, rbtree<T,Allocator,O1,O2,O3,O4,O5,O6> >
#else
		template <class ManagedRegionType, class T, class Allocator, class Options...>
		class container_proxy<ManagedRegionType, rbtree<T,Allocator,Options...> >
#endif
			: public container_proxy_base<ManagedRegionType>
		{
		public:
#if !defined(BOOST_INTRUSIVE_VARIADIC_TEMPLATES)
			typedef rbtree<T,Allocator,O1,O2,O3,O4>				container_type;
#else
			typedef rbtree<T,Allocator,Options...>				container_type;
#endif
			typedef container_proxy_base<ManagedRegionType>		base;
			typedef typename container_type::allocator_type     allocator_type;
			typedef typename container_type::value_compare      value_compare;
			typedef typename container_type::value_traits       value_traits;

			container_proxy(const char *name)
				: container_proxy_base<ManagedRegionType>(name)
				, _container(NULL), _db(NULL)
			{ }
			
			virtual ~container_proxy() { }
			
			virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
				typename ManagedRegionType::segment_manager *sm = db.getRegion().get_segment_manager();
				allocator_type alloc( sm );
				_db = &db;  // late initialization
				_container = db.getRegion().template find_or_construct<container_type>
					(base::container_name.c_str())
					(base::container_name.c_str(), value_compare(), value_traits(), alloc);
				return _container;
			}
			
			virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) {
				switch (opcode) {
						// Note: Lock_op is missing from this list because they are never written to logs.
					case Insert_op: {
						detail::rbtree_insert_operation<container_type> op(*_container);
						op.recover(stream, lsn);
						break;
					}
					case Update_op: {
						detail::rbtree_update_operation<container_type> op(*_container);
						op.recover(stream, lsn);
						break;
					}
					case Delete_op: {
						detail::rbtree_delete_operation<container_type> op(*_container);
						op.recover(stream, lsn);
						break;
					}
					case Deleted_Insert_op: {
						detail::rbtree_deleted_insert_operation<container_type> op(*_container);
						op.recover(stream, lsn);
						break;
					}
					case Clear_op: {
						detail::rbtree_clear_operation<container_type> op(*_container);
						op.recover(stream, lsn);
						break;
					}
					case Swap_op: {
						detail::rbtree_swap_operation<container_type> op(*_container);
						typename boost::interprocess::basic_string<char, typename std::char_traits<char>, std::allocator<char> >
							other_container_name;
						stream & other_container_name;
						container_type *other = _db->template getContainer<container_type>(other_container_name.c_str());
						op.recover(*other, lsn);
						break;
					}
					default:
						break;
				}
			}
			void initializeTxn(Transaction &trans)
			{
				// to start a commit or rollback, we need an exclusive lock on the
				// container.
				while (true) {
					try {
						_container->mutex().lock();
						break;
					}
					catch (lock_timeout_exception &ex) { }
				}
			}
			void completeTxn(Transaction &trans)
			{
				// we release our exclusive lock, and also signal any threads
				// waiting on row-level locks that they may now be able to proceed.
				{
					// the mutex must be held before calling notify_all() on the
					// condition variable, because there otherwise is a race condition
					// with the waiting convention where a thread could miss a notify,
					// and end up waiting an extra commit before unlocking.
					scoped_lock<typename container_type::mutex_type>  lock(_container->_row_level_lock);
					// wake up anyone waiting on a row lock
					_container->_row_level_lock_released.notify_all(); 
				}
				_container->mutex().unlock();
			}
			virtual void initializeCommit(Transaction &trans)
			{
				initializeTxn(trans);
			}
			virtual void initializeRollback(Transaction &trans)
			{
				initializeTxn(trans);
			}
			virtual void completeCommit(Transaction &trans)
			{
				completeTxn(trans);
			}
			virtual void completeRollback(Transaction &trans)
			{
				completeTxn(trans);
			}
			virtual void save_checkpoint(Database<ManagedRegionType> &db,
										 checkpoint_ofstream &checkpoint,
										 transaction_id_t last_checkpoint_lsn )
			{
				// TODO - remove this method from proxy interface
			}
			virtual void load_checkpoint(checkpoint_ifstream &checkpoint)
			{
				// TODO - remove this method from proxy interface
			}
			
		private:
			container_type *_container;
			Database<ManagedRegionType> *_db;
		};
		/// @cond

} //namespace stldb 
		
#endif
