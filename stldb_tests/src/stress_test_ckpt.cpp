//============================================================================
// Name        : stldb-tests.cpp
// Author      : Bob Walters
// Version     :
// Copyright   : Copyright 2009 by Bob Walters
// Description : Hello World in C++, Ansi-style
//============================================================================

/**
 * This test attempts to bring a comprehensive, "real-world" load to a set
 * of databases, each of which, in turn will contain multiple maps.  Work
 * is done, transactions are logged, and occasionally, the database is
 * set_invalid to force all connected process to disconnect and perform appropriate
 * recovery procedures, which will include restoring the region from checkpoints
 * and logs.  Multiple instances of this process can run simultaneously to
 * also test the interaction of these processes across the shared region.
 */

#include "partitioned_test_database.h"
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/allocators/variable_node_allocator.h>
#include <stldb/containers/rbtree.h>
#include <stldb/timing/timer.h>
#include <stldb/sync/wait_policy.h>
#include <stldb/checkpoint_manager.h>

#include <boost/interprocess/indexes/flat_map_index.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/cached_node_allocator.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread.hpp>

namespace intrusive = boost::intrusive;
namespace bi = boost::interprocess;

using boost::thread;
using boost::shared_lock;
using boost::unique_lock;
using boost::lock_guard;
using bi::sharable_lock;
using stldb::Transaction;
using stldb::scoped_allocation;

static const size_t megabyte = 1024*1024;
static const size_t clump_size = 8*megabyte;

typedef bi::basic_managed_mapped_file<
	char,
	bi::rbtree_best_fit<stldb::bounded_mutex_family, bi::offset_ptr<void> >,
	bi::flat_map_index
> stldb_managed_mapped_file;

// Allocator of char in shared memory, with support for default constructor
typedef stldb::region_or_heap_allocator< bi::allocator<
		char, stldb_managed_mapped_file::segment_manager> > shm_char_allocator_t;

typedef stldb::region_or_heap_allocator< stldb::variable_node_allocator<
		char, stldb_managed_mapped_file::segment_manager, clump_size> > clustered_char_allocator_t;

// non-reference counted string (minimize copying)
typedef bi::basic_string<char, std::char_traits<char>, shm_char_allocator_t>		shm_string;
typedef bi::basic_string<char, std::char_traits<char>, clustered_char_allocator_t>  clustered_shm_string;

typedef intrusive::void_pointer< bi::offset_ptr<void> >  shm_void_pointer;

// Data type to be used as T for MapType (rbtree<T>)
class node_t : public intrusive::set_base_hook< shm_void_pointer >, public stldb::trans_set_hook {
public:
	clustered_shm_string first;
	shm_string second;
	
	// compiler generated default constructor, copy constructor, destructor, and operator= used.
	node_t() : first(), second() { }
	node_t(const char *s1) : first(s1) { }
	node_t(const char *s1, const char *s2) : first(s1), second(s2) { }
	
	bool operator==(const node_t &rarg) const { return first == rarg.first; }
	bool operator<(const node_t &rarg) const { return first < rarg.first; }
	bool operator>(const node_t &rarg) const { return first > rarg.first; }
};

// A node allocator for node_t, which uses the bulk allocation mechanism of boost::interprocess
typedef bi::cached_node_allocator<node_t,stldb_managed_mapped_file::segment_manager>  node_allocator_t;

// A Comparator used with rbtree::find() calls, to permit the search for a node_t
// based on a string (first).
struct node_key_compare {
	bool operator()(const clustered_shm_string &key, const node_t &node) const {
		return (key < node.first);
	}
	bool operator()(const node_t &node, const clustered_shm_string &key) const {
		return (node.first < key);
	}
};

// Finally, a transactional rbtree, composed of node_t nodes
typedef stldb::rbtree<node_t, node_allocator_t> MapType;

typedef PartitionedTestDatabase<stldb_managed_mapped_file,MapType>  ParttionedDbType;


// Config parameters (set in stress_test())
static int g_num_db;
static int g_maps_per_db;
static int g_max_key;
static int g_avg_val_length;  // real langth is between 50% and 150% of this value.

static std::string g_db_dir;
static std::string g_checkpoint_dir;
static std::string g_log_dir;

