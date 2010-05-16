
#ifndef STLDB_DETAIL_MAP_OPS_
#define STLDB_DETAIL_MAP_OPS_

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

// Concept / Requirements on map_type:
// 1) map_type map_type::baseclass - underlying std::map or boost::interprocess::map implementation of the map itself.
// 2) map_type must have a getName() method (non-virtual)
// 3) map_type has a 'int ver_num' member (used to help let iterators know that they may now point to a deleted entry)

template <class map_type>
struct assoc_transactional_operation : public TransactionalOperation
{
	// Constructor used when an insert operation is done
	assoc_transactional_operation( map_type &c,
            TransactionalOperations op,
			transaction_id_t prev_txnid,
			TransactionalOperations prev_op)
		: _container( &c ), _op_code(op),
		  _prev_txnid(prev_txnid), _prev_op(prev_op)
		  { }

	// Constructor used when recovering the op from logs
	assoc_transactional_operation( map_type &c )
	: _container( &c ), _prev_txnid(0), _prev_op(No_op)
	  { }

	// Accessors
	virtual const char* get_container_name() const { return _container->get_name(); }
	virtual int get_op_code() const { return int(_op_code); }
	map_type& get_container() { return *_container; }
	TransactionalOperations get_prev_op() const { return _prev_op; }
	transaction_id_t get_prev_txnid() const { return _prev_txnid; }

	// The container is the one attribute which can be changed, to accommodate
	// the way that swap() and clear() work.
	void set_container(map_type &val) { _container = &val; }

private:
	map_type *_container;
	TransactionalOperations _op_code;
	transaction_id_t _prev_txnid;
	TransactionalOperations _prev_op;
};

template <class map_type>
struct map_insert_operation : public assoc_transactional_operation<map_type>
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_insert_operation,sizeof(map_insert_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_insert_operation,sizeof(map_insert_operation)>::free(p);
	}

	// Constructor used when an insert operation is done
	map_insert_operation( map_type &c,
			typename map_type::baseclass::iterator &entry )
		: assoc_transactional_operation<map_type>(c, Insert_op, 0, No_op)
		, _entry( entry )
		{ }

	// Constructor used during recovery processing
	map_insert_operation( map_type &c)
		: assoc_transactional_operation<map_type>(c), _entry()
		{ }

	virtual void commit(Transaction &t) {
		if (_entry->second.getOperation() == Insert_op)
			_entry->second.unlock(t.getLSN());
	}
	virtual void rollback(Transaction &t) {
		// inserts which follow a delete are handled as modifies,
		// so on rollback, we always erase.
		typename map_type::baseclass &ref = this->get_container();
		ref.erase(_entry);
	}
	virtual int add_to_log(boost_oarchive_t &buffer) {
		this->serialize_header(buffer);
		typename map_type::const_reference ref = *_entry;
		buffer & ref;
		return 1;
	}
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
		typename map_type::value_type temp;
		stream & temp;
		typename map_type::baseclass &ref = this->get_container();
		temp.second.unlock(lsn);
		ref.insert(temp);  // ignore errors if already exists.
	}

private:
	typename map_type::baseclass::iterator _entry;	 // the entry in c being modified.
};

template <class map_type>
struct map_update_operation : public assoc_transactional_operation<map_type>
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_update_operation,sizeof(map_update_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_update_operation,sizeof(map_update_operation)>::free(p);
	}

	map_update_operation( map_type &c, transaction_id_t original_txnid,
						  TransactionalOperations original_op,
						  typename map_type::baseclass::iterator &entry,
						  const typename map_type::value_type &new_value )
		: assoc_transactional_operation<map_type>(c, Update_op, original_txnid, original_op)
		, _entry( entry ), _newValue(new_value)
		{ }

	// Constructor used during recovery processing
	map_update_operation( map_type &c )
		: assoc_transactional_operation<map_type>( c ), _entry(), _newValue()
		{ }

	virtual void commit(Transaction &t) {
		if (_entry->second.getOperation() == Update_op) {
			// set the value and txn_id, but leave checkpoint location untouched.
			_entry->second.base() = _newValue.second.base();
			_entry->second.unlock(t.getLSN());
		}
	}
	virtual void rollback(Transaction &t) {
		_entry->second.unlock( this->get_prev_txnid(), this->get_prev_op() );
	}
	virtual int add_to_log(boost_oarchive_t &buffer) {
		this->serialize_header(buffer);
		buffer & _newValue;
		return 1;
	}
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
		stream & _newValue;
		typename map_type::baseclass &ref = this->get_container();
		_entry = ref.find(_newValue.first);
		// Update is sometimes recorded for an insert on a row which already has a pending delete.
		// so we need to be ready to insert or update
		if (_entry != ref.end() ) {
			// set the value and txn_id, but leave checkpoint location untouched.
			_entry->second.base() = _newValue.second.base();
			_entry->second.unlock( lsn );
		}
		else {
			_newValue.second.unlock(lsn);
			ref.insert( _newValue );
		}
	}

