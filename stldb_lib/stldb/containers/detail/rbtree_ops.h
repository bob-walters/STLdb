/*
 *  rbtree_ops.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 1/30/11.
 *  Copyright 2011 bobw. All rights reserved.
 *
 */

#ifndef STLDB_DETAIL_RBTREE_OPS_
#define STLDB_DETAIL_RBTREE_OPS_

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
typedef boost::archive::binary_oarchive boost_oarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
typedef boost::archive::text_oarchive boost_oarchive_t;
#endif

// For the serialization of std::pair
#include <boost/serialization/utility.hpp>


namespace stldb {
	namespace detail {
		
		
		template <class rbtree_type>
		struct rbtree_insert_operation : public assoc_transactional_operation<rbtree_type>
		{
			typedef typename rbtree_type::reference      reference;
			typedef typename rbtree_type::pointer        pointer;
			typedef typename rbtree_type::allocator_type allocator_type;
			
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_insert_operation,sizeof(rbtree_insert_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_insert_operation,sizeof(rbtree_insert_operation)>::free(p);
			}
			
			// Constructor used when an insert operation is done
			rbtree_insert_operation(rbtree_type &c, reference entry )
				: assoc_transactional_operation<rbtree_type>(c, Insert_op, 0, No_op)
				, _entry(&entry)
			{ }
	
			// Constructor used during recovery processing
			rbtree_insert_operation( rbtree_type &c)
				: assoc_transactional_operation<rbtree_type>(c)
				, _entry(NULL)
			{ }
			
			virtual void commit(Transaction &t) {
				if (_entry->getOperation() == Insert_op)
					_entry->unlock(t.getLSN());
			}
			
			virtual void rollback(Transaction &t) {
				// inserts which follow a delete are handled as modifies,
				// so on rollback, we always erase.
				typename rbtree_type::baseclass &ref = this->get_container();
				ref.erase(*_entry);
				allocator_type alloc( this->get_container().get_allocator() );
				alloc.destroy( _entry );
				alloc.deallocate( _entry, 1 );
			}
			
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				reference ref = *_entry;
				buffer & ref;
				return 1;
			}
			
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				allocator_type alloc( this->get_container().get_allocator() );
				pointer node = alloc.allocate(1);
				alloc.construct( node );
				stream & (*node);
				node->unlock(lsn);
				typename rbtree_type::baseclass &ref = this->get_container();
				ref.insert_unique(*node);  // ignore errors if already exists.
			}
			