static boost::posix_time::time_duration  g_max_wait;
static boost::posix_time::time_duration  g_checkpoint_interval;
static boost::posix_time::time_duration  g_invalidation_interval;

// Databases.  We need the shared mutex to coordinate re-opening of the
// databases in the event one of the starts throwing run_recovery exceptions
static boost::shared_mutex  g_db_mutex[100];
static ParttionedDbType *g_databases[100];
static node_allocator_t *g_allocators[100];

// validates the memory allocation of the key/values within map, making sure that they
// are all pointing to addresses allocated within the db shared region.
static void validate( ParttionedDbType *db )
{
	void *start = db->getRegion().get_address();
	void *end = (char*)start + db->getRegion().get_size();
	cout << "Region start: " << start << endl << "Region end: " << end << endl;
	int errors = 0;

	for (int i=0; i<g_maps_per_db; i++) {
		cout << "Scanning map " << i << endl;
		MapType* map = db->getMap(i);

		MapType::iterator iter = map->begin();
		unsigned int count = 0;
		while (iter != map->end()) {
			const char *keybuff = iter->first.c_str();
			const char *valbuff = iter->second.c_str();
			if (keybuff < start || keybuff > end) {
				//cout << "Error: found key " << i->first << " with buffer address " << (void*)keybuff << endl;
				errors++;
			}
			if (valbuff < start || valbuff > end) {
				//cout << "Error: found value with buffer address " << (void*)valbuff << endl;
				errors++;
			}
			count++;
			iter++;
		}
		if (count != map->size()) {
			cout << "size discrepancy noted.  read " << count << " rows.  map->size()==" << map->size() << endl;
			errors++;
		}
		if (errors==0) {
			cout << "No errors detected" << endl;
		}
		else {
			cout << errors << " errors detected" << endl;
		}
	}
}

// Wipes out all data on disk for all databases up to db_max.
void cleanUp(int db_max)
{
	for (int i=0; i<db_max; i++) {
		// We need to open the database, apparently.
		ostringstream str;
		str << "stressDb_" << i;
		std::string dbname( str.str() );

		// Remove any previous database instance.
		Database<stldb_managed_mapped_file>::remove(dbname.c_str(), g_db_dir.c_str(),
				g_checkpoint_dir.c_str(), g_log_dir.c_str());
	}
}

// Returns the specified database, opening it if necessary.
// A shared_lock is returned with the databases, which determines
// how long the pointer returned is guaranteed to remain valid.
ParttionedDbType*
getDatabase(int db_num, boost::shared_lock<boost::shared_mutex> &lock )
{
	// result holds a lock on d_db_mutex[db_num] after construction
	ParttionedDbType* result = NULL;
	boost::shared_lock<boost::shared_mutex>  slock(g_db_mutex[db_num]);

	while (g_databases[db_num] == NULL) {
		// We'll need an exclusive lock to construct the DB.
		slock.unlock();
		boost::unique_lock<boost::shared_mutex> excl_lock(g_db_mutex[db_num]);

		// Double check now that we're exclusive.  It could have been
		// constructed by another thread while we waited to get 'exholder'
		if (g_databases[db_num] == NULL) {
			
			ostringstream str;
			str << "stressDb_" << db_num;
			std::string dbname( str.str() );
		
			ostringstream str2;
			str << g_checkpoint_dir << "/stressDb_" << db_num << "_ckpt";
			std::string ckptname( str2.str() );
		
			g_databases[db_num] = new ParttionedDbType(
				dbname.c_str(), g_db_dir.c_str(),
				g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );
			
//			g_ckpt_managers[db_num] = new stldb::checkpoint_manager<stldb_managed_mapped_file>(
//				g_databases[db_num]->getDatabase()->getRegion(), ckptname.c_str() );
			
			g_allocators[db_num] = new node_allocator_t(g_databases[db_num]->getDatabase()->getRegion().get_segment_manager());
		}
		excl_lock.unlock();
		slock.lock();
	}
	// downgrade upgradable lock to shared lock, and return to sender.
	lock = slock.move();
	result = g_databases[db_num];

	// move semantics should result in lock going with return value.
	return result;
}

