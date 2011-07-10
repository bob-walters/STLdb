/*
 *  utilities.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 12/30/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */
#include <stldb/cachetypes.h>
#include <boost/intrusive/detail/ebo_functor_holder.hpp>
#include <boost/intrusive/detail/mpl.hpp>

namespace stldb {
	namespace detail {
		
		namespace bidetail = boost::intrusive::detail;
		
		using bidetail::enable_if_c;
		using bidetail::is_convertible;
		using bidetail::ebo_functor_holder;
		
		//! Node and KeyType comparison functor which takes the transactonal account
		//! of existing nodes into account during comparisons in order to produce the
		//! correct result with insert_unique_check calls in rbtree.
		
		template<class KeyType, class KeyValueCompare, class Container>
		struct insert_key_nodeptr_comp
			:  private ebo_functor_holder<KeyValueCompare>
		{
			typedef typename Container::real_value_traits         real_value_traits;
			typedef typename real_value_traits::node_ptr          node_ptr;
			typedef typename real_value_traits::const_node_ptr    const_node_ptr;
			typedef typename real_value_traits::reference		  reference;
			typedef typename real_value_traits::const_reference	  const_reference;

			typedef ebo_functor_holder<KeyValueCompare>			  base_t;

			insert_key_nodeptr_comp(KeyValueCompare kcomp, const KeyType &key
									, transaction_id_t tid, const Container *cont)
			:  base_t(kcomp), cont_(cont), tid_(tid), key_(key)
			{}

			// Determine the equivalence of 2 entries based on key equality plus
			// current transaction activity.
			bool trans_equal(transaction_id_t tid, const_reference vptr) const
			{
				switch( vptr.getOperation() ) {
					case Delete_op:
					case Deleted_Insert_op:
						if (tid != vptr.getLockId())
							throw row_level_lock_contention(); // nowait
						return false;
					case Insert_op:
						if (tid != vptr.getLockId())
							throw row_level_lock_contention(); // nowait
						return true;
					default:
						return true;
				}
			}
							 
			template<class KeyType2>
			bool operator()( const_reference node, const KeyType2 & key
					, typename enable_if_c
					<!is_convertible<KeyType2, const_node_ptr>::value>::type * = 0) const
			{  
				bool ret = base_t::get()(node, key); 
				if (!ret) 
					return ret;
				return trans_equal(tid_, node);
			}
	
			template<class KeyType2>
			bool operator()( const KeyType2 & key, const_reference node
					, typename enable_if_c
					<!is_convertible<KeyType2, const_node_ptr>::value>::type * = 0) const
			{  
				bool ret = base_t::get()(key, node); 
				if (!ret) 
					return ret;
				return trans_equal(tid_, node);
			}

			// needed for disambiguation
			bool operator()(const_reference node1, const_reference node2) const
			{
				bool ret = base_t::get()( node1, node2 );
				if (!ret)
					return ret;
				if (&key_ == &node1)
					return trans_equal(tid_, node2);
				if (&key_ == &node2)
					return trans_equal(tid_, node1);
				return true;
			}
			
			const Container *cont_;
			transaction_id_t tid_;
			const KeyType &key_;
		};

	} // namespace detail
} // namespace stldb