		private:
			pointer  _entry;	 // the entry in c being modified.
		};
		
		template <class rbtree_type>
		struct rbtree_update_operation : public assoc_transactional_operation<rbtree_type>
		{
			typedef typename rbtree_type::reference      reference;
			typedef typename rbtree_type::pointer        pointer;
			typedef typename rbtree_type::allocator_type allocator_type;
			typedef typename rbtree_type::pending_change_map_t pending_change_map_t;
			
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_update_operation,sizeof(rbtree_update_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_update_operation,sizeof(rbtree_update_operation)>::free(p);
			}
			
			rbtree_update_operation( rbtree_type &c, transaction_id_t original_txnid,
								 TransactionalOperations original_op,
								 reference entry, reference new_value )
				: assoc_transactional_operation<rbtree_type>(c, Update_op, original_txnid, original_op)
				, _entry(&entry), _newValue(&new_value)
			{ }
			
			// Constructor used during recovery processing
			rbtree_update_operation( rbtree_type &c )
				: assoc_transactional_operation<rbtree_type>( c )
				, _entry(NULL), _newValue(NULL)
			{ }
			
			virtual void commit(Transaction &t) {
				if (_entry->getOperation() == Update_op) {
					typename rbtree_type::baseclass &ref = this->get_container();
					// newValue must be removed from the pending change map for replace_node to work.
					pending_change_map_t *changes = t.getContainerSpecificData<pending_change_map_t>(& this->get_container());
					changes->erase( changes->iterator_to(*_newValue) );
/*
 TODO - possible bug in replace_node, the replace node is still linked, and fails deallocation.
					ref.replace_node( ref.iterator_to(*_entry), *_newValue);
					BOOST_ASSERT(_newValue->is_linked());
					BOOST_ASSERT(!_entry->is_linked());
 */
					// alternative (less performant)
					ref.erase( ref.iterator_to(*_entry) );
					ref.insert_equal(*_newValue);
					
					// we can now clean up (destroy) _entry.
					allocator_type alloc( this->get_container().get_allocator() );
					alloc.destroy( _entry );
					alloc.deallocate( _entry, 1 );
					_newValue->unlock(t.getLSN());
				}
			}
			virtual void rollback(Transaction &t) {
				_entry->unlock( this->get_prev_txnid(), this->get_prev_op() );
				// _newValue will be deleted when the pending_change_map_t is purged at the end of the txn.
			}
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				reference ref = *_entry;
				buffer & ref;
				return 1;
			}
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				allocator_type alloc( this->get_container().get_allocator() );
				_newValue = alloc.allocate(1);
				alloc.construct( _newValue );
				reference ref = *_newValue;
				stream & ref;
				_newValue->unlock(lsn);

				typename rbtree_type::baseclass &container = this->get_container();
				typename rbtree_type::baseclass::iterator i = container.find(*_newValue);
				// Update is sometimes recorded for an insert on a row which already has a pending delete.
				// so we need to be ready to insert or update
				if (i != container.end() ) {
					_entry = &*i;
					container.replace_node(i, *_newValue);
					alloc.destroy( _entry );
					alloc.deallocate( _entry, 1 );
				}
				else {
					container.insert_unique( *_newValue );
				}
				_newValue->unlock(lsn);
			}
			
		private:
			pointer _entry;	 // the entry in c being modified.
			pointer _newValue; // for pending updates.
		};
		
		template <class rbtree_type>
		struct rbtree_delete_operation : public assoc_transactional_operation<rbtree_type>
		{
			typedef typename rbtree_type::iterator       iterator;
			typedef typename rbtree_type::pointer        pointer;
			typedef typename rbtree_type::reference      reference;
			typedef typename rbtree_type::allocator_type allocator_type;
			
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_delete_operation,sizeof(rbtree_delete_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_delete_operation,sizeof(rbtree_delete_operation)>::free(p);
			}
			
			// Constructor used when operation is performed on a rbtree
			rbtree_delete_operation( rbtree_type &c, transaction_id_t original_txnid,
								 TransactionalOperations original_op,
								 iterator &entry)
				: assoc_transactional_operation<rbtree_type>( c, Delete_op, original_txnid, original_op )
				, _entry( entry )
			{}
			
			// Constructor used during recovery processing
			rbtree_delete_operation( rbtree_type &c )
				: assoc_transactional_operation<rbtree_type>( c )
				, _entry()
			{ }
			
			virtual void commit(Transaction &t) {
				if (_entry->getOperation() == Delete_op) {
					typename rbtree_type::baseclass &ref = this->get_container();
					ref.erase(*_entry);
					allocator_type alloc( this->get_container().get_allocator() );
					pointer ptr = &*_entry;
					alloc.destroy( ptr );
					alloc.deallocate( ptr, 1 );
					this->get_container().node_erased();
				}
			}
			virtual void rollback(Transaction &t) {
				_entry->unlock( this->get_prev_txnid(), this->get_prev_op() );
			}
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				reference ref = *_entry;
				buffer & ref;
				return 1;
			}
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				typename rbtree_type::value_type value;
				stream & value;
				
				typename rbtree_type::baseclass &container = this->get_container();
				typename rbtree_type::baseclass::iterator entry = container.find(value);
				if (entry != container.end()) {
					container.erase( entry );
					allocator_type alloc( this->get_container().get_allocator() );
					pointer ptr = &*_entry;
					alloc.destroy( ptr );
					alloc.deallocate( ptr, 1 );
				}
			}
			
		private:
			iterator _entry;	 // the entry in c being modified.
			transaction_id_t _original_txn_id;
		};
		
		
		template <class rbtree_type>
		struct rbtree_deleted_insert_operation : public assoc_transactional_operation<rbtree_type>
		{
			typedef typename rbtree_type::iterator       iterator;
			typedef typename rbtree_type::pointer        pointer;
			typedef typename rbtree_type::reference      reference;
			typedef typename rbtree_type::allocator_type allocator_type;
			
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_deleted_insert_operation,sizeof(rbtree_deleted_insert_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_deleted_insert_operation,sizeof(rbtree_deleted_insert_operation)>::free(p);
			}
			
			// Constructor used when operation is performed on a rbtree
			rbtree_deleted_insert_operation( rbtree_type &c, transaction_id_t original_txnid,
										 TransactionalOperations original_op,
										 iterator &entry)
				: assoc_transactional_operation<rbtree_type>( c, Delete_op, original_txnid, original_op )
				, _entry( entry )
			{}
			
			// Constructor used during recovery processing
			rbtree_deleted_insert_operation( rbtree_type &c )
				: assoc_transactional_operation<rbtree_type>( c )
				, _entry()
			{ }
			
			virtual void commit(Transaction &t) {
				if (_entry->getOperation() == Deleted_Insert_op) {
					typename rbtree_type::baseclass &ref = this->get_container();
					ref.erase(*_entry);
					allocator_type alloc( this->get_container().get_allocator() );
					pointer ptr = &*_entry;
					alloc.destroy( ptr );
					alloc.deallocate( ptr, 1 );
				}
			}
			virtual void rollback(Transaction &t) {
				_entry->unlock( this->get_prev_txnid(), this->get_prev_op() );
			}
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				reference ref = *_entry;
				buffer & ref;
				return 1;
			}
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				typename rbtree_type::value_type value;
				stream & value;
				
				typename rbtree_type::baseclass &container = this->get_container();
				typename rbtree_type::baseclass::iterator entry = container.find(value);
				if (entry != container.end()) {
					container.erase( entry );
					allocator_type alloc( this->get_container().get_allocator() );
					pointer ptr = &*_entry;
					alloc.destroy( ptr );
					alloc.deallocate( ptr, 1 );
				}
			}
			
		private:
			iterator         _entry;	 // the entry in c being modified.
			transaction_id_t _original_txn_id;
		};
		
		template <class rbtree_type>
		struct rbtree_lock_operation : public assoc_transactional_operation<rbtree_type>
		{
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_lock_operation,sizeof(rbtree_lock_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_lock_operation,sizeof(rbtree_lock_operation)>::free(p);
			}
			
			rbtree_lock_operation( rbtree_type &c, transaction_id_t original_txnid,
							   TransactionalOperations original_op,
							   typename rbtree_type::baseclass::iterator &entry )
			: assoc_transactional_operation<rbtree_type>( c, Lock_op, original_txnid, original_op )
			, _entry( entry )
			{}
			
			rbtree_lock_operation( rbtree_type &c )
			: assoc_transactional_operation<rbtree_type>( c ), _entry( )
			{ }
			
			virtual void commit(Transaction &t) {
				if (_entry->second.getOperation() == Lock_op)
					_entry->second.unlock( this->get_prev_txnid(), No_op );
			}
			virtual void rollback(Transaction &t) {
				_entry->second.unlock( this->get_prev_txnid(), this->get_prev_op() );
			}
			virtual int add_to_log(boost_oarchive_t &buffer) {
				return 0; // nothing written to log for lock ops.
			}
			
		private:
			typename rbtree_type::baseclass::iterator _entry;	 // the entry in c being modified.
		};
		
		template <class rbtree_type>
		struct rbtree_clear_operation : public TransactionalOperation
		{
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_clear_operation,sizeof(rbtree_clear_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_clear_operation,sizeof(rbtree_clear_operation)>::free(p);
			}
			
			// Constructor used when a clear operation is done, and also when being recovered.
			rbtree_clear_operation( rbtree_type &c )
				: _container( c )
				, _old_values( "", typename rbtree_type::key_compare(), typename rbtree_type::value_traits(), c.get_allocator() )
			{ }
			
			rbtree_type &old_values_rbtree() { return _old_values; }
			
			virtual const char * get_container_name() const { return _container.get_name(); }
			virtual int get_op_code() const { return int(Clear_op); }
			
			virtual void commit(Transaction &t) {
				_container.node_erased();
			}
			
			virtual void rollback(Transaction &t) {
				typename rbtree_type::baseclass &ref = _container;
				ref.swap( _old_values );
				// Every assoc_transactional_operation pertaining to this container which
				// follows this operation must have its _container switched to work on _old_values.
				// Cool thing about intrusive lists: I can get an iterator and container pointer based on 'this'
				typename trans_op_list_type::iterator i = trans_op_list_type::s_iterator_to(*this);
				transactional_op_list &oplist = t.get_work_in_progress();
				for (typename trans_op_list_type::iterator j = oplist.begin(); j != i; j++) {
					// assumption, container names don't change (const char* compare)
					if (j->get_container_name() == _container.get_name()) {
						assoc_transactional_operation<rbtree_type> *op = dynamic_cast<assoc_transactional_operation<rbtree_type>*>(&*j);
						if (op) // found a assoc_transactional_operation which applies to this container
							op->set_container(_old_values);
					}
				}
			}
			
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				return 1;
			}
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				typename rbtree_type::baseclass &ref = _container;
				ref.clear();
			}
			
		private:
			rbtree_type &_container;
			rbtree_type _old_values;	// temporary storage of deleted values until commit/rollback
		};
		
		template <class rbtree_type>
		struct rbtree_swap_operation : public TransactionalOperation
		{
			// new and delete overloaded to use a pool allocator, for efficiency
			void *operator new (size_t) {
				return boost::singleton_pool<rbtree_swap_operation,sizeof(rbtree_swap_operation)>::malloc();
			}
			void operator delete (void *p) {
				boost::singleton_pool<rbtree_swap_operation,sizeof(rbtree_swap_operation)>::free(p);
			}
			
			// Constructor used when a clear operation is done.  This constructor actually
			// does the clear, but retains the old values in case a rollback is done.
			rbtree_swap_operation( rbtree_type &c, rbtree_type &rarg )
			: _container( c ), _other_container( rarg )
			{ }
			
			// Constructor used during recovery processing
			rbtree_swap_operation( rbtree_type &c )
			// _other_container not used, just initialized to avoid compiler warnings
			: _container( c ), _other_container( c )
			{ }
			
			virtual const char* get_container_name() const { return _container.get_name(); }
			virtual int get_op_code() const { return int(Clear_op); }
			
			virtual void commit(Transaction &t) {
				_container.node_erased();
				_other_container.node_erased();
				// TODO - set last_clear_or_swap_lsn on BOTH rbtrees
			}
			virtual void rollback(Transaction &t) {
				typename rbtree_type::baseclass &ref = _container;
				ref.swap(_other_container);
				// Every assoc_transactional_operation pertaining to this container which
				// follows this operation must have its _container switched to work on _other_container,
				// and vice versa.
				// Cool thing about intrusive lists: I can get an iterator and container pointer based on 'this'
				typename trans_op_list_type::iterator i = trans_op_list_type::s_iterator_to(*this);
				transactional_op_list &oplist = t.get_work_in_progress();
				for (typename trans_op_list_type::iterator j = oplist.begin(); j != i; j++) {
					if (j->get_container_name() == _container.get_name()) {
						assoc_transactional_operation<rbtree_type> *op = dynamic_cast<assoc_transactional_operation<rbtree_type>*>(&(*j));
						if (op != NULL)
							op->set_container(_other_container);
					}
					if (j->get_container_name() == _other_container.get_name()) {
						assoc_transactional_operation<rbtree_type> *op = dynamic_cast<assoc_transactional_operation<rbtree_type>*>(&(*j));
						if (op != NULL)
							op->set_container(_container);
					}
				}
			}
			
			virtual int add_to_log(boost_oarchive_t &buffer) {
				this->serialize_header(buffer);
				std::string temp( _other_container.get_name() );
				buffer & temp;
				return 1;
			}
			virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
				// This method is not supposed to be called when recovering swap.  Let's make sure of it.
				throw stldb_exception("Proxy class coding error.  For swap, it is supposed to call recovery(rbtree_type&)");
			}
			// special multi-container form of recover.  The container proxy has to figure out to call this
			void recover(rbtree_type &other_container, transaction_id_t lsn) {
				typename rbtree_type::baseclass &ref = _container;
				ref.swap( other_container );
				// TODO - set last_clear_or_swap_lsn  on BOTH rbtrees
			}
			
		private:
			rbtree_type &_container;                   // the container this op applies to.
			rbtree_type &_other_container;             // the container that entries are swapped with
		};
		
		
	} // namespace stldb {
} // namespace detail {

#endif
