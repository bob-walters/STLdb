/*
 *  acidcachemap.h
 *  ACIDCache
 *
 *  Created by Bob Walters on 2/24/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_CONCURRENT_TRANS_MAP_H
#define STLDB_CONCURRENT_TRANS_MAP_H 1

#include <iterator>
#include <map>
#include <vector>
#include <utility>   // std::pair
#include <functional>

#include <boost/pool/pool_alloc.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>

#include <stldb/exceptions.h>
#include <stldb/container_proxy.h>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/concurrent_trans_assoc_iterator.h>
#include <stldb/containers/iter_less.h>
#include <stldb/containers/detail/map_ops.h>
#include <stldb/sync/bounded_interprocess_mutex.h>
#include <stldb/sync/picket_lock_set.h>
#include <stldb/transaction.h>

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_oarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

namespace stldb {

// Forward declaraton
class Transaction;

namespace concurrent {


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
 * prep a value for update, then provide direct access to the updated value, avoiding copy construction.
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
		  class mutex_family = stldb::bounded_mutex_family,
		  int picket_lock_size = 31>
  class trans_map
	: public boost::interprocess::map<K,TransEntry<V>,Comparator, typename Allocator::template rebind< std::pair<K,TransEntry<V> > >::other >
{
  public:
	// overloads of some typedefs.
	typedef typename Allocator::template rebind<std::pair<K,TransEntry<V> > >::other  base_alloc;

	typedef boost::interprocess::map<K,TransEntry<V>,Comparator,base_alloc>  baseclass;

	typedef K key_type;
	typedef TransEntry<V> mapped_type;
	typedef std::pair<const K, TransEntry<V> > value_type;
	typedef Comparator key_compare;
	typedef Allocator  allocator_type;

	// mutex and condition types used within map.
	typedef typename mutex_family::upgradable_mutex_type upgradable_mutex_type;
	typedef typename mutex_family::mutex_type            mutex_type;
	typedef typename mutex_family::condition_type        condition_type;

	typedef picket_lock_set<value_type, mutex_type, picket_lock_size>  picket_lock_t;

	typedef value_type&        reference;
	typedef const value_type&  const_reference;
	typedef value_type*        pointer;
	typedef const value_type*  const_pointer;

	typedef typename Allocator::size_type size_type;
	typedef typename Allocator::difference_type difference_type;

	friend class trans_assoc_iterator<trans_map, typename baseclass::iterator
		, boost::interprocess::scoped_lock<mutex_type> >;
	friend class trans_assoc_iterator<trans_map, typename baseclass::const_iterator
		, boost::interprocess::scoped_lock<mutex_type> >;

	typedef trans_assoc_iterator<trans_map, typename baseclass::iterator
		, boost::interprocess::scoped_lock<mutex_type> >        iterator;
	typedef trans_assoc_iterator<trans_map, typename baseclass::const_iterator
		, boost::interprocess::scoped_lock<mutex_type> >  const_iterator;

	typedef std::reverse_iterator< iterator >        reverse_iterator;
	typedef std::reverse_iterator< const_iterator >  const_reverse_iterator;

	// container-specific data put onto any transaction which is used to modify the
	// container.  This is a map used to holding uncommitted changes as part of MVCC.
	// The reason std::less is not used for comparison is that it isn't guaranteed to work
	// for std::iterators.  It doesn't work for boost::interprocess::map<>::iterator.
	// In addition to holding pending changes, it also stores whether or not commit and
	// rollback operations can be done with only a shared lock on the container,
	// and finally also holds a vector of rows which need to be locked in that case.
	struct pending_change_map_t : public std::map<typename baseclass::iterator, value_type,
								  iter_less<typename baseclass::iterator> >
	{
		bool exclusive_commit;
		bool exclusive_rollback;

		// A list of all entries changed (insert, update, lock or erase) during the transaction
		// which will therefore require.  Note: Not using value_type or pointer typedefs of
		// containing class because inheriting from map hides those typedefs.
		std::vector< typename trans_map::pointer > _modified_entries;

		// Once locks are acquired on rows in _modified, they are tracked in locks_held during
		// the phases of commit or rollback processing.
		std::vector<mutex_type*> _locks_held;

		pending_change_map_t()
			: exclusive_commit(false), exclusive_rollback(false)
			, _modified_entries(), _locks_held()
			{ }
	};

	// constructors.
	// Note that constructors are not transactional.
	explicit trans_map(const Comparator& comp, const Allocator& alloc, const char *name);

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

	// Returns the mutex that governs the entry at the indicated iterator position
	mutex_type& mutexForRow( const typename baseclass::iterator &i ) {
		return _row_level_locks.mutex(&*i);
	}

	// Declare the proxy as a friend, it has priveledged access to some private methods
//	template <class ManagedRegionType, class K, class V, class Pred, class Allocator, class mutex_family, int picket_lock_size>
//	friend class stldb::container_proxy<ManagedRegionType, trans_map<K, V, Pred, Allocator, mutex_family, picket_lock_size> >;
	template <class ManagedRegionType, class MapType> friend class stldb::container_proxy;

	void print_stats() {
		std::cout << "concurrent::trans_map<> entries: " << this->size();
		_row_level_locks.print_stats();
	}

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

	// mutexes & condition used for row-level locking.  This lock needs to be acquired
	// when seeking to acquire/release a row level lock, and only for the duration of
	// the primitive being invoked.  Not expected to be used externally, except by the Proxy.
	picket_lock_t  _row_level_locks;
	condition_type  _row_level_lock_released;

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
	// the current value at location.
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
		changes->_modified_entries.push_back( reinterpret_cast<typename trans_map::pointer>(&*location) );
		return result.first->second;
	}

	// Record the fact that the entry at the indicated address will need to be relocked during
	// commit or rollback processing to avoid race conditions
	inline void addPendingInsert(Transaction &trans, typename baseclass::iterator location)
	{
		pending_change_map_t *changes = trans.getContainerSpecificData<pending_change_map_t>(this);
		changes->_modified_entries.push_back( reinterpret_cast<typename trans_map::pointer>(&*location) );
		changes->exclusive_rollback = true;
	}

	// Record the fact that the entry at the indicated address will need to be relocked during
	// commit or rollback processing to avoid race conditions
	inline void addPendingErase(Transaction &trans, typename baseclass::iterator location)
	{
		pending_change_map_t *changes = trans.getContainerSpecificData<pending_change_map_t>(this);
		changes->_modified_entries.push_back( reinterpret_cast<typename trans_map::pointer>(&*location) );
		changes->exclusive_commit = true;
	}

	// Record the fact that the entry at the indicated address will need to be relocked during
	// commit or rollback processing to avoid race conditions
	inline void addPendingUnlock(Transaction &trans, typename baseclass::iterator location)
	{
		pending_change_map_t *changes = trans.getContainerSpecificData<pending_change_map_t>(this);
		changes->_modified_entries.push_back( reinterpret_cast<typename trans_map::pointer>(&*location) );
	}

	/**
	 * Support for TransactionalOperations
	 */

	// The following is in support of the transaction system.
	typename boost::interprocess::basic_string<char, typename std::char_traits<char>,
		typename Allocator::template rebind<char>::other>  _container_name;

	// _ver_num is used to determine when iterators might have gone invalid after a cond_wait.
	// when iterators are created, they are stamped with the _ver_num on the container at that
	// time.  Thereafter, the iterators can compare their value to the containers current value
	// to determine if they might be invalid.  For methods of trans_map which block waiting
	// a row lock, this is used to help optimize out the need to refresh the iterators after
	// re-acquiring the mutex.
	uint64_t _ver_num;

	friend struct detail::assoc_transactional_operation<trans_map>;
	friend struct detail::map_insert_operation<trans_map>;
	friend struct detail::map_update_operation<trans_map>;
	friend struct detail::map_delete_operation<trans_map>;
	friend struct detail::map_lock_operation<trans_map>;
	friend struct detail::map_clear_operation<trans_map>;
	friend struct detail::map_swap_operation<trans_map>;

	/**
	 * Private class used as a factory with the transaction infrastructure when previously logged
	 * records are being used to perform Database recovery.
	 */
	struct trans_map_op_factory
	{
		TransactionalOperation* create( TransactionalOperations op_code,
				                        trans_map &map ) {
			switch (op_code) {
			// Note: Lock_op is missing from this list because they are never written to logs.
			case Lock_op:
				return new detail::map_lock_operation<trans_map>(map);
			case Insert_op:
				return new detail::map_insert_operation<trans_map>(map);
			case Update_op:
				return new detail::map_update_operation<trans_map>(map);
			case Delete_op:
				return new detail::map_delete_operation<trans_map>(map);
			case Clear_op:
				return new detail::map_clear_operation<trans_map>(map);
			case Swap_op:
				return new detail::map_swap_operation<trans_map>(map);
			default:
				return NULL; // Will indicate a problem with this code.
			}
		}
	};

	// trans_map contains an instance of this factory.
	trans_map_op_factory  _factory;

	// save checkpoint of map
	void save_checkpoint(std::ostream &out)
	{
		int count = 0;
		static const int entries_per_segment = 10;
		std::pair<key_type,mapped_type> values[entries_per_segment];
		boost_oarchive_t archive(out);

		boost::interprocess::sharable_lock<upgradable_mutex_type> lock(mutex());
		iterator i = begin();
		while (i != end())
		{
			while (i != end() && count < entries_per_segment) {
				values[count++] = *(i++);
			}
			i = end();  // to release entry-level lock held by iterator.
			lock.unlock();
			// The serialization to the output can be done without holding any lock.
			for (int j=0; j<count; j++) {
				// When writing out the committed data for a row which has an existing transaction
				// in progress, the txn_id written out for that row needs to be 0,
				// because on load, that becomes the committed LSN of that row, and we need to make sure
				// that log processing will apply all LSNs found for that row. (In case I try to optimize
				// recovery so that ops are not unnecessarily re-applied.)
				if ( values[j].second.getOperation() != No_op ) {
					values[j].second.unlock(0); // sets LSN ==0, op = No_op
				}
				archive & values[j];
			}
			// set i to next entry.
			lock.lock();
			i = iterator( baseclass::upper_bound(values[count-1].first), this );
			count = 0;
		}
	}

	// load a checkpoint.
	void load_checkpoint(std::istream &in)
	{
		boost_iarchive_t archive(in);
		value_type entry;
		// loading a checkpoint is done as an exclusive operation.  There's no reason for it not to be.
		// Strictly speaking the lock is not required.
		boost::interprocess::scoped_lock<upgradable_mutex_type> lock(mutex());
		while (in) {
			try {
				archive & entry;
				baseclass::insert(entry);
			}
			// Boost::Serialization archives signal eof with an exception.
		    catch (boost::archive::archive_exception& error) {
		        // Make sure that this due to EOF.  Annoyingly, when
		    	// this type of exception is thrown, the eof() bit on 'in'
		    	// won't be set.  We need to try reading one more byte to
		    	// make sure it is set.
		        char tmp;
		        in >> tmp;
		        if (!in.eof())
		          throw error;
		      }
		 }
	}

};


} // namespace concurrent


