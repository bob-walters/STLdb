/*
 *  transIterator.h
 *  ACIDCache
 *
 *  Created by Bob Walters on 3/3/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_CONCURRENT_TRANS_ITERATOR_H
#define STLDB_CONCURRENT_TRANS_ITERATOR_H 1

#include <map>
#include <iterator>

#include <boost/thread/tss.hpp>

#include <stldb/stldb.hpp>
#include <stldb/transaction.h>
#include <stldb/transaction.h>
#include <stldb/containers/iter_less.h>

using namespace std;

namespace stldb {
namespace concurrent {

/**
 * This is an iterator adapter which will work for any associative iterator type in which the
 * value type of the base iterator is pair<const K,transEntry<V>> for any K,V.
 *
 * This adapter provides a transactionally aware iterator which keeps transactions isolated from each other.
 *
 * First, the iterator will skip over any uncomitted data, or any uncomitted data not associated with
 * the transaction that the iterator was constructed with.  This provides a consistent view of the data
 * in the map.
 *
 * Finally, when the iterator lands on an entry which has been modified by the current transaction,
 * it composes a value_type entry which has the key and the pending update value on it, and returns
 * references to that pair.  In this case, the pair being seen is not actually in the map, but is rather
 * a pending change.
 */

/* Note:  (Contract) Assumptions about container_t:
	1) it has a field _ver_num which is of type int
	2) has members baseclass::begin() and baseclass::end() which can be compared to base_iterator.
	3) it has declared this iterator to be a friend, or the above fields are public.
*/