// This method is used for clean-up at the end of a run.
void
closeDatabase(int db_num)
{
	// We'll need an exclusive lock to construct the DB.
	lock_guard<boost::shared_mutex> exholder(g_db_mutex[db_num]);

	// Double check now that we're exclusive.  It could have been
	// constructed by another thread while we waited to get 'exholder'
	if (g_databases[db_num] != NULL) {
		// we now perform recovery processing.
		validate(g_databases[db_num]);

		delete g_allocators[db_num];
		g_allocators[db_num] = NULL;

		delete g_databases[db_num];
		g_databases[db_num] = NULL;
		//delete g_ckpt_managers[db_num];
		//g_ckpt_managers[db_num] = NULL;
	}
}

// Generate a random value for the indicated key_no.  The value will
// consist of bytes whose values cover all byte values (0..255), and
// it is generated with a convention that allows later validation.
void randomValue(int key_no, shm_string& result) {
	int length = static_cast<int>(::rand() * (((double)g_avg_val_length) / (double)RAND_MAX)) + (g_avg_val_length/2);
	if (length < 10) length = 10;
	int sum = 0;
	result.resize(length, char(0));
	for (int i=0; i<length-4; i++) {
		result[i] = (char)(length + key_no + i);
		sum += result[i];
	}
	result[length-4] = char( key_no % 255 );
	result[length-3] = char( key_no / 255 );
	result[length-2] = char( sum % 255 );
	result[length-1] = char( sum / 255 );
}

// verify the value passed as being a valid value for the key_no indicated, as
// generated previously by randomValue.  This helps to protect us against errors
// in which the entry seems corrupt.
bool checkValue( int key_no, shm_string& value ) {
	int length = value.size();
	int sum = 0;
	for (int i=0; i<length-4; i++) {
		if (value[i] != (char)(length + key_no + i))
			return false;
		sum += value[i];
	}
	if ( value[length-4] != char( key_no % 255 ) ) return false;
	if ( value[length-3] != char( key_no / 255 ) ) return false;
	if ( value[length-2] != char( sum % 255 ) ) return false;
	if ( value[length-1] != char( sum / 255 ) ) return false;
	return true;
}


void recoverDatabase( int db_num,
		ParttionedDbType* db,
		shared_lock<boost::shared_mutex> &lock )
{
	// We still have out shared lock, and db*.  Drop the shared lock,
	// and get an exclusive one.
	lock.unlock();

	// We'll need an exclusive lock to construct the DB.
	lock_guard<boost::shared_mutex> exholder(g_db_mutex[db_num]);

	// Double check now that we're exclusive.  It could have been
	// constructed by another thread while we waited to get 'exholder'
	if (g_databases[db_num]==db) {
		// we now perform recovery processing.
		ParttionedDbType *db = g_databases[db_num];

		db->close();

		ostringstream str;
		str << "stressDb_" << db_num;
		std::string dbname( str.str() );

		// During construction, recovery should occur.
		g_databases[db_num] = new ParttionedDbType(
			dbname.c_str(), g_db_dir.c_str(),
			g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );
		
		ostringstream str2;
		str << g_checkpoint_dir << "/stressDb_" << db_num << "_ckpt";
		std::string ckptname( str2.str() );
		
//		delete g_ckpt_managers[db_num];
//		g_ckpt_managers[db_num] = new stldb::checkpoint_manager<stldb_managed_mapped_file>(
//			g_databases[db_num]->getDatabase()->getRegion(), ckptname.c_str() );

		delete g_allocators[db_num];
		g_allocators[db_num] = new node_allocator_t(g_databases[db_num]->getDatabase()->getRegion().get_segment_manager());

		delete db;
	}
}

// Operation - perform on transaction of some sort against the map.
// The operation performed is one test loop iteration
class trans_operation
{
public:
	virtual void operator()()=0;
	virtual ~trans_operation() { }
};

// One of the possible transactional operations which is invoked.
// Does some standard read/writes to a couple of maps within an
// environment, and then commits.
class CRUD_transaction : public trans_operation
{
public:
	CRUD_transaction(int ops_per_txn)
		: txnSize(ops_per_txn)
		, transactions(0)
		, finds(0)
		, founds(0)
		, inserts(0)
		, inserts_dupkey(0)
		, updates(0)
		, updates_row_removed(0)
		, deletes(0)
		, deletes_row_removed(0)
		, lock_timeouts(0)
		, db_invalids(0)
		, recovery_needed_exceptions(0)
		{ }

