/*
 *  acidcachemap.h
 *  ACIDCache
 *
 *  Created by Bob Walters on 2/24/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_TRANS_MAP_H
#define STLDB_TRANS_MAP_H 1

#include <iterator>
#include <string>
#include <map>
#include <vector>
#include <utility>   // std::pair
#include <functional>

#include <boost/pool/pool_alloc.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>  // for detail arg
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/containers/string.hpp>

#include <stldb/exceptions.h>
#include <stldb/container_proxy.h>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/trans_assoc_iterator.h>
#include <stldb/containers/iter_less.h>
#include <stldb/containers/detail/map_ops.h>
#include <stldb/transaction.h>
#include <stldb/checkpoint.h>
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

/**
 * Concurrent ACID-compliant transactional map implementation.
 *
 * The map itself has a lock.  This lock is used to protect the structure of the map during
 * concurrent access, but is held only for the duration of the operation being performed
 * (short), not for the duration of the transaction.  It's also allowed to be an upgradable_mutex
 * which is locked in a shared mode for most operations.  This lock is not automatically
 * acquired by any of the methods of the transaction map, rather, the caller must acquire
 * the lock directly (via the mutex() method), but is also thus able to control the scope
 * and granularity of the locking used with a series of operations.
 *
 * The map also recognizes row-level locks on the individual entries.  These row-level locks
 * are accomplished as indicators on the individual map entries.  The coordination of waiting
 * on row level locks is based on the use of an internal mutex and condition variable.  How the
 * wait is done (i.e. whether indefinite or with a timeout) is controlled via the wait_policy
 * object passed to those methods which can block on row-level locks.
 *
 * Iterators can be used safely in all cases where the iterator's scope is limited to the scope
 * of a container-level lock.  Iterators can also be used beyond the scope of a lock, but in that
 * case, the iterator might go invalid.  The iterators include an optional means to determine whether
 * they may be invalid due to erase(), clear(), etc. ops which may have been performed
 * on the map since they were created, invalidating them in the process.
 *
 * This map implementation is intended for high transaction concurrency, so:
 * 1) isolation is at read-committed level, so a transaction sees only committed data and its
 * own changes made under that transaction.  The transaction also sees newly committed changes
 * as they occur.  So re-reading the value of a row could reveal a different value.  Uncommitted
 * changes from other transactions are never seen however.
 * 2) multi-view concurrency control is implemented.  Writers do not block readers.  A reader
 * will see the last committed version of the row, and is not blocked if another transaction
 * has an uncommitted change (and write lock on the row) in progress.
 *
 * Normally std::map's entries are modified by using iterator->second as an lvalue.  This is not
 * possible with trans_map because of the MVCC support, and because it would be very hard to
 * correctly identify cases where the application accessed the value without changing it vs. cases
 * where a change was made.  So the normal map API is extended
 * to include an update() method which makes a change to an entries value under a
 * transaction.  TODO - might want a method on the iterator which allows you to explicitly
 * prep a value for update, then provide direct access to the updated value.
 *
 * Other methods of the
 * normal map<> class are also extended to include transaction parameters.  The transaction which
 * is passed to these methods (e.g. begin(), end(), find(), etc.) is used to set the
 * transaction that the resulting iterator is associated with.  The iterator then obeys the
 * isolations rules described above (1 and 2).
 *
 */

using namespace std;
using boost::interprocess::map;

//!Parameters:
// K, V, Comparator, and Allocator are as per std::map.
// mutex_family is as per the boost::interprocess::mutex_family concept.  In this case,
// it must be a structure with the following typedefs:
// 		typedef mutex_type;
// 		typedef upgradable_mutex_type;
// 		typedef condition_type;
//
// trans_map supports using an upgradable_mutex for the container-level lock.
// In that case, shared_locks can be used while
// executing all methods except insert, swap, clear, and commit.  (i.e. All methods which
// don't invalidate iterators by affecting the structure of the map itself.)  An exclusive (scoped)
// lock is required for those 4 operations.
// iterators are thus only good for the duration of locks acquired on the container's mutex().