// container_t - any container type with a bi_directional iterator type.
// base_iterator - either container_t::baseclass::iterator,
//                 or container_t::baseclass::const_iterator.
template <class container_t, class base_iterator, class lock_type>
class trans_assoc_iterator : public std::iterator <
	typename iterator_traits<base_iterator>::iterator_category,
	typename iterator_traits<base_iterator>::value_type,  // overloaded below
	typename iterator_traits<base_iterator>::difference_type,
	typename iterator_traits<base_iterator>::pointer,	// overloaded below
	typename iterator_traits<base_iterator>::reference >  // overloaded below
{
	public:
		// Assuming that the base_iterator is an iterator for some associative map type, then the
		// value_type is typically std::pair<const K, V>.  This iterator's value type needs to be
		// std::pair<const K, const V>, to help resist the urge to update entries outside of a
		// transaction (even if one purpose).
		typedef typename iterator_traits<base_iterator>::value_type  value_type;
		typedef typename iterator_traits<base_iterator>::value_type::first_type  K;
		typedef typename iterator_traits<base_iterator>::value_type::second_type V;
		typedef typename container_t::pending_change_map_t  pending_change_map_t;
//		typedef std::map<base_iterator,value_type,iter_less<base_iterator> >  pending_change_map_t; // for MVCC only
		typedef typename container_t::picket_lock_t  picket_lock_t;

		typedef value_type&  reference;
		typedef value_type*  pointer;

		/**
		 * TODO: There seems to be a bug with boost::interprocess::map<>, because although
		 * map::value_type is pair<const K, V>, it seems that map::iterator::operator* is returning
		 * pair<K,V>, in violation of current C++ standards.  The workaround are the reinterpret casts
		 * seen below and in the tcc.
		 */

		// The default constructor is for convenience.  It produces an iterator which
		// points to limbo, and if used in any manner other than assignment, will produce
		// "undefined results" (probably a SEGV);
		// Depends on base_iterator supporting default constructor also.
		trans_assoc_iterator()
			: _current_pos(), _current(NULL)
			, _lock(NULL)
			, _locker(0), _container(NULL), _pending_changes(NULL)
			, _ver_num(0)
			{ forward(0); }

		// Construct the iterator to point to the first piece of committed data in the container,
		// which is at or after i, but not past container->end().  This iterator has no association
		// to a transaction, and will never show uncomitted data.
		trans_assoc_iterator ( base_iterator i, container_t *container, lock_type &already_held )
			: _current_pos(i), _current(reinterpret_cast<pointer>(&(*i)))
			, _lock( thread_lockinfo(container) )
			, _locker(0), _container(container), _pending_changes(NULL)
			, _ver_num( container->_ver_num )
			{
				_lock->move_lock(i, already_held);
				forward(0);
			}

		// Construct the iterator to point to the first piece of committed data in the container,
		// which is at or after i, but not past container->end().  This iterator has no association
		// to a transaction, and will never show uncomitted data.
		trans_assoc_iterator ( base_iterator i, container_t *container )
			: _current_pos(i), _current(reinterpret_cast<pointer>(&(*i)))
			, _lock( thread_lockinfo(container) )
			, _locker(0), _container(container), _pending_changes(NULL)
			, _ver_num(container->_ver_num)
			{ forward(0); }

		// Show only committed data, and uncommitted data from the trans passed.  The iterator constructed
		// points to the first applicable entry at or after i, but not past map_end.
		trans_assoc_iterator ( base_iterator i, container_t *container, Transaction &trans, lock_type &already_held )
			: _current_pos(i), _current(reinterpret_cast<pointer>(&(*i)))
			, _lock( thread_lockinfo(container) )
			, _locker(trans.getLockId()), _container(container)
			, _pending_changes(trans.getContainerSpecificData<pending_change_map_t>(container))
			, _ver_num(container->_ver_num)
			{
				_lock->move_lock(i, already_held);
				forward(0);
			}

		// Show only committed data, and uncommitted data from the trans passed.  The iterator constructed
		// points to the first applicable entry at or after i, but not past map_end.
		trans_assoc_iterator ( base_iterator i, container_t *container, Transaction &trans )
			: _current_pos(i), _current(reinterpret_cast<pointer>(&(*i)))
			, _lock( thread_lockinfo(container) )
			, _locker(trans.getLockId()), _container(container)
			, _pending_changes(trans.getContainerSpecificData<pending_change_map_t>(container))
			, _ver_num(container->_ver_num)
			{ forward(0); }

		// Copy constructor
		// The ocopy and original both have a reference to a common Impl.
		// moving either iterator results in copy-on-write behavior.
		trans_assoc_iterator( const trans_assoc_iterator &rarg)
			: _current_pos( rarg._current_pos ), _current( rarg._current )
			, _lock( rarg._lock )
			, _locker( rarg._locker ), _container( rarg._container )
			, _pending_changes( rarg._pending_changes )
			, _ver_num( rarg._ver_num )
			{ forward(0); }

		~trans_assoc_iterator() {
			_lock->unlock();
		}

		// Rather than returning a pair<const K,V>&, this is going to return a pair<const K&,const V&>&,
		// which is only usable as an rvalue.  To update the entry, use the update() method on the map.
		inline reference operator*() const {
			this->lock();
			return *_current;
		}

		// Rather than returning a pair<const K,V>&, this is going to return a pair<const K&,const V&>&,
		// which is only usable as an rvalue.  To update the entry, use the update() method on the map.
		inline pointer operator->() const {
			this->lock();
			return _current;
		}

		// Overloaded to skip in-progress inserts
		trans_assoc_iterator& operator++()
			{ forward(1); return *this; }

		trans_assoc_iterator& operator--()
			{ backward(1); return *this; }

		trans_assoc_iterator operator++(int n)
			{ trans_assoc_iterator temp = *this; this->forward(1); return temp; }

		trans_assoc_iterator operator--(int n)
			{ trans_assoc_iterator temp = *this; this->backward(1); return temp; }

		base_iterator& base()
			{ return _current_pos; }

		bool valid() const
			{ return _ver_num == _container._ver_num; }

		// Since the iterator may hold a lock on the entry it is currently
		// pointing to, these methods support the locking aspects of the iter

		/**
		 * Acquire the lock on the appropriate mutex for this entry, if it
		 * is not already held.  This method is idempotent, but NOT recursive.
		 */
		inline bool lock() const
			{ _lock->lock(const_cast<container_t*>(_container),_current_pos); return true; }

		inline bool lock()
			{ _lock->lock(_container,_current_pos); return true; }

		/**
		 * Release the lock on the mutex for this entry, if one is held.
		 * This method is idempotent, but not recursive.
		 */
		inline void unlock() const
			{ _lock->unlock(); }

		/**
		 * Return true if the entry pointed to by this iterator is currently locked
		 * with the appropriate mutex from the picket lock.
		 */
		inline bool locked() const {
			return _lock->_current_pos == _current_pos && _lock->_lock;
		}

		inline lock_type& lock_held() {
			return _lock->_lock;
		}

		struct lock_info {
			lock_type                 _lock;
			base_iterator             _current_pos;

			lock_info()
				: _lock(), _current_pos()
				{ }
			lock_info(const lock_info &rarg)
				: _lock(), _current_pos(rarg._current_pos)
				{ }

			inline void lock(container_t *container, const base_iterator &pos) {
				if (_current_pos != pos) {
					unlock();
					_current_pos = pos;
				}
				if (!_lock) {
					_lock = lock_type(container->mutexForRow(_current_pos));
				}
			}

			inline void move_lock(base_iterator &pos, lock_type &held) {
				if (_current_pos != pos) {
					unlock();
					_current_pos = pos;
				}
				_lock = lock_type(move(held));
			}

			inline void unlock() {
				if (_lock)
					_lock.unlock();
			}
		};

	private:

		static BOOST_STLDB_DECL boost::thread_specific_ptr<
			std::map<container_t*,lock_info> >  _locks;

		static lock_info* thread_lockinfo(container_t *container) {
			std::map<container_t*,lock_info> *infomap = _locks.get();
			if (infomap == NULL) {
				infomap = new std::map<container_t*,lock_info>();
				_locks.reset(infomap);
			}
			return &(*infomap)[container];
		}

		// current position in container_t;
		base_iterator             _current_pos;

		// pointer to value_type to be returned by operator*, operator->.   Might not be
		// *_current_pos if a transaction is iterating over its own pending updates.
		pointer                   _current;

		// The iterator holds a lock on one of the mutexes in the _row_level_mutexes set.
		lock_info                *_lock;

		// The transaction_id of the txn this iterator is part of.  Used when determineing
		// whether to show uncomitted entries.
		transaction_id_t          _locker;

		// container the iterator is going over.  used to get to begin() & end().
		container_t              *_container;

		// A map of uncommitted update values for implementing MVCC.
		pending_change_map_t     *_pending_changes;

		// the _container._ver_num value at time of construction.   Can later be compared
		// to the _container._ver_num to determine if the iterator is still valid.
		uint64_t				  _ver_num;


		// move the iterator forward or backward n entries, skipping uncommitted changes as
		// appropriate given the _locker value.  n=0 is a valid call.
		void forward( int n );
		void backward( int n );

		//friends comparison ops:
		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator==(const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &larg,
							   const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &rarg);
		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator==(const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &larg,
							   const base_iter_t &rarg);
		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator==(const base_iter_t &larg,
							   const trans_assoc_iterator<cont_t,base_iter_t, lock_t> &rarg);

		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator!=(const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &larg,
				               const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &rarg);
		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator!=(const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &larg,
				               const base_iter_t &rarg);
		template <class cont_t, class base_iter_t, class lock_t>
		friend bool operator!=(const base_iter_t &larg,
				               const trans_assoc_iterator<cont_t,base_iter_t,lock_t> &rarg);
};

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator==(const trans_assoc_iterator<container_t, base_iterator, lock_type>& larg,
                       const trans_assoc_iterator<container_t, base_iterator, lock_type>& rarg)
{
	return (larg._current_pos == rarg._current_pos);
}

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator==(const trans_assoc_iterator<container_t, base_iterator, lock_type>& larg,
                       const base_iterator& rarg)
{
	return (larg._current_pos == rarg);
}

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator==(const base_iterator& larg,
                       const trans_assoc_iterator<container_t, base_iterator, lock_type>& rarg)
{
	return (larg == rarg._current_pos);
}

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator!=(const trans_assoc_iterator<container_t, base_iterator, lock_type>& larg,
                       const trans_assoc_iterator<container_t, base_iterator, lock_type>& rarg)
{
	return (larg._current_pos != rarg._current_pos);
}

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator!=(const trans_assoc_iterator<container_t, base_iterator, lock_type>& larg,
                       const base_iterator& rarg)
{
	return (larg._current_pos != rarg);
}