	virtual ~CRUD_transaction() {
		this->print_stats();
	}

	// Allocate an insert an entry into the map.
	void insert_node(Transaction *txn, MapType *map, node_allocator_t &allocator, 
					 int key_no, clustered_shm_string &key_in_heap,
					 bi::sharable_lock<bi::interprocess_upgradable_mutex> &held_lock) 
	{
		stldb::timer t("insert_node");
		
		// generate a pseudo-random value for key.  3 allocations in shm.
		node_t *new_node = new(&* allocator.allocate(1)) node_t();
		new_node->first = key_in_heap;
		randomValue(key_no, new_node->second );
		
		// insert the new node.
		std::pair<MapType::iterator, bool> result;
		try {
			// If we discover an uncommitted insert with the same key, block until that transaction resolves.
			//stldb::bounded_wait_policy<scoped_lock<bi::interprocess_upgradable_mutex> >
			//	wait_with_timeout(ex_lock_holder, g_max_wait);
			
			// insertion requires a short-lived exclusive lock on the map
			held_lock.unlock();
			bi::scoped_lock<bi::interprocess_upgradable_mutex> excl_lock(map->mutex());
			
			result = map->insert_unique( *new_node, *txn );
			
			held_lock.swap( bi::sharable_lock<bi::interprocess_upgradable_mutex>(bi::move(excl_lock)));
		}
		catch (stldb::row_level_lock_contention &) {
			result.second = false;
			allocator.destroy(new_node);
			allocator.deallocate(new_node,1);
			// continue...
		}
		
		if (result.second)
			inserts++;
		else {
			// this can happen because or race condition with another inserter
			// i.e. other inserting thread gets exclusive lock first.
			inserts_dupkey++;
		}
	}

	void update_node(Transaction *txn, MapType *map, node_allocator_t &allocator, 
					 MapType::iterator entry, int key_no)
	{
		stldb::timer t("update_node");
		
		// generate a pseudo-random value for key.  3 allocations in shm.
		node_t *new_node = new(&*allocator.allocate(1)) node_t();
		new_node->first = entry->first;
		randomValue(key_no, new_node->second );
		
		try {
			// If we discover an uncommitted insert with the same key, block until that transaction resolves.
			//stldb::bounded_wait_policy<scoped_lock<bi::interprocess_upgradable_mutex> >
			//	wait_with_timeout(ex_lock_holder, g_max_wait);
			
			map->update( entry, *new_node, *txn );
			updates++;
			return;
		}
		catch (stldb::row_level_lock_contention &) {
			// another transaction deleted the row while we were trying to
			// update it.
			updates_row_removed++;
		}
		catch (stldb::row_deleted_exception &) {
			// another transaction deleted the row while we were trying to
			// update it.
			updates_row_removed++;
		}
		allocator.destroy(new_node);
		allocator.deallocate(new_node,1);
		// continue...		
	}
	
	void erase_node(Transaction *txn, MapType *map, node_allocator_t &allocator, 
					MapType::iterator entry)
	{
		stldb::timer t("erase_node");

		// delete the entry
		try {
			map->erase_and_dispose(entry, *txn);
			deletes++;
		}
		catch (stldb::row_level_lock_contention &) {
			// another transaction deleted the row while we were trying to
			// update it.
			deletes_row_removed++;
		}
		catch (stldb::row_deleted_exception &) {
			// another transaction deleted the row while we were trying to
			// update it.
			deletes_row_removed++;
		}
	}
	