private:
	typename map_type::baseclass::iterator _entry;	 // the entry in c being modified.
	typename map_type::value_type _newValue; // for pending updates.
};

template <class map_type>
struct map_delete_operation : public assoc_transactional_operation<map_type>
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_delete_operation,sizeof(map_delete_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_delete_operation,sizeof(map_delete_operation)>::free(p);
	}

	// Constructor used when operation is performed on a map
	map_delete_operation( map_type &c, transaction_id_t original_txnid,
						  TransactionalOperations original_op,
						  typename map_type::baseclass::iterator &entry)
		: assoc_transactional_operation<map_type>( c, Delete_op, original_txnid, original_op )
		, _entry( entry )
		{}

	// Constructor used during recovery processing
	map_delete_operation( map_type &c )
		: assoc_transactional_operation<map_type>( c ), _entry()
		{ }

	virtual void commit(Transaction &t) {
		if (_entry->second.getOperation() == Delete_op) {
			typename map_type::baseclass &ref = this->get_container();
			if (_entry->second.checkpointLocation().second > 0) {
				this->get_container()._freed_checkpoint_space.insert( _entry->second.checkpointLocation() );
			}
			ref.erase(_entry);
			this->get_container()._ver_num++;
		}
	}
	virtual void rollback(Transaction &t) {
		_entry->second.unlock( this->get_prev_txnid(), this->get_prev_op() );
	}
	virtual int add_to_log(boost_oarchive_t &buffer) {
		this->serialize_header(buffer);
		buffer & _entry->first;
		return 1;
	}
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
		typename map_type::key_type key;
		stream & key;
		typename map_type::baseclass &ref = this->get_container();
		typename map_type::baseclass::iterator entry = ref.find(key);
		if (entry != ref.end()) {
			if (entry->second.checkpointLocation().second > 0)
				this->get_container()._freed_checkpoint_space.insert(
						entry->second.checkpointLocation() );
			ref.erase( entry );
		}
	}

private:
	typename map_type::baseclass::iterator _entry;	 // the entry in c being modified.
	transaction_id_t _original_txn_id;
};


template <class map_type>
struct map_deleted_insert_operation : public assoc_transactional_operation<map_type>
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_deleted_insert_operation,sizeof(map_deleted_insert_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_deleted_insert_operation,sizeof(map_deleted_insert_operation)>::free(p);
	}

	// Constructor used when operation is performed on a map
	map_deleted_insert_operation( map_type &c, transaction_id_t original_txnid,
						  TransactionalOperations original_op,
						  typename map_type::baseclass::iterator &entry)
		: assoc_transactional_operation<map_type>( c, Delete_op, original_txnid, original_op )
		, _entry( entry )
		{}

	// Constructor used during recovery processing
	map_deleted_insert_operation( map_type &c )
		: assoc_transactional_operation<map_type>( c )
		, _entry()
		{ }

	virtual void commit(Transaction &t) {
		if (_entry->second.getOperation() == Deleted_Insert_op) {
			typename map_type::baseclass &ref = this->get_container();
			ref.erase(_entry);
		}
	}
	virtual void rollback(Transaction &t) {
		_entry->second.unlock( this->get_prev_txnid(), this->get_prev_op() );
	}
	virtual int add_to_log(boost_oarchive_t &buffer) {
		this->serialize_header(buffer);
		buffer & _entry->first;
		return 1;
	}
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
		typename map_type::key_type key;
		stream & key;
		typename map_type::baseclass &ref = this->get_container();
		ref.erase(key); // ignore errors if row not found.
	}

private:
	typename map_type::baseclass::iterator _entry;	 // the entry in c being modified.
	transaction_id_t _original_txn_id;
};

template <class map_type>
struct map_lock_operation : public assoc_transactional_operation<map_type>
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_lock_operation,sizeof(map_lock_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_lock_operation,sizeof(map_lock_operation)>::free(p);
	}

	map_lock_operation( map_type &c, transaction_id_t original_txnid,
						TransactionalOperations original_op,
						typename map_type::baseclass::iterator &entry )
		: assoc_transactional_operation<map_type>( c, Lock_op, original_txnid, original_op )
		, _entry( entry )
		{}

	map_lock_operation( map_type &c )
		: assoc_transactional_operation<map_type>( c ), _entry( )
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
	typename map_type::baseclass::iterator _entry;	 // the entry in c being modified.
};

