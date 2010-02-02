/*
 *  acidcachemap.cpp
 *
 *
 *  Created by Bob Walters on 2/25/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */


#include <boost/bind.hpp>
#include <stldb/checkpoint.h>
#include <stldb/timing/timer.h>


namespace stldb {


template <class K, class V, class Comparator, class Allocator, class mutex_family>
trans_map<K,V,Comparator,Allocator,mutex_family>::trans_map(const Comparator& comp, const Allocator& alloc, const char *name)
	: map<K,TransEntry<V>,Comparator,typename Allocator::template rebind< std::pair<K,TransEntry<V> > >::other>(comp,alloc)
	, _lock()
	, _row_level_lock()
	, _row_level_lock_released()
	, _container_name(name, typename Allocator::template rebind<char>::other(alloc))
	, _ver_num(0)
	, _freed_checkpoint_space(std::less<boost::interprocess::offset_t>(),
			                  typename Allocator::template rebind<checkpoint_loc_t>::other(alloc))
	, _uncheckpointed_clear(false)
{ }


template <class K, class V, class Comparator, class Allocator, class mutex_family>
trans_map<K,V,Comparator,Allocator,mutex_family>::~trans_map()
{ }


template <class K, class V, class Comparator, class Allocator, class mutex_family>
  template <class bound_method_t, class wait_policy_t>
typename bound_method_t::result_type
trans_map<K,V,Comparator,Allocator,mutex_family>::while_row_locked(
		const bound_method_t &bound_method, iterator &i, Transaction &trans, wait_policy_t &wait_policy )
{
	stldb::timer t1("acquire row_level_lock");
	boost::interprocess::scoped_lock<mutex_type> holder(_row_level_lock);
	t1.end();
	while (true) {
		try {
			return bound_method(i, trans);
		}
		catch (row_level_lock_contention) {
			K temp = i->first; // need to make a copy before retrying the lock, just in case
			uint64_t current_ver_num = this->_ver_num;
			wait_policy.unlock_container();
			try {
				wait_policy.wait(holder, this->condition());
			}
			catch(...) {
				// to re-acquite the container-level lock, we have to first
				// unlock mutex, then lock container, to prevent mutex deadlocks
				if (holder)
					holder.unlock();
				wait_policy.relock_container();
				throw;
			}
			// to re-acquite the container-level lock, we have to first
			// unlock mutex, then lock container, to prevent mutex deadlocks
			holder.unlock();
			wait_policy.relock_container();

			// Need to see if 'i' is still in the map.  If it might have been
			// deleted, we have to re-read it.
			if (current_ver_num != this->_ver_num) {
				i = this->find(temp, trans);
				if ( i == end() ) {
					// some other transaction deleted i while we were waiting.
					throw row_deleted_exception();
				}
			}
			holder.lock(); // will need _row_level_lock for next iteration.
		}
	}
}


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::find(const K& key)
{
	stldb::timer t1("trans_map::find(key)");
	typename baseclass::iterator i = baseclass::find(key);
	if ( i == baseclass::end() || i->second.getOperation() == Insert_op )
		return iterator( baseclass::end(), this );
	else
		return iterator( i, this );
};

template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::const_iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::find(const K& key) const
{
	stldb::timer t1("trans_map::find(key)");
	typename baseclass::const_iterator i = baseclass::find(key);
	if ( i == baseclass::end() || i->second.getOperation() == Insert_op )
		return const_iterator( baseclass::end(), this );
	else
		return const_iterator( i, this );
};

template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::find(const K& key, Transaction &trans)
{
	stldb::timer t1("trans_map::find(key,trans)");
	typename baseclass::iterator i = baseclass::find(key);
	if ( i == baseclass::end() ||
		(i->second.getOperation() == Delete_op && i->second.getLockId() == trans.getLockId() ) ||
		(i->second.getOperation() == Insert_op && i->second.getLockId() != trans.getLockId() ))
		return iterator( baseclass::end(), this, trans );
	else
		return iterator( i, this, trans );
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::lower_bound(const K& key, Transaction &trans)
{
	stldb::timer t1("trans_map::lower_bound(key,trans)");
	typename baseclass::iterator i = baseclass::lower_bound(key);
	// can leave out check for Deleted_op, Insert_op, etc because the iterator's constructor will
	// advance the position appropriately.
	return iterator( i, this, trans );
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::lower_bound(const K& key)
{
	stldb::timer t1("trans_map::lower_bound(key,trans)");
	typename baseclass::iterator i = baseclass::lower_bound(key);
	// can leave out check for Deleted_op, Insert_op, etc because the iterator's constructor will
	// advance the position appropriately.
	return iterator( i, this );
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::upper_bound(const K& key, Transaction &trans)
{
	stldb::timer t1("trans_map::upper_bound(key,trans)");
	typename baseclass::iterator i = baseclass::upper_bound(key);
	// can leave out check for Deleted_op, Insert_op, etc because the iterator's constructor will
	// advance the position appropriately.
	return iterator( i, this, trans );
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::upper_bound(const K& key)
{
	stldb::timer t1("trans_map::upper_bound(key,trans)");
	typename baseclass::iterator i = baseclass::upper_bound(key);
	// can leave out check for Deleted_op, Insert_op, etc because the iterator's constructor will
	// advance the position appropriately.
	return iterator( i, this );
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::lock_i( iterator &i, Transaction &trans )
{
	stldb::timer t1("trans_map::lock_i(iterator,trans)");
	if ( i->second.getOperation() != No_op && i->second.getLockId() != trans.getLockId() )
		throw row_level_lock_contention();

	if ( i->second.getLockId() != trans.getLockId() )
	{
		// Set our read lock in place now.
		transaction_id_t oldlock = i->second.getLockId();
		TransactionalOperations oldop = i->second.getOperation();
		i->second.lock( trans, Lock_op );
		trans.insert_work_in_progress( this,
			new detail::map_lock_operation<trans_map>(*this, oldlock, oldop, i.base()) );
	}
}


template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::lock( iterator &i, Transaction &trans )
{
	stldb::timer t1("trans_map::lock(iterator,trans)");
	boost::interprocess::scoped_lock<mutex_type> holder(_row_level_lock);
	lock_i( i, trans );
}

// version which blocks.  I need some kind of general wrapper for this crap which uses
template <class K, class V, class Comparator, class Allocator, class mutex_family>
  template <class wait_policy_t>
void trans_map<K,V,Comparator,Allocator,mutex_family>::lock( iterator &i, Transaction &trans, wait_policy_t &wait_policy )
{
	stldb::timer t1("trans_map::lock(iterator,trans,wait_policy)");
	this->while_row_locked(boost::bind(&trans_map::lock_i, this, _1, _2),
			i, trans, wait_policy );
}

template <class K, class V, class Comparator, class Allocator, class mutex_family>
V& trans_map<K,V,Comparator,Allocator,mutex_family>::update_i( iterator& i, const V &newValue,
		Transaction &trans )
{
	stldb::timer t1("trans_map::update_i(iterator,trans,wait_policy)");
	if ( i->second.getOperation() != No_op && i->second.getLockId() != trans.getLockId() )
		throw row_level_lock_contention();

	// Either op==No_op or I already have the lock.
	transaction_id_t oldlock= i->second.getLockId();
	TransactionalOperations oldop = i->second.getOperation();
	switch ( i->second.getOperation() )
	{
		case Insert_op:
			// we've updated a row we already have a pending insert on.  optimize...
			// no need to create additional transactions ops.  The one for insert can cover this too.
			i->second = newValue;
			break;
		case Delete_op:
			// invalid.  Someone has passed an iterator they previously passed to erase.
			throw row_deleted_exception();
		case No_op:
		case Lock_op:
			i->second.lock( trans, Update_op );
			// deliberate fallthrough! (no break)
		case Update_op:
			// The update is not stored on the row directly.  We store it in a heap-based array on the
			// transaction, keyed off the iterator, which contains the new value_type to be
			// stored in the map at the time of commit.
			{
				stldb::timer t2("add pending update & insert_work_in_progress");
				this->addPendingUpdate(trans, i.base(), newValue);
				trans.insert_work_in_progress( this, new detail::map_update_operation<trans_map>(
					*this, oldlock, oldop, i.base(), value_type( i->first, newValue) ) );
			}
			break;
		default:
			// sign of internal corruption.
			throw recovery_needed();
	}
	//TODO - should we just be returning i at this point?
	return (i->second);
}


template <class K, class V, class Comparator, class Allocator, class mutex_family>
V& trans_map<K,V,Comparator,Allocator,mutex_family>::update( iterator& i, const V &newValue,
		Transaction &trans )
{
	stldb::timer t1("trans_map::update(iterator,newvalue,trans)");
	boost::interprocess::scoped_lock<mutex_type> holder(_row_level_lock);
	return update_i(i, newValue, trans);
}


// version which blocks.
template <class K, class V, class Comparator, class Allocator, class mutex_family>
	template <class wait_policy_t>
V& trans_map<K,V,Comparator,Allocator,mutex_family>::update( iterator& i, const V &newValue,
		Transaction &trans, wait_policy_t &wait_policy )
{
	stldb::timer t1("trans_map::update(iterator,newvalue,trans,wait_policy)");
	return this->while_row_locked(	boost::bind(&trans_map::update_i, this, _1, newValue, _2),
			i, trans, wait_policy );
}


template <class K, class V, class Comparator, class Allocator, class mutex_family>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::insert( iterator i, const value_type& v, Transaction& trans )
{
	stldb::timer t1("trans_map::insert(iterator,v,trans)");
	value_type vt( v.first, v.second );
	vt.second.lock(trans, Insert_op);

	iterator j = baseclass::insert( i.base(), vt );

	if (j != baseclass::end() ) {
		// j points to either a row that we just inserted, or one that was already there.
		switch (j->second.getOperation()) {
		case No_op:
		case Lock_op:
		case Update_op:
			// Failed to insert - a committed entry already exists.
			return j;
		case Insert_op:
			// If existing row found, under another lock_id, with Insert_op, then we have
			// thread contention on insert.
			if (j->second.getLockId() != trans.getLockId())
				throw row_level_lock_contention();
			// If caller invoked insert() twice in same transaction, for same key,
			// then this second+ insert didn't succeed.
			if (j->second != v.second)
				return j;
			break;
		case Delete_op:
			if (j->second.getLockId() == trans.getLockId()) {
				j->second.lock( trans, Update_op );
				this->addPendingUpdate(trans, j.base(), v.second);
				trans.insert_work_in_progress( this,
					new detail::map_update_operation<trans_map>( *this,
							trans.getLockId(), Delete_op, j.base(),  v) );
				return j;
			}
			else
				throw row_level_lock_contention();
			break;
		}
		// If we get here, then the insert succeeded.
		trans.insert_work_in_progress( this,
				new detail::map_insert_operation<trans_map>(*this, j.base()) );
	}
	return j;
};

template <class K, class V, class Comparator, class Allocator, class mutex_family>
	template <class wait_policy_t>
typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator
trans_map<K,V,Comparator,Allocator,mutex_family>::insert( iterator i, const value_type& v,
		Transaction &trans, wait_policy_t &wait_policy )
{
	stldb::timer t1("trans_map::insert(iterator,v,trans,wait_policy)");
	while (true) {
		try {
			return insert(i, v, trans);
		}
		catch (row_level_lock_contention &ex) {
			// found an existing row with the same key which was an in-progress
			// insert that will either be completed via commit, or rolled back (deleted)
			// need to wait until the wave form collapses into one or the other.

			uint64_t current_ver_num = this->_ver_num;
			{
				// get row-level mutex.  needed for the condition var.  Need to get the mutex
				// before releasing the container lock in order to not miss a wake-up call.
				boost::interprocess::scoped_lock<mutex_type> row_lock_holder(_row_level_lock);

				wait_policy.unlock_container();
				try {
					wait_policy.wait(row_lock_holder, this->condition());
				}
				catch(...) {
					// to re-acquite the container-level lock, we have to first
					// unlock mutex, then lock container, to prevent mutex deadlocks
					if (row_lock_holder)
						row_lock_holder.unlock();
					wait_policy.relock_container();
					throw;
				}
			}
			// re-acquire exclusive lock, after release the row_level_lock.
			// Note that there is a gap here where no lock is held, so i must be validated.
			wait_policy.relock_container();

			// check to see if i might have gone invalid, since we did release our lock.
			if (current_ver_num != this->_ver_num) {
				i = this->end(trans);
			}
		}
	}
}

template <class K, class V, class Comparator, class Allocator, class mutex_family>
std::pair<typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator, bool>
trans_map<K,V,Comparator,Allocator,mutex_family>::insert( const value_type& v, Transaction& trans )
{
	stldb::timer t1("trans_map::insert(value_type,trans)");
	typename baseclass::value_type vt( v.first, v.second );
	vt.second.lock(trans, Insert_op);

	std::pair<typename baseclass::iterator, bool> result = baseclass::insert( vt );

	// insert might not work, in the case of a duplicate
	if (result.second) {
		trans.insert_work_in_progress( this,
				new detail::map_insert_operation<trans_map>(*this, result.first) );
	}
	else {
		// Insert failed, but it might still take if the entry is deleted.
		switch (result.first->second.getOperation()) {
		case No_op:
		case Lock_op:
		case Update_op:
			// we failed because there was an existing committed value
			break;
		case Insert_op:
			// we failed because another thread has a pending insert,
			if (result.first->second.getLockId() != trans.getLockId())
				throw row_level_lock_contention();
			// or because we have a pending insert, already.
			break;
		case Delete_op:
			// we can insert on top of a delete operation.
			if (result.first->second.getLockId() == trans.getLockId()) {
				result.first->second.lock( trans, Update_op );
				result.second = true;
				this->addPendingUpdate(trans, result.first, v.second);
				trans.insert_work_in_progress( this,
					new detail::map_update_operation<trans_map>(*this,
							trans.getLockId(), Delete_op, result.first, v) );
			}
			else
				throw row_level_lock_contention();
			break;
		default:
			// sign of internal corruption.
			throw recovery_needed();
		}
	}
	iterator i(result.first, this, trans);
	return std::make_pair<iterator,bool>(i, result.second);
};


template <class K, class V, class Comparator, class Allocator, class mutex_family>
	template <class wait_policy_t>
std::pair<typename trans_map<K,V,Comparator,Allocator,mutex_family>::iterator, bool>
trans_map<K,V,Comparator,Allocator,mutex_family>::insert( const value_type& v,
		Transaction &trans, wait_policy_t &wait_policy )
{
	stldb::timer t1("trans_map::insert(value_type,trans,wait_policy)");
	while (true) {
		try {
			return insert(v, trans);
		}
		catch (row_level_lock_contention) {
			// get row-level mutex.  needed for cond.wait.  Need to get the mutex
			// before releasing the container lock in order to not miss a wake-up call.
			boost::interprocess::scoped_lock<mutex_type> row_lock_holder(_row_level_lock);

			wait_policy.unlock_container();
			try {
				wait_policy.wait(row_lock_holder, this->condition());
			}
			catch(...) {
				// to re-acquite the container-level lock, we have to first
				// unlock mutex, then lock container, to prevent mutex deadlocks
				if (row_lock_holder)
					row_lock_holder.unlock();
				wait_policy.relock_container();
				throw;
			}
		}
		// re-acquire exclusive lock, after release the row_level_lock.
		// Note that there is a gap here where no lock is held.
		wait_policy.relock_container();
	}
}

template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::erase_i( iterator& i, Transaction& trans )
{
	stldb::timer t1("trans_map::erase_i(iterator,trans)");
	if ( i->second.getOperation() != No_op && i->second.getLockId() != trans.getLockId() )
		throw row_level_lock_contention();
	// Either op is No_op, or we are already a locker of this row.
	transaction_id_t oldlock = i->second.getLockId();
	TransactionalOperations oldop = i->second.getOperation();
	switch (oldop) {
		case Insert_op:
			i->second.lock(trans, Deleted_Insert_op);
			// Record an operation for the transaction which will indicate that this row has been locked.
			trans.insert_work_in_progress( this,
					new detail::map_deleted_insert_operation<trans_map>(*this, oldlock, oldop, i.base()) );
			break;
		case No_op:
		case Lock_op:
		case Update_op:
			i->second.lock(trans, Delete_op);
			// Record an operation for the transaction which will indicate that this row has been locked.
			trans.insert_work_in_progress( this,
					new detail::map_delete_operation<trans_map>(*this, oldlock, oldop, i.base()) );
			break;
		case Delete_op:
			throw row_deleted_exception();
		default:
			// sign of internal corruption.
			throw recovery_needed();
	}
};

template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::erase( iterator i, Transaction& trans )
{
	stldb::timer t1("trans_map::erase(iterator,trans)");
	boost::interprocess::scoped_lock<mutex_type> holder(_row_level_lock);
	erase_i( i, trans );
}

// version which blocks.
template <class K, class V, class Comparator, class Allocator, class mutex_family>
	template <class wait_policy_t>
void trans_map<K,V,Comparator,Allocator,mutex_family>::erase( iterator i, Transaction& trans,
		wait_policy_t &wait_policy )
{
	stldb::timer t1("trans_map::erase(iterator,trans,wait_policy)");
	this->while_row_locked(boost::bind<void>(&trans_map::erase_i, this, _1, _2),
			i, trans, wait_policy );
}


template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::clear(exclusive_transaction& trans)
{
	/**
	 * Important assumption underlying this methods design:
	 * 		boost::interprocess::map map1 = ...;
	 * 		boost::interprocess::map map2 = ...;
	 * 		map::iterator i = map1.find( something );
	 * 		map1.swap(map2);
	 * 		map2.erase(i); // is a valid call,
	 * 		// because the maps implement swap() by exchanging their root nodes.
	 */
	detail::map_clear_operation<trans_map> *clear_op = new detail::map_clear_operation<trans_map>(*this);
	trans_map &old_values = clear_op->old_values_map();
	old_values.baseclass::swap(*this);  // 'this' is now an empty map.

	// clear() and swap(), when they follow other transactional operations, must modify
	// those operations so that they know to with with the other map during commit processing.
	transactional_op_list &ops = trans.get_work_in_progress();
	for ( typename transactional_op_list::iterator i = ops.begin(); i!=ops.end(); i++) {
		if (i->get_container_name() != _container_name.c_str())
			continue;
		detail::assoc_transactional_operation<trans_map> *op = dynamic_cast<detail::assoc_transactional_operation<trans_map>*>(&(*i));
		if (op == NULL)
			continue;
		op->set_container(old_values);
	}
	// When this class of exclusive transaction is done, the clear is done immediately,
	// since it is an exclusive transaction.  The clear is actually done as a swap(),
	// by the constructor of map_clear_operation(), so that the old data is retained in
	// case of rollback, and also so that any iterators on previously recorded
	// TransactionalOps will still be valid, and not cause SEGVs when commit/rollback is done.
	trans.insert_work_in_progress( this, clear_op );
}

template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::swap(trans_map &other, exclusive_transaction& trans)
{
	/**
	 * Important assumption underlying this methods design:
	 * 		boost::interprocess::map map1 = ...;
	 * 		boost::interprocess::map map2 = ...;
	 * 		map::iterator i = map1.find( something );
	 * 		map1.swap(map2);
	 * 		map2.erase(i); // is a valid call,
	 * 		// because the maps implement swap() by exchanging their root nodes.
	 */
	detail::map_swap_operation<trans_map> *swap_op = new detail::map_swap_operation<trans_map>(*this, other);
	this->baseclass::swap(other);  // 'this' and 'other' now contain each other's data.

	// clear() and swap(), when they follow other transactional operations, must modify
	// those operations so that they know to with with the other map during commit processing.
	transactional_op_list &ops = trans.get_work_in_progress();
	for ( typename transactional_op_list::iterator i = ops.begin(); i!=ops.end(); i++) {
		const char *name = i->get_container_name();
		if (name == _container_name.c_str()) {
			detail::assoc_transactional_operation<trans_map> *op = dynamic_cast<detail::assoc_transactional_operation<trans_map>*>(&(*i));
			if (op == NULL)
				continue;
			op->set_container(other);
		}
		if (name == other._container_name.c_str()) {
			detail::assoc_transactional_operation<trans_map> *op = dynamic_cast<detail::assoc_transactional_operation<trans_map>*>(&(*i));
			if (op == NULL)
				continue;
			op->set_container(*this);
		}
	}
	// When this class of exclusive transaction is done, the swap is done immediately,
	// since it is an exclusive transaction.  It can be redone in the
	// case of rollback.
	trans.insert_work_in_progress( this, swap_op );
}

// save checkpoint of map
// Checkpoint Entry Structure (on disk)  length(size_t),bytes(N)
template <class K, class V, class Comparator, class Allocator, class mutex_family>
  template <class Database_t>
void trans_map<K,V,Comparator,Allocator,mutex_family>::save_checkpoint(
		Database_t &db,
		checkpoint_ofstream &checkpoint,
		transaction_id_t last_checkpoint_lsn )
{
	int count = 0, total=0, scanned=0;
	static const int entries_per_segment = 10;  // TODO - part of config
	static const int entries_per_scan = 50;     // TODO - part of config

    db.safety_check();

    // Get the freed space from the map, and reset the maps freed space to empty
	{
	  boost::interprocess::scoped_lock<upgradable_mutex_type> lock(mutex());

	  if (this->_uncheckpointed_clear) {
		  // clear() was called since last checkpoint.  _freed_checkpoint_space should
		  // be all space not already free in the checkpoint file.
		  checkpoint.clear( );
		  this->_uncheckpointed_clear = false;
	  }
	  else {
		  if (!this->_freed_checkpoint_space.empty() && tracing::get_trace_level() >= finer_e) {
			  STLDB_TRACE(fine_e, "Checkpoint space freed by map erase() calls [offset,size]: ");
			  typedef typename boost::interprocess::map<boost::interprocess::offset_t, std::size_t,
					std::less<boost::interprocess::offset_t>,
					typename Allocator::template rebind<checkpoint_loc_t >::other>::iterator iterator_t;
			  for (iterator_t i=this->_freed_checkpoint_space.begin(); i != _freed_checkpoint_space.end(); i++)
			  {
				  STLDB_TRACE(fine_e, "[" << i->first << "," << i->second << "]");
			  }
		  }
		  // different allocator types prevent use of swap() here.
		  checkpoint.add_free_space(_freed_checkpoint_space.begin(), _freed_checkpoint_space.end());
		  _freed_checkpoint_space.clear();
	  }
	}

	STLDB_TRACE(info_e, "Starting checkpoint of Map with " << this->size() << " entries.");

	std::pair<key_type,mapped_type> values[entries_per_segment];

	boost::interprocess::sharable_lock<upgradable_mutex_type> lock(mutex());
	key_type next_loop_key;
	bool done = false;

	iterator i = begin();
	while (i != end())
	{
	    db.safety_check();  // abort if db goes invalid at any point.

		while (i != end() && count < entries_per_segment && scanned < entries_per_scan) {
            // we have to write out all in-progress changes with each checkpoint,
            // because we can't be sure if the committed value has been written out.
			// this is a side effect of the fact that I'm not storing both the
			// last commited lsn and the inprog txn_id at the same time on TransEntry<>
			transaction_id_t last_commit_lsn = i->second.getLockId();
		    if ( (last_commit_lsn > last_checkpoint_lsn && i->second.getOperation() == No_op)
		       || i->second.getOperation() == Lock_op || i->second.getOperation() == Update_op
		       || i->second.getOperation() == Delete_op )
		    {
		    	values[count++] = *i;
		    }
		    scanned++;
		    i++;
		}
		if (i != end()) {
			next_loop_key = i->first;
		}
		else {
			done = true;
		}
		lock.unlock();

		// The serialization to the output can be done without holding
		// any lock.  Write the entries found out to the free-space portions of
		// the checkpoint file.
		for (int j=0; j<count; j++) {
			// When writing out the committed data for a row which has an
			// existing transaction in progress, the txn_id written out for
			// that row needs to be 0, because on load, that becomes the
			// committed LSN of that row, and we need to make sure
			// that log processing will apply all LSNs found for that row.
			// (In case I try to optimize
			// recovery so that ops are not unnecessarily re-applied.)
			if ( values[j].second.getOperation() != No_op ) {
				// best estimate of the LSN.
				values[j].second.unlock(last_checkpoint_lsn+1);
			}

//			STLDB_TRACE(finer_e, "Write: [" << values[j].first << "," << values[j].second << "] op: "
//							<< values[j].second.getOperation() << " lsn: " << values[j].second.getLockId()
//							<< "prior ckpt:{" << values[j].second.checkpointLocation().first << ","
//							<< values[j].second.checkpointLocation().second << "}" );

			std::pair<boost::interprocess::offset_t,std::size_t> new_location =
					checkpoint.write( values[j], values[j].second.checkpointLocation() );

			STLDB_TRACE(finer_e, "   new ckpt:{" << new_location.first << "," << new_location.second << "}" );

	        // remember the value's new checkpoint location
	        values[j].second.setCheckpointLocation( new_location );
		}

		lock.lock();
		// update the entries in the map to indicate their new checkpoint
		// locations.  Unfortunately, because we unlocked the map during the I/O
		// we have not retained the iterators to the values[].  An alternative
		// to this algorithm would be to serialize the entries to buffers and allocate
		// space for those buffers in a tight loop, with a lock, then update the
		// entries directly via the still-valid iterators.  I decided not to have
		// a sharedlock during object serialization, and instead go with this approach.
		for (int j=0; j<count; j++) {
			typename baseclass::iterator i( this->baseclass::find( values[j].first ) );
			if (i != this->baseclass::end() ) {
				i->second.setCheckpointLocation( values[j].second.checkpointLocation());
			}
		}

		// set i to the starting entry for the next iteration of this loop
		if (!done) {
			i = iterator( baseclass::lower_bound(next_loop_key), this );
		}
		else {
			i = end();
		}

		// update the entries
		total += count;
		count = 0;
		scanned = 0;
	}
	STLDB_TRACE(info_e, "Checkpointed Map contains " << this->size() << "entries.");
	STLDB_TRACE(info_e, "Wrote " << total << "entries to checkpoint.");
}

// load a checkpoint.
template <class K, class V, class Comparator, class Allocator, class mutex_family>
void trans_map<K,V,Comparator,Allocator,mutex_family>::load_checkpoint(checkpoint_ifstream &checkpoint)
{
	// loading a checkpoint is done as an exclusive operation.
	// There's no reason for it not to be.
	// Strictly speaking the lock is not required.
	boost::interprocess::scoped_lock<upgradable_mutex_type> lock(mutex());
	checkpoint_iterator<value_type> i( checkpoint.begin<value_type>() );
	STLDB_TRACE(finer_e, "Loading checkpoint: ");
	typename baseclass::iterator pos = baseclass::end();
	while (i != checkpoint.end<value_type>()) {
		value_type entry( *i );
		entry.second.setCheckpointLocation( i.checkpoint_location() );
		pos = baseclass::insert( pos, entry );
//		STLDB_TRACE(finer_e, "[" << entry.first << "," << entry.second << "] op: "
//				<< entry.second.getOperation() << " lsn: " << entry.second.getLockId()
//				<< "ckpt:{" << entry.second.checkpointLocation().first << ","
//				<< entry.second.checkpointLocation().second << "}" );
//		assert( this->baseclass::find( entry.first )->second.checkpointLocation().second != 0);
		i++;
	}
	STLDB_TRACE(info_e, "Load_checkpoint done, map contains " << this->size() << " entries.");
}


} // namespace stldb