	virtual void operator()() {
		stldb::timer t("std_transactional_operation");

		// First things first: randomly determine an environment which we will use:
		// randomly pick one operation, and execute it.
		int db_no = static_cast<int>((::rand() * (double)g_num_db) / RAND_MAX);
		db_no = (db_no >= g_num_db) ? g_num_db-1 : db_no ;

		shared_lock<boost::shared_mutex> lock;
		ParttionedDbType* db = getDatabase(db_no, lock);
		node_allocator_t *allocator = g_allocators[db_no];
		
		typedef ParttionedDbType::db_type db_type;

		Transaction *txn = NULL;
		try {
			// We'll need to begin a transaction.  This could throw recovery_needed
			txn = db->beginTransaction();

			// Do txnSize operations.
			for (int i=0; i<txnSize; i++)
			{
				// Now randomly determine a map within the database to get.  Every map
				// can contain every potential key value.
				int map_no = static_cast<int>((::rand() * (double)g_maps_per_db) / RAND_MAX);
				MapType* map = db->getMap(map_no);

				// We're going to do some random op, on a random key, within
				// one of the maps within this database.  Normally, I would try to sort the
				// changes to avoid deadlocks.  But I'm not doing that here.  Instead, I'll
				// watch for LockTimoutExceptions, and handle them by aborting the transaction.
				int key_no = static_cast<int>((::rand() * (double)g_max_key) / RAND_MAX);
				
				// fabricate an entries key...
				ostringstream keystream;
				keystream << "TestKey" << key_no;
				clustered_shm_string key( keystream.str().c_str() );

				{ // lock scope (shared lock).  I only lock per operation here.
					stldb::timer maptimer("sharable_lock(map->mutex) scope");
					bi::sharable_lock<bi::interprocess_upgradable_mutex> lock_holder(map->mutex());

					// Now...  for the indicated key, read it to see if it is present in the map.
					// We always do this, so 50% of all CRUD traffic is reads, although that does include
					// reads of rows which might not exist yet.
					// find never blocks (or it isn't supposed to) so I don't handle Lock errors
					MapType::iterator entry = map->find(key, node_key_compare(), *txn);
					finds++;

					if (entry == map->end()) {
						// establish allocation scope against db->getRegion()'s segment manager, for
						// the sake of default shm_string constructors called within this method.
						stldb::scoped_allocation<db_type::RegionType::segment_manager> 
							alloc_scope(db->getRegion().get_segment_manager());
						
						insert_node(txn, map, *allocator, key_no, key, lock_holder);
					}
					else {
						// verify that we have read a valid value (find is working)
						assert( checkValue(key_no, entry->second) );
						founds++;

						// With the found row, let's do one of 3 things. a) nothing, b) update it,
						// c) delete it. (33.3% chance of each.)
						int operation = static_cast<int>((::rand() * (double)3.0) / RAND_MAX);

						// establish allocation scope against db->getRegion()'s segment manager, for
						// the sake of default shm_string constructors called within this method.
						stldb::scoped_allocation<db_type::RegionType::segment_manager> 
							alloc_scope(db->getRegion().get_segment_manager());
						
						// stldb::bounded_wait_policy<sharable_lock<boost::interprocess::interprocess_upgradable_mutex> >
						// wait_with_timeout(lock_holder, g_max_wait);
						
						switch (operation) {
						case 1:
							// update the entry
							update_node(txn, map, *allocator, entry, key_no); 
							break;
						case 2:
							// erase the entry
							erase_node(txn, map, *allocator, entry);
							break;
						default: // case 0:
							// do nothing - this was a read-only operation.
							break;
						}
					}
				}
			} // end of for loop

			db->commit( txn );

		}
		catch( stldb::lock_timeout_exception & ) {
			// probably means I have deadlocked with another thread.
			// so rollback to release my locks.
		        cout << "Received lock_timeout exception" << endl;
			lock_timeouts++;
			bool db_ok = false;
			try {
				db->rollback(txn);
				// make sure this isn't because someone has screwed up...
				db_ok = db->getDatabase()->check_integrity();
			}
			catch ( stldb::recovery_needed & ) {
				cout << "Received recovery_needed exception during rollback following lock_timeout exception" << endl;
				recovery_needed_exceptions++;
			}
			if (!db_ok)
				db_invalids++;
		}
		catch ( stldb::recovery_needed & ) {
			// We have found ourselves to be using a PartitionedTestDatabase which needs
			// recovery processing.  In other words, it has to be cloed and reopened.
			cout << "Received recovery_needed exception" << endl;
			recovery_needed_exceptions++;
			// no point in doing any db->rollback(txn) as it would be ignored at this point.
			recoverDatabase(db_no, db, lock);
		}
		transactions++;
	}

