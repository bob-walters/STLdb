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
#include <stldb/allocators/allocator_gnu.h>
#include <stldb/timing/timer.h>
#include <stldb/sync/wait_policy.h>

#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread.hpp>

using stldb::Transaction;
using stldb::scoped_allocation;

#ifdef __GNUC__
// String in shared memory, ref counted, copy-on-write qualities based on GNU basic_string
#	include <stldb/containers/gnu/basic_string.h>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;
	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  shm_string;
#else
// String in shared memory, based on boost::interprocess::basic_string
#	include <boost/interprocess/containers/string.hpp>
	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> shm_string;
#endif

// A node allocator for the map, which uses the bulk allocation mechanism of boost::interprocess
typedef boost::interprocess::cached_node_allocator<std::pair<const shm_string, shm_string>,
	managed_mapped_file::segment_manager>  trans_map_allocator;

// A transactional map, using std ref counted strings.
typedef stldb::trans_map<shm_string, shm_string, std::less<shm_string>,
	trans_map_allocator>  MapType;

using boost::interprocess::sharable_lock;

using boost::thread;
using boost::shared_lock;
using boost::unique_lock;
using boost::lock_guard;



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
static PartitionedTestDatabase<managed_mapped_file,MapType> *g_databases[100];

// validates the memory allocation of the key/values within map, making sure that they
// are all pointing to addresses allocated within the db shared region.
static void validate( PartitionedTestDatabase<managed_mapped_file,MapType> *db )
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
			if (valbuff < start || keybuff > end) {
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
		Database<managed_mapped_file>::remove(dbname.c_str(), g_db_dir.c_str(),
				g_checkpoint_dir.c_str(), g_log_dir.c_str());
	}
}

// Returns the specified database, opening it if necessary.
// A shared_lock is returned with the databases, which determines
// how long the pointer returned is guaranteed to remain valid.
PartitionedTestDatabase<managed_mapped_file,MapType>*
getDatabase(int db_num, boost::shared_lock<boost::shared_mutex> &lock )
{
	// result holds a lock on d_db_mutex[db_num] after construction
	PartitionedTestDatabase<managed_mapped_file,MapType>* result = NULL;
	boost::upgrade_lock<boost::shared_mutex>  ulock( g_db_mutex[db_num] );

	while (g_databases[db_num] == NULL) {
		// We'll need an exclusive lock to construct the DB.
		boost::upgrade_to_unique_lock<boost::shared_mutex> exlock(ulock);

		// Double check now that we're exclusive.  It could have been
		// constructed by another thread while we waited to get 'exholder'
		ostringstream str;
		str << "stressDb_" << db_num;
		std::string dbname( str.str() );
		if (!g_databases[db_num])
			g_databases[db_num] = new PartitionedTestDatabase<managed_mapped_file,MapType>(
				dbname.c_str(), g_db_dir.c_str(),
				g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );
	}
	// downgrade upgradable lock to shared lock, and return to sender.
	lock = ulock.move();
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

		delete g_databases[db_num];
		g_databases[db_num] = NULL;
	}
}

// Generate a random value for the indicated key_no.  The value will
// consist of bytes whose values cover all byte values (0..255), and
// it is generated with a convention that allows later validation.
shm_string randomValue(int key_no) {
	int length = static_cast<int>(::rand() * (((double)g_avg_val_length) / (double)RAND_MAX)) + (g_avg_val_length/2);
	if (length < 10) length = 10;
	int sum = 0;
	shm_string result(length, char(0));
	for (int i=0; i<length-4; i++) {
		result[i] = (char)(length + key_no + i);
		sum += result[i];
	}
	result[length-4] = char( key_no % 255 );
	result[length-3] = char( key_no / 255 );
	result[length-2] = char( sum % 255 );
	result[length-1] = char( sum / 255 );
	return result;
}