template <typename container_t, typename base_iterator, class lock_type>
inline bool operator!=(const base_iterator& larg,
                       const trans_assoc_iterator<container_t, base_iterator, lock_type>& rarg)
{
	return (larg != rarg._current_pos);
}

template <class container_t, class base_iterator, class lock_type>
void trans_assoc_iterator<container_t, base_iterator, lock_type>::forward( int n )
{
	int i = 0;
    // skip over rows which are pending inserts from other trans, or deletes done by this tran.
	while (_current_pos != _container->container_t::baseclass::end() && lock() &&
		  ((_current_pos->second.getOperation() == Insert_op && _current_pos->second.getLockId() != _locker) ||
           (_current_pos->second.getOperation() == Delete_op && _current_pos->second.getLockId() == _locker)))
    {
            _current_pos++;
    }
	while ( i<n )
	{
        _current_pos++; i++;
        // skip over rows which are pending inserts from other trans, or deletes done by this tran.
		while (_current_pos != _container->container_t::baseclass::end() && lock() &&
			  ((_current_pos->second.getOperation() == Insert_op && _current_pos->second.getLockId() != _locker) ||
			   (_current_pos->second.getOperation() == Delete_op && _current_pos->second.getLockId() == _locker)))
		{
			_current_pos++;
		}
	}
	if ( _current_pos->second.getOperation() == Update_op && _current_pos->second.getLockId() == _locker) {
		// we have landed on a row with a pending update, return references to that.
		_current = reinterpret_cast<pointer>(&(_pending_changes->find(_current_pos)->second));
	}
	else
		_current = reinterpret_cast<pointer>(&(*_current_pos));
};