	void print_stats() {
		cout << "Transactions:      " << transactions << endl
			 << "find()s:           " << finds << endl
			 << "   entries found:  " << founds << endl
			 << "   no entry found: " << (finds-founds) << endl
			 << "inserts:           " << inserts << endl
			 << "   dup_key result: " << inserts_dupkey << endl
			 << "updates:           " << updates << endl
			 << "   row_deleted:    " << updates_row_removed << endl
			 << "erase:             " << deletes << endl
			 << "   row_deleted:    " << deletes_row_removed << endl
			 << "lock timeouts:     " << lock_timeouts << endl
			 << "   db_invalids:    " << db_invalids << endl
			 << "recovery_needed:   " << recovery_needed_exceptions << endl;
	}

private:
	int txnSize;

	int transactions;
	int finds;
	int founds;

	int inserts;
	int inserts_dupkey;
	int updates;
	int updates_row_removed;
	int deletes;
	int deletes_row_removed;
	int lock_timeouts;
	int db_invalids;
	int recovery_needed_exceptions;
};

		
// One of the possible transactional operations which is invoked.
// Does some standard read/writes to a couple of maps within an
// environment, and then commits.
class checkpoint_operation  : public trans_operation
{
public:
	checkpoint_operation(int inter)
		: interval( inter )
		{ }

	virtual ~checkpoint_operation() { }

	virtual void operator()() {
		while (true) {
#ifdef BOOST_INTERPROCESS_WINDOWS
			Sleep(interval);
#else
			sleep(interval);
#endif
			for (int i=0; i<g_num_db; i++) {
				try {
					shared_lock<boost::shared_mutex> lock;
					ParttionedDbType* db = getDatabase(i, lock);
					db->getDatabase()->checkpoint_new();

					/*
					 * TODO - This was SEGVing based on my incomplete checkpoint routine
					std::vector<boost::filesystem::path> logs = db->getDatabase()->get_archivable_logs();
					for (std::vector<boost::filesystem::path>::const_iterator i = logs.begin();
							i != logs.end(); i++ )
					{
						try {
							boost::filesystem::remove( *i );
							cout << "Archived file: " << *i << endl;
						}
						catch (...) {
							cerr << "Warning: can't remove (clean-up) log file: " << *i;
						}
					}
					*/
				}
				catch ( stldb::recovery_needed & ) {
					// We have found ourselves to be using a PartitionedTestDatabase which needs
						// recovery processing.  In other words, it has to be cloed and reopened.
					cout << "Received recovery_needed exception during checkpoint" << endl;
				}
			}
		}
	}
private:
	int interval;
};


// Set_invalid(true) on a random database, causing needs_recovery_exception on all
// threads using it.
class set_invalid_operation  : public trans_operation
{
public:
	set_invalid_operation(int inter)
		: interval( inter )
		{ }

	virtual ~set_invalid_operation() { }

	virtual void operator()() {
		while (true) {
#ifdef BOOST_INTERPROCESS_WINDOWS
			Sleep(interval);
#else
			sleep(interval);
#endif

			int db_no = static_cast<int>((::rand() * (double)g_num_db) / RAND_MAX);
			db_no = (db_no >= g_num_db) ? g_num_db-1 : db_no ;

			ParttionedDbType *db, *db2;
			{
				shared_lock<boost::shared_mutex> lock;
				db = getDatabase(db_no, lock);
				db->getDatabase()->set_invalid(true);
				db2 = db;
			}
			// wait for one of the CRUD_transaction threads to restore db before next sleep.
			while (db == db2) {
				boost::thread::yield();
				shared_lock<boost::shared_mutex> lock;
				db2 = getDatabase(db_no, lock);
			}
		}
	}
private:
	int interval;
};



// A callable (per boost::thread concept) which has the executed thread
// perform a series of loop_size operations from among the set provided via
// add functions.  operations to be executed are chosen randomly from among
// the set passed.
class test_loop {
public:
	test_loop(int loop_size)
		: loop_size(loop_size)
		, _ops(), _max_freq(0)
		{ }