//!Specialization of container_proxy for trans_map, to deal with
//!map's 2 arg constructor.
template <class ManagedRegionType, class K, class V, class Pred, class Allocator, class mutex_family, int picket_lock_size>
class container_proxy<ManagedRegionType, concurrent::trans_map<K, V, Pred, Allocator, mutex_family, picket_lock_size> >
	: public container_proxy_base<ManagedRegionType>
{
public:
	typedef concurrent::trans_map<K,V,Pred,Allocator,mutex_family,picket_lock_size> container_type;
	typedef typename container_type::pending_change_map_t  pending_change_map_t;
	typedef typename mutex_family::mutex_type  mutex_type;
	typedef container_proxy_base<ManagedRegionType>  base;
	typedef Allocator  allocator_type;

	container_proxy(const char *name)
		: container_proxy_base<ManagedRegionType>(name)
		{ }

	virtual ~container_proxy() { }

	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
		_db = &db;  // late initialization
		allocator_type alloc( db.getRegion().get_segment_manager() );
		_container = db.getRegion().template find_or_construct<container_type>
				(base::container_name.c_str())(Pred(), alloc, this->getName().c_str());
	    return _container;
	}

	virtual void recoverOp(int opcode, boost_iarchive_t &stream) {
		auto_ptr<TransactionalOperation> operation( _container->_factory.create(
				static_cast<TransactionalOperations>(opcode), *_container) );
		// Special form of recover exists for swap(), because the operation is multi-container in nature.
		detail::map_swap_operation<container_type> *swap_op = dynamic_cast<detail::map_swap_operation<container_type>*>(operation.get());
		if (swap_op) {
			typename boost::interprocess::basic_string<char, typename std::char_traits<char>, typename Allocator::template rebind<char>::other>
				other_container_name(_db->getRegion().get_segment_manager());
			stream & other_container_name;
			container_type *other = _db->template getContainer<container_type>(
					other_container_name.c_str());
			swap_op->recover(*other);
		}
		else {
			// All other operations use the std recovery interface
			operation->recover(stream);
		}
	}

	virtual void initializeCommit(Transaction &trans)
	{
		pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
		bool exclusive_commit = data==NULL ? false : data->exclusive_commit;
		if (exclusive_commit)
			_container->mutex().lock(); // exclusive lock
		else {
			if (data) {
				// determine and record locks to be held in data->_locks_held
				_container->_row_level_locks.mutexes( data->_modified_entries.begin(), data->_modified_entries.end(), data->_locks_held);
				// container shared lock first
				_container->mutex().lock_sharable();
				// then get the row-level locks
				for (typename std::vector<mutex_type*>::iterator i = data->_locks_held.begin(); i != data->_locks_held.end(); i++) {
					(*i)->lock();
				}
			}
			else
				_container->mutex().lock_sharable();
		}
	}

	virtual void completeCommit(Transaction &trans)
	{
		pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
		bool exclusive_commit = data==NULL ? false : data->exclusive_commit;
		if (exclusive_commit) {
			_container->_row_level_lock_released.notify_all();
			_container->mutex().unlock();
		}
		else {
			pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
			if (data) {
				_container->_row_level_lock_released.notify_all();
				for (typename std::vector<mutex_type*>::iterator i = data->_locks_held.begin(); i != data->_locks_held.end(); i++) {
					(*i)->unlock();
				}
			}
			_container->mutex().unlock_sharable();
		}
	}
	virtual void initializeRollback(Transaction &trans)
	{
		pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
		bool exclusive_rollback = data==NULL ? false : data->exclusive_rollback;
		if (exclusive_rollback)
			_container->mutex().lock();
		else {
			if (data) {
				_container->_row_level_locks.mutexes( data->_modified_entries.begin(), data->_modified_entries.end(), data->_locks_held);
				_container->mutex().lock_sharable();
				for (typename std::vector<mutex_type*>::iterator i = data->_locks_held.begin(); i != data->_locks_held.end(); i++) {
					(*i)->lock();
				}
			}
			else
				_container->mutex().lock_sharable();
		}
	}

	virtual void completeRollback(Transaction &trans)
	{
		pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
		bool exclusive_rollback = data==NULL ? false : data->exclusive_rollback;
		if (exclusive_rollback) {
			_container->_row_level_lock_released.notify_all();
			_container->mutex().unlock();
		}
		else {
			pending_change_map_t *data = trans.getContainerSpecificData<pending_change_map_t>(_container);
			if (data) {
				_container->_row_level_lock_released.notify_all();
				for (typename std::vector<mutex_type*>::iterator i = data->_locks_held.begin(); i != data->_locks_held.end(); i++) {
					(*i)->unlock();
				}
			}
			_container->mutex().unlock_sharable();
		}
	}

	virtual void save_checkpoint(std::ostream &out)
	{
		_container->save_checkpoint(out);
	}
	virtual void load_checkpoint(std::istream &in)
	{
		_container->load_checkpoint(in);
	}

private:
	container_type *_container;
	Database<ManagedRegionType> *_db;
};

} // namespace stldb

#ifndef AUTO_TEMPLATE
#include <stldb/containers/concurrent_trans_map.tcc>
#endif

#endif