template <class K, class V, class Comparator = std::less<K>,
          class Allocator = boost::interprocess::allocator<std::pair<const K, V>,
						    typename boost::interprocess::managed_mapped_file::segment_manager>,
		  class mutex_family = stldb::bounded_mutex_family>
  class trans_map
	: public boost::interprocess::map<K,TransEntry<V>,Comparator, typename Allocator::template rebind< std::pair<K,TransEntry<V> > >::other >
{
  public:
	// overloads of some typedefs.
	typedef typename Allocator::template rebind<std::pair<K,TransEntry<V> > >::other  base_alloc;
	typedef map<K,TransEntry<V>,Comparator,base_alloc>  baseclass;

	typedef K key_type;
	typedef TransEntry<V> mapped_type;
	typedef std::pair<const K, TransEntry<V> > value_type;
	typedef Comparator key_compare;
	typedef Allocator  allocator_type;

	// mutex and condition types used within map.
	typedef typename mutex_family::upgradable_mutex_type upgradable_mutex_type;
	typedef typename mutex_family::mutex_type            mutex_type;
	typedef typename mutex_family::condition_type        condition_type;

	typedef value_type&        reference;
	typedef const value_type&  const_reference;
	typedef value_type*        pointer;
	typedef const value_type*  const_pointer;

	typedef typename Allocator::size_type size_type;
	typedef typename Allocator::difference_type difference_type;

	friend class trans_assoc_iterator<trans_map, typename baseclass::iterator>;
	friend class trans_assoc_iterator<trans_map, typename baseclass::const_iterator>;

	typedef trans_assoc_iterator<trans_map, typename baseclass::iterator>        iterator;
	typedef trans_assoc_iterator<trans_map, typename baseclass::const_iterator>  const_iterator;

	typedef std::reverse_iterator< iterator >        reverse_iterator;
	typedef std::reverse_iterator< const_iterator >  const_reverse_iterator;

	// The type of a map used to holding uncommitted changes as part of MVCC
	// The reason std::less is not used for comparison is that it isn't required to work
	// for std::iterators.  It doesn't work for boost::interprocess::map<>::iterator.
	typedef std::map<typename baseclass::iterator, value_type,
	                 iter_less<typename baseclass::iterator> > pending_change_map_t;

	// constructors.
	// Note that constructors are not transactional.
	explicit trans_map(const Comparator& comp, const Allocator&, const char *name);

	template <class InputIterator>
	trans_map(InputIterator first, InputIterator last,
			  const Comparator& comp, const Allocator& alloc, const Transaction& trans, int id);

	trans_map(const trans_map& rarg);

	// std destructor.
	~trans_map();

	allocator_type get_allocator() const {
		return baseclass::get_allocator();
	}

	// Iterator methods which don't take a transaction parameter return iterators
	// that show only committed records.
	iterator begin() { return iterator(baseclass::begin(), this); }
	const_iterator begin() const { return const_iterator(baseclass::begin(), this); }
	iterator end() { return iterator(baseclass::end(), this); }
	const_iterator end() const { return const_iterator(baseclass::end(), this); }

	reverse_iterator rbegin() { return reverse_iterator(end()); }
	const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
	reverse_iterator rend() { return reverse_iterator(begin()); }
	const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

	// Iterator methods which take a transaction parameter return iterators
	// that show committed data, and uncommitted changes belonging to that transaction.
	iterator begin(Transaction &trans) { return iterator(baseclass::begin(), this, trans); }
	iterator end(Transaction &trans) { return iterator(baseclass::end(), this, trans); }

	reverse_iterator rbegin(Transaction &trans) { return reverse_iterator(end(trans)); }
	reverse_iterator rend(Transaction &trans) { return reverse_iterator(begin(trans)); }

	// insert methods which do not block
	// EXCLUSIVE LOCK must be held on call to insert.
	// The row is actually inserted into the container.  If there is a committed row already
	// in the container, insert will fail, as per std::map.  If there is an uncommitted
	// insert in the container, a row_level_lock_contention exception is thrown back to
	// caller.  In no case does this API call block.
	std::pair<iterator, bool> insert (const value_type&, Transaction &trans);
	iterator insert (iterator, const value_type&, Transaction &trans);
	template <class InputIterator> void insert (InputIterator, InputIterator, Transaction &trans);

	// insert methods which can block if there is already an uncommitted insert/delete in place with the same key value
	// If the insert method is forced to block it does a cond_wait.wait(lock).  The lock passed
	// must already be held by the caller. Other than the chance of blocking, these methods are
	// the same as the previous set.
	template <class wait_policy_t> std::pair<iterator, bool> insert (const value_type&, Transaction &trans, wait_policy_t &wait_policy);
	template <class wait_policy_t> iterator insert (iterator, const value_type&, Transaction &trans, wait_policy_t &wait_policy);
	template <class InputIterator, class wait_policy_t> void insert (InputIterator, InputIterator, Transaction &trans, wait_policy_t &wait_policy);

	// erase methods.
	// erase the row pointed to by iterator.  If the row is already locked by another
	// thread this method fails immediately with a row_level_lock_contention exception.
	void erase (iterator, Transaction &trans);
	size_type erase (const K&, Transaction &trans);
	void erase (iterator, iterator, Transaction &trans);

	// blocking versions.  If the row is already locked, waits for it to become unlocked,
	// and then erases it.  If it is erased in the interim via commit of another transaction
	// throws a row_deleted_exception.
	template <class wait_policy_t> void erase (iterator, Transaction &trans, wait_policy_t &wait_policy);
	template <class wait_policy_t> size_type erase (const K&, Transaction &trans, wait_policy_t &wait_policy);
	template <class wait_policy_t> void erase (iterator, iterator, Transaction &trans, wait_policy_t &wait_policy);

	// clear the map, deleting all entries.  Must be done under an exclusive_transaction.
	void clear(exclusive_transaction &trans);

	// swap the contents of this and other.  Must be done under an exclusive_transaction.
	void swap (trans_map &other, exclusive_transaction &trans);

	// find/search methods.
	// find is overloaded to return only committed data, unless the method
	// takes a transaction, in which case the method returns iterators which
	// show committed data and pending changes related to that transaction.
	// based on the MVCC paradigm, find never blocks on a row-level lock.
	iterator find(const K& key);
	const_iterator find(const K& key) const;
	iterator find(const K& key, Transaction &trans);

	iterator lower_bound(const K& key);
	const_iterator lower_bound(const K& key) const;
	iterator lower_bound(const K& key, Transaction &trans);

	iterator upper_bound(const K& key);
	const_iterator upper_bound(const K& key) const;
	iterator upper_bound(const K& key, Transaction &trans);

	// NEW METHODS:
	// acquire a write lock on the row which was found at the indicated iterator position.
	// This method was added for the transactional map.
	// This version will not block, but can throw row_level_lock_contention
	void lock( iterator& i, Transaction &trans );

	// This version will block if a row exists with a pending operation on it.  It may
	// throw a row_deleted_exception if the row referenced by 'i' has a pending delete
	// operation which is committed as it waits.
	//template<class Lock> void lock( iterator& i, Transaction &trans, Lock &heldlock );
	// experimental:
	template <class wait_policy_t> void lock( iterator &i, Transaction &trans, wait_policy_t &wait_policy );

	// Update Method.  Had to be added to provide a transactional update.
	// (It is not possible to update the data directly via the iterators.)
	// The value at i is changed to newVal, within the context of transaction trans.
	// Update returns a reference to the 'V' at the indicated iterator position which the application
	// is then free to modify directly between now and the commit of the transaction.
	V& update( iterator& i, const V& newVal, Transaction &trans );

	// This version will block if a row exists with a pending operation on it.  It may
	// throw a row_deleted_exception if the row referenced by 'i' has a pending delete
	// operation which is committed as it waits.
	template<class wait_policy_t> V& update( iterator& i, const V& newVal, Transaction &trans, wait_policy_t &wait_policy );

	// Returns the lock which guards this map, so that the caller can use standard locking
	// conventions with the map.
	upgradable_mutex_type& mutex() { return _lock; }

	// Returns the condition variable used to signal when row-level locks are released.
	// (Called in the course of commit by Transaction class.)
	condition_type& condition() { return _row_level_lock_released; }

	// Returns the name of this container, as it is known by within the database
	const char *get_name() { return _container_name.c_str(); }

private:

	// while_row_locked generalizes the retry behavior needed to allow a bocking function to
	// be composed by iteratively calling the non-blocking one, and then handling any thrown
	// row_level_lock_contention exception by waiting on the condition variable for transactions
	// to be committed, and then retrying the operation.  The operation being formed is passed in
	// using boost::bind(), and can be any bound function that can be invoked with 2 parameters,
	// an iterator (indicating the entry the operation applies to, if any), and the transaction.
	// The passed wait_policy is consulted when a condition wait is called for to let the caller
	// control exactly how that wait is done.  (i.e. indefinite blocking, block for a maximum duration, etc.)
	template <class bound_method_t, class wait_policy_t>
	typename bound_method_t::result_type  while_row_locked( const bound_method_t &bound_method,
			  iterator &i, Transaction &trans, wait_policy_t &wait_policy );

	// the container's lock.  insert, swap, clear, and commit processing must use
	// a scoped(exclusive) lock, but all others can use a shared lock.
	upgradable_mutex_type _lock;

	// mutex & condition used for row-level locking.  This only needs to be acquired
	// when seeking to acquire/release a row level lock, and only for the duration of
	// the internal operation, so they are exclusively internal
	mutex_type            _row_level_lock;
    condition_type        _row_level_lock_released;

    // implementation methods. must be called with the _row_level_lock mutex held.
    // represent commonality across the various public forms of these operations.
	void erase_i (iterator& i, Transaction &trans);
	typename baseclass::size_type erase_i (const K&, Transaction &trans);
	void erase_i (iterator& i, iterator, Transaction &trans);
	void lock_i( iterator& i, Transaction &trans );
	V& update_i( iterator& i, const V& newVal, Transaction &trans );

	/**
	 * Support for MVCC
	 */
	// Return the pending update (newVal) which corresponds to the current value.
	// This method is only called when a transaction rereads an entry in the map
	// which it already has locked for Update_op.  Otherwise, there is never a need
	// for this overhead.  This is also why I'm not checking for end()s below.
	inline value_type& pendingUpdate( Transaction &trans, typename baseclass::iterator location ) {
		return trans.getContainerSpecificData<pending_change_map_t>(this)->find(location)->second;
	}

	// Record the fact that there is a update of newValue which is to replace
	// the current value at currVal.
	inline value_type addPendingUpdate( Transaction &trans, typename baseclass::iterator location,
			const V& newVal )
	{
		// The transaction retains memory management responsibility for changes.
		pending_change_map_t *changes = trans.getContainerSpecificData<pending_change_map_t>(this);
		pair<typename pending_change_map_t::iterator,bool> result
			= changes->insert( std::make_pair(location, value_type(location->first, newVal)));
		if (result.second==false) {
			result.first->second.second = newVal;
		}
		return result.first->second;
	}

	// The following is in support of the transaction system.
	typename boost::interprocess::basic_string<char, typename std::char_traits<char>, 
		typename Allocator::template rebind<char>::other>  _container_name;

	// _ver_num is used to determine when iterators might have gone invalid after a cond_wait.
	// when iterators are created, they are stamped with the _ver_num on the container at that
	// time.  Thereafter, the iterators can compare their value to the containers current value
	// to determine if they might be invalid.  For methods of trans_map which block waiting on
	// a row lock, this is used to help optimize out the need to refresh the iterators after
	// re-acquiring the mutex.
	uint64_t _ver_num;

	// a record of space in the last checkpoint which has been freed because of
	// erased objects.
	boost::interprocess::map<boost::interprocess::offset_t, std::size_t,
		std::less<boost::interprocess::offset_t>,
		typename Allocator::template rebind<checkpoint_loc_t >::other>
		_freed_checkpoint_space;
	bool _uncheckpointed_clear;

	// save checkpoint
	template <class Database_t>
	void save_checkpoint(Database_t &db, checkpoint_ofstream &checkpoint,
	         transaction_id_t last_checkpoint_lsn );

	// load checkpoint
	void load_checkpoint(checkpoint_ifstream &checkpoint);

	template <class ManagedRegionType, class MapType> friend class stldb::container_proxy;

	friend struct detail::assoc_transactional_operation<trans_map>;
	friend struct detail::map_insert_operation<trans_map>;
	friend struct detail::map_update_operation<trans_map>;
	friend struct detail::map_delete_operation<trans_map>;
	friend struct detail::map_deleted_insert_operation<trans_map>;
	friend struct detail::map_lock_operation<trans_map>;
	friend struct detail::map_clear_operation<trans_map>;
	friend struct detail::map_swap_operation<trans_map>;
};