	// Adds a trans_operation to the set of ops being performed, which will
	// be executed with the specified frequency.
	void add(trans_operation* op, int frequency ) {
		_max_freq += frequency;
		_ops.insert( std::make_pair<const int,trans_operation*>( _max_freq, op ) );
	}

	// Run through loop_size randomly
	void operator()() {
		if (_ops.size() == 1) {
			// fast loop
			trans_operation &op = *(_ops[_max_freq]);
			for (int i=0; i<loop_size; i++) {
				op();
			}
		}
		else {
			// slower loop, randomly pick work to do.
			for (int i=0; i<loop_size; i++) {
				// randomly pick one operation, and execute it.
				int r = static_cast<int>((::rand() * (double)_max_freq) / RAND_MAX);
				(*_ops.lower_bound(r)->second)();
			}
		}
	}

private:
	int loop_size;
	std::map<int,trans_operation*> _ops;
	int _max_freq;
};

// run-time configuration in the form of name/value pairs.
properties_t properties;

int main(int argc, const char* argv[])
{
	properties.parse_args(argc, argv);

	stldb::timer_configuration config;
	config.enabled_percent = properties.getProperty("timing_percent", 0.0);
	config.report_interval_seconds = properties.getProperty("report_freq", 60);
	config.reset_after_print = properties.getProperty("report_reset", true);
	stldb::timer::configure( config );
	stldb::tracing::set_trace_level(stldb::fine_e);

	const int thread_count = properties.getProperty("threads", 4);
	const int loopsize = properties.getProperty("loopsize", 100);
	const int ops_per_txn = properties.getProperty("ops_per_txn", 10);

	g_db_dir = properties.getProperty<std::string>("rootdir", std::string("."));
	g_checkpoint_dir = g_db_dir + "/checkpoint";
	g_log_dir = g_db_dir + "/log";

	g_num_db = properties.getProperty("databases", 4);
	g_maps_per_db = properties.getProperty("maps_per_db", 4);
	g_max_key = properties.getProperty("max_key", 10000);
	g_avg_val_length = properties.getProperty("avg_val_length", 1000);
	g_max_wait = boost::posix_time::millisec(properties.getProperty("max_wait", 10000));
	g_checkpoint_interval = boost::posix_time::millisec(properties.getProperty("checkpoint_interval", 0));
	g_invalidation_interval = boost::posix_time::millisec(properties.getProperty("invalidation_interval", 0));

	// The loop that the running threads will execute
	test_loop loop(loopsize);
	CRUD_transaction main_op(ops_per_txn);
	loop.add( &main_op, 100 );

	// Start the threads which are going to run operations:
	boost::thread **workers = new boost::thread *[thread_count];
	for (int i=0; i<thread_count; i++) {
		workers[i] = new boost::thread( loop );
	}
	// start a thread which does periodic checkpointing
	boost::thread *checkpointor = NULL, *invalidator = NULL;
	if ( g_checkpoint_interval.seconds() > 0 ) {
		checkpointor = new boost::thread( checkpoint_operation(g_checkpoint_interval.seconds()) );
	}
	if ( g_invalidation_interval.seconds() > 0 ) {
		// start a thread which does periodic invalidation, forcing recovery to be done
		invalidator = new boost::thread( set_invalid_operation(g_invalidation_interval.seconds()) );
	}

	// Support the option of writing to an indicator file once all databses have been opened,
	// confirming to watching processes/scripts that database open/recovery has finished.
	std::string indicator_filename = properties.getProperty<std::string>("indicator_filename", std::string());
	if (!indicator_filename.empty()) {
		for (int i=0; i<g_num_db; i++) {
			shared_lock<boost::shared_mutex> lock;
			getDatabase(i, lock);
		}
		std::ofstream indf(indicator_filename.c_str());
	}
	
	// now await their completion
	for (int i=0; i<thread_count; i++) {
		workers[i]->join();
		delete workers[i];
	}

	// close the databases
	for (int i=0; i<g_num_db; i++) {
		closeDatabase(i);
	}

	// final print timing stats (if requested)
	if (config.enabled_percent > 0.0)
		stldb::time_tracked::print(std::cout, true);

	return 0;
}