template <class container_t, class base_iterator, class lock_type>
void trans_assoc_iterator<container_t, base_iterator, lock_type>::backward( int n )
{
	int i = 0;
    // skip over rows which are pending inserts from other trans, or deletes done by this tran.
    while (_current_pos != _container->container_t::baseclass::begin() && lock() &&
    	  ((_current_pos->second.getOperation() == Insert_op && _current_pos->second.getLockId() != _locker) ||
           (_current_pos->second.getOperation() == Delete_op && _current_pos->second.getLockId() == _locker)))
    {
        _current_pos--;
    }
	while ( i<=n )
	{
        _current_pos--; i++;
		// advance over rows which are pending inserts from other trans, or deletes done by this tran.
		while (_current_pos != _container->container_t::baseclass::begin() && lock() &&
			  ((_current_pos->second.getOperation() == Insert_op && _current_pos->second.getLockId() != _locker) ||
			   (_current_pos->second.getOperation() == Delete_op && _current_pos->second.getLockId() == _locker)))
		{
			_current_pos--;
		}
	}
	if ( _current_pos->second.getOperation() == Update_op && _current_pos->second.getLockId() == _locker) {
		// we have landed on a row with a pending update, return references to that.
		_current = reinterpret_cast<pointer>(&(_pending_changes->find(_current_pos)->second));
	}
	else
		_current = reinterpret_cast<pointer>(&(*_current_pos));
};


template <class container_t, class base_iterator, class lock_type>
boost::thread_specific_ptr<std::map<container_t*, typename trans_assoc_iterator<container_t, base_iterator, lock_type>::lock_info> >
trans_assoc_iterator<container_t, base_iterator, lock_type>::_locks;


} // concurrent namespace
} // stldb namespace

#endif