// verify the value passed as being a valid value for the key_no indicated, as
// generated previously by randomValue.  This helps to protect us against errors
// in which the entry seems corrupt.
bool checkValue( int key_no, shm_string value ) {
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
		PartitionedTestDatabase<managed_mapped_file,MapType>* db,
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
		PartitionedTestDatabase<managed_mapped_file,MapType> *db = g_databases[db_num];

		db->close();

		ostringstream str;
		str << "stressDb_" << db_num;
		std::string dbname( str.str() );

		// During construction, recovery should occur.
		g_databases[db_num] = new PartitionedTestDatabase<managed_mapped_file,MapType>(
			dbname.c_str(), g_db_dir.c_str(),
			g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );

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

	virtual void operator()() {
		stldb::timer t("std_transactional_operation");

		// First things first: randomly determine an environment which we will use:
		// randomly pick one operation, and execute it.
		int db_no = static_cast<int>(::rand() * (((double)g_num_db) / (double)RAND_MAX));

		shared_lock<boost::shared_mutex> lock;
		PartitionedTestDatabase<managed_mapped_file,MapType>* db = getDatabase(db_no, lock);

		typedef PartitionedTestDatabase<managed_mapped_file,MapType>::db_type db_type;

		Transaction *txn = NULL;
		try {
			// We'll need to begin a transaction.  This could throw recovery_needed
			txn = db->beginTransaction();

			// Do txnSize operations.
			for (int i=0; i<txnSize; i++)
			{
				// Now randomly determine a map within the database to get.  Every map
				// can contain every potential key value.
				int map_no = static_cast<int>(::rand() * (((double)g_maps_per_db) / (double)RAND_MAX));
				MapType* map = db->getMap(map_no);

				// We're going to do some random op, on a random key, within
				// one of the maps within this database.  Normally, I would try to sort the
				// changes to avoid deadlocks.  But I'm not doing that here.  Instead, I'll
				// watch for LockTimoutExceptions, and handle them by aborting the transaction.
				int key_no = static_cast<int>(::rand() * (((double)g_max_key) / (double)RAND_MAX));
				ostringstream keystream;
				keystream << "TestKey" << key_no;
				shm_string key( keystream.str().c_str() );

				{ // lock scope (shared lock).  I only lock per operation here.
					sharable_lock<boost::interprocess::interprocess_upgradable_mutex> lock_holder(map->mutex());

					// Now...  for the indicated key, read it to see if it is present in the map.
					// We always do this, so 50% of all CRUD traffic is reads, although that does include
					// reads of rows which might not exist yet.
					// find never blocks (or it isn't supposed to) so I don't handle Lock errors
					MapType::iterator entry = map->find(key, *txn);
					finds++;

					// establish allocation scope against db->getRegion()'s segment manager, or the
					// remainder of this method.  So while 'key' is in heap for the call to find(),
					// any other shm_string's allocated hereafter are in the region.
					stldb::scoped_allocation<db_type::RegionType::segment_manager> alloc_scope(
							db->getRegion().get_segment_manager());

					if (entry == map->end()) {
						// row not found, so we'll insert the row with that key.
						// to do this, we must upgrade our shared lock to an exclusive one.
						lock_holder.unlock();
						scoped_lock<boost::interprocess::interprocess_upgradable_mutex> ex_lock_holder(map->mutex());

						// and generate a pseudo-random value for key.
						shm_string key_in_shm( key.c_str() );  // permit allocator to change to shm
						shm_string value( randomValue(key_no) );

						// insert the new key_in_shm, value pair.
						std::pair<MapType::iterator, bool> result;
						try {
							// If we discover an uncommitted insert with the same key, block until that transaction resolves.
							stldb::bounded_wait_policy<scoped_lock<boost::interprocess::interprocess_upgradable_mutex> >
								wait_with_timeout(ex_lock_holder, g_max_wait);

							result = map->insert( std::make_pair<shm_string,shm_string>( key_in_shm, value), *txn, wait_with_timeout );
						}
						catch (stldb::lock_timeout_exception &) {
							// The only exception possible from a blocking insert.
							// we timed out waiting for another threads lock on a pending
							// insert or delete for the same key to be resolved.
							throw;
						}

						if (result.second)
							inserts++;
						else {
							// this can happen because or race condition with another inserter
							// i.e. other inserting thread gets exclusive lock first.
							inserts_dupkey++;
						}
					}
					else {
						// verify that we have read a valid value (find is working)
						assert( checkValue(key_no, entry->second) );
						founds++;

						// With the found row, let's do one of 3 things. a) nothing, b) update it,
						// c) delete it. (33.3% chance of each.)
						int operation = static_cast<int>(::rand() * (((double)3.0) / (double)RAND_MAX));

						stldb::bounded_wait_policy<sharable_lock<boost::interprocess::interprocess_upgradable_mutex> >
							wait_with_timeout(lock_holder, g_max_wait);

						switch (operation) {
						case 1:
							{
								// update the entry
								try {
									shm_string newValue( randomValue(key_no) );
									map->update(entry, newValue, *txn, wait_with_timeout);
									updates++;
								}
								catch (stldb::row_deleted_exception &) {
									// another transaction deleted the row while we were trying to
									// update it.
									updates_row_removed++;
								}
							}
							break;
						case 2:
							{
								// delete the entry
								try {
									map->erase(entry, *txn, wait_with_timeout);
									deletes++;
								}
								catch (stldb::row_deleted_exception &) {
									// another transaction deleted the row while we were trying to
									// update it.
									deletes_row_removed++;
								}
							}
							break;
						default: //0
							// do nothing - read-only operation.
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
					PartitionedTestDatabase<managed_mapped_file,MapType>* db = getDatabase(i, lock);
					db->getDatabase()->checkpoint();

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

			int db_no = static_cast<int>(::rand() * (((double)g_num_db) / (double)RAND_MAX));

			PartitionedTestDatabase<managed_mapped_file,MapType> *db, *db2;
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
				int r = static_cast<int>(::rand() * (((double)_max_freq) / (double)RAND_MAX));
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