template <class map_type>
struct map_clear_operation : public TransactionalOperation
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_clear_operation,sizeof(map_clear_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_clear_operation,sizeof(map_clear_operation)>::free(p);
	}

	// Constructor used when a clear operation is done, and also when being recovered.
	map_clear_operation( map_type &c )
		: _container( c ),
		_old_values( typename map_type::key_compare(), c.get_allocator(), "" )
		{ }

	map_type &old_values_map() { return _old_values; }

	virtual const char * get_container_name() const { return _container.get_name(); }
	virtual int get_op_code() const { return int(Clear_op); }

	virtual void commit(Transaction &t) {
		_container._ver_num++;
		_container._uncheckpointed_clear = true;
	}
	virtual void rollback(Transaction &t) {
		typename map_type::baseclass &ref = _container;
		ref.swap( _old_values );
		// Every assoc_transactional_operation pertaining to this container which
		// follows this operation must have its _container switched to work on _old_values.
		// Cool thing about intrusive lists: I can get an iterator and container pointer based on 'this'
		typename trans_op_list_type::iterator i = trans_op_list_type::s_iterator_to(*this);
		transactional_op_list &oplist = t.get_work_in_progress();
		for (typename trans_op_list_type::iterator j = oplist.begin(); j != i; j++) {
			// assumption, container names don't change (const char* compare)
			if (j->get_container_name() == _container.get_name()) {
				assoc_transactional_operation<map_type> *op = dynamic_cast<assoc_transactional_operation<map_type>*>(&*j);
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
		typename map_type::baseclass &ref = _container;
		ref.clear();
		_container._uncheckpointed_clear = true;
	}

private:
	map_type &_container;
	map_type _old_values;	// temporary storage of deleted values until commit/rollback
};

template <class map_type>
struct map_swap_operation : public TransactionalOperation
{
	// new and delete overloaded to use a pool allocator, for efficiency
	void *operator new (size_t) {
		return boost::singleton_pool<map_swap_operation,sizeof(map_swap_operation)>::malloc();
	}
	void operator delete (void *p) {
		boost::singleton_pool<map_swap_operation,sizeof(map_swap_operation)>::free(p);
	}

	// Constructor used when a clear operation is done.  This constructor actually
	// does the clear, but retains the old values in case a rollback is done.
	map_swap_operation( map_type &c, map_type &rarg )
		: _container( c ), _other_container( rarg )
		{ }

	// Constructor used during recovery processing
	map_swap_operation( map_type &c )
		// _other_container not used, just initialized to avoid compiler warnings
		: _container( c ), _other_container( c )
		{ }

	virtual const char* get_container_name() const { return _container.get_name(); }
	virtual int get_op_code() const { return int(Clear_op); }

	virtual void commit(Transaction &t) {
		_container._ver_num++;
		_other_container._ver_num++;
		// TODO - set last_clear_or_swap_lsn on BOTH maps
	}
	virtual void rollback(Transaction &t) {
		typename map_type::baseclass &ref = _container;
		ref.swap(_other_container);
		// Every assoc_transactional_operation pertaining to this container which
		// follows this operation must have its _container switched to work on _other_container,
		// and vice versa.
		// Cool thing about intrusive lists: I can get an iterator and container pointer based on 'this'
		typename trans_op_list_type::iterator i = trans_op_list_type::s_iterator_to(*this);
		transactional_op_list &oplist = t.get_work_in_progress();
		for (typename trans_op_list_type::iterator j = oplist.begin(); j != i; j++) {
			if (j->get_container_name() == _container.get_name()) {
				assoc_transactional_operation<map_type> *op = dynamic_cast<assoc_transactional_operation<map_type>*>(&(*j));
				if (op != NULL)
					op->set_container(_other_container);
			}
			if (j->get_container_name() == _other_container.get_name()) {
				assoc_transactional_operation<map_type> *op = dynamic_cast<assoc_transactional_operation<map_type>*>(&(*j));
				if (op != NULL)
					op->set_container(_container);
			}
		}
	}

	virtual int add_to_log(boost_oarchive_t &buffer) {
		this->serialize_header(buffer);
		buffer & _other_container._container_name;
		return 1;
	}
	virtual void recover(boost_iarchive_t& stream, transaction_id_t lsn) {
		// This method is not supposed to be called when recovering swap.  Let's make sure of it.
		throw stldb_exception("Proxy class coding error.  For swap, it is supposed to call recovery(map_type&)");
	}
	// special multi-container form of recover.  The container proxy has to figure out to call this
	void recover(map_type &other_container, transaction_id_t lsn) {
		typename map_type::baseclass &ref = _container;
		ref.swap( other_container );
		// TODO - set last_clear_or_swap_lsn  on BOTH maps
	}

private:
	map_type &_container;                   // the container this op applies to.
	map_type &_other_container;             // the container that entries are swapped with
};


} // namespace stldb {
} // namespace detail {

#endif