//!Specialization of container_proxy for trans_map, to deal with
//!map's 2 arg constructor.
template <class ManagedRegionType, class K, class V, class Pred, class Allocator, class mutex_family>
class container_proxy<ManagedRegionType, trans_map<K, V, Pred, Allocator, mutex_family> >
	: public container_proxy_base<ManagedRegionType>
{
public:
	typedef trans_map<K,V,Pred,Allocator,mutex_family> container_type;
	typedef container_proxy_base<ManagedRegionType>  base;
	typedef Allocator  allocator_type;

	container_proxy(const char *name)
		: container_proxy_base<ManagedRegionType>(name)
		, _container(NULL), _db(NULL)
		{ }

	virtual ~container_proxy() { }

	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
		_db = &db;  // late initialization
		allocator_type alloc( db.getRegion().get_segment_manager() );
		_container = db.getRegion().template find_or_construct<container_type>
				(base::container_name.c_str())(Pred(), alloc, this->getName().c_str());
	    return _container;
	}

	virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) {
		switch (opcode) {
			// Note: Lock_op is missing from this list because they are never written to logs.
			case Insert_op: {
				detail::map_insert_operation<container_type> op(*_container);
				op.recover(stream, lsn);
				break;
			}
			case Update_op: {
				detail::map_update_operation<container_type> op(*_container);
				op.recover(stream, lsn);
				break;
			}
			case Delete_op: {
				detail::map_delete_operation<container_type> op(*_container);
				op.recover(stream, lsn);
				break;
			}
			case Deleted_Insert_op: {
				detail::map_deleted_insert_operation<container_type> op(*_container);
				op.recover(stream, lsn);
				break;
			}
			case Clear_op: {
				detail::map_clear_operation<container_type> op(*_container);
				op.recover(stream, lsn);
				break;
			}
			case Swap_op: {
				detail::map_swap_operation<container_type> op(*_container);
				typename boost::interprocess::basic_string<char, typename std::char_traits<char>, typename Allocator::template rebind<char>::other>
						other_container_name(_db->getRegion().get_segment_manager() );
				stream & other_container_name;
				container_type *other = _db->template getContainer<container_type>(
							other_container_name.c_str());
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
			scoped_lock<typename container_type::mutex_type>  lock(
				_container->_row_level_lock);
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
		_container->save_checkpoint(db, checkpoint, last_checkpoint_lsn);
	}
	virtual void load_checkpoint(checkpoint_ifstream &checkpoint)
	{
		_container->load_checkpoint(checkpoint);
	}

private:
	container_type *_container;
	Database<ManagedRegionType> *_db;
};


} // namespace

#ifndef AUTO_TEMPLATE
#include <stldb/containers/trans_map.tcc>
#endif

#endif

