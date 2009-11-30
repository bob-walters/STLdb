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


static int countRows( MapType *map, Transaction *txn )
{
	MapType::iterator i = map->begin(*txn);
	int count = 0;
	while (i != map->end(*txn)) {
		std::cout << "[" << i->first.c_str() << "," << i->second.c_str() << "]" << std::endl;
		i++; count++;
	}
	return count;
}

// Config parameters (set in stress_test())
static int g_num_db;
static int g_maps_per_db;
static int g_max_key;
static int g_avg_val_length;  // real langth is between 50% and 150% of this value.

static std::string g_db_dir;
static std::string g_checkpoint_dir;
static std::string g_log_dir;

static boost::posix_time::time_duration  g_max_wait;

// Databases.  We need the shared mutex to coordinate re-opening of the
// databases in the event one of the starts throwing run_recovery exceptions
static boost::shared_mutex  g_db_mutex[100];
static PartitionedTestDatabase<managed_mapped_file,MapType> *g_databases[100];


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
	lock = boost::shared_lock<boost::shared_mutex>( g_db_mutex[db_num] ).move();

	if (g_databases[db_num] == NULL) {
		// We need to create the database
		lock.unlock(); // release shared lock, for now.

		// We'll need an exclusive lock to construct the DB.
		unique_lock<boost::shared_mutex> exholder(g_db_mutex[db_num]);

		// Double check now that we're exclusive.  It could have been
		// constructed by another thread while we waited to get 'exholder'
		ostringstream str;
		str << "stressDb_" << db_num;
		std::string dbname( str.str() );
		if (!g_databases[db_num])
			g_databases[db_num] = new PartitionedTestDatabase<managed_mapped_file,MapType>(
				dbname.c_str(), g_db_dir.c_str(),
				g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );

		// down-grade the exclusive lock using move semantics.
		lock = exholder.move();
	}
	result = g_databases[db_num];

	// move semantics should result in lock going with return value.
	return result;
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
		delete g_databases[db_num];
		g_databases[db_num] = NULL;

		ostringstream str;
		str << "stressDb_" << db_num;
		std::string dbname( str.str() );

		// During construction, recovery should occur.
		g_databases[db_num] = new PartitionedTestDatabase<managed_mapped_file,MapType>(
			dbname.c_str(), g_db_dir.c_str(),
			g_checkpoint_dir.c_str(), g_log_dir.c_str(), g_maps_per_db );
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
		{ }

	virtual ~CRUD_transaction() { }

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
					// find never blocks (or it isn't supposed to, so I don't handle Lock errors
					MapType::iterator entry = map->find(key, *txn);

					if (entry == map->end()) {
						// row not found, so we'll insert the row with that key.
						// to do this, we must upgrade our shared lock to an exclusive one.
						lock_holder.unlock();
						scoped_lock<boost::interprocess::interprocess_upgradable_mutex> ex_lock_holder(map->mutex());

						// establish allocation scope against db->getRegion()'s segment manager.
						stldb::scoped_allocation<db_type::RegionType::segment_manager> a(db->getRegion().get_segment_manager());

						// and generate a pseudo-random value for key.
						shm_string key_in_shm( key );
						shm_string value( randomValue(key_no) );

						// insert the new key_in_shm, value pair.
						std::pair<MapType::iterator, bool> result;
						try {
							// If we discover an uncommitted insert with the same key, block until that transaction resolves.
							stldb::bounded_wait_policy<scoped_lock<boost::interprocess::interprocess_upgradable_mutex> >
								wait_with_timeout(ex_lock_holder, g_max_wait);

							result = map->insert( std::make_pair<shm_string,shm_string>( key_in_shm, value), *txn, wait_with_timeout );

						} catch (stldb::row_deleted_exception) {
							// another transaction deleted 'i' before we could lock it, so pretend
							// that find() never read it at all.
							return;
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

						// With the found row, let's do one of 3 things. a) nothing, b) update it,
						// c) delete it. (33.3% chance of each.)
						int operation = static_cast<int>(::rand() * (((double)3.0) / (double)RAND_MAX));

						stldb::bounded_wait_policy<sharable_lock<boost::interprocess::interprocess_upgradable_mutex> >
							wait_with_timeout(lock_holder, g_max_wait);

						switch (operation) {
						case 1:
							{
								// establish allocation scope against db->getRegion()'s segment manager.
								stldb::scoped_allocation<db_type::RegionType::segment_manager> a(db->getRegion().get_segment_manager());

								// update the entry
								shm_string newValue( randomValue(key_no) );
								map->update(entry, newValue, *txn, wait_with_timeout);
							}
							break;
						case 2:
							{
								// delete the entry
								map->erase(entry, *txn, wait_with_timeout);
							}
							break;
						default: //0
							// do nothing - read-only operation.
							break;
						}
					}
				}
			}
		}
		catch( stldb::lock_timeout_exception & ) {
			// probably means I have deadlocked with another thread.
			// so rollback to release my locks.
			lock_timeouts++;
			db->rollback(txn);
			// make sure this isn't because someone has screwed up...
			bool db_ok = db->getDatabase()->check_integrity();
			if (!db_ok)
				real_panics++;
		}
		catch ( stldb::recovery_needed & ) {
			// We have found ourselves to be using a PartitionedTestDatabase which needs
			// recovery processing.  In other words, it has to be cloed and reopened.
			recoverDatabase(db_no, db, lock);
		}
	}

private:
	int txnSize;

	int finds;
	int inserts;
	int inserts_dupkey;
	int updates;
	int updates_row_removed;
	int deletes;
	int deletes_row_removed;
	int lock_timeouts;
	int real_panics;
};


// One of the possible transactional operations which is invoked.
// Does some standard read/writes to a couple of maps within an
// environment, and then commits.
class checkpoint_operation
{
public:
	checkpoint_operation()
		{ }

	virtual ~checkpoint_operation() { }

	virtual void operator()() {
		// TODO - get a database, and perform a checkpoint of that database
	}
};


// Set_invalid(true) on a random database, causing needs_recovery_exception on all
// threads using it.
class set_invalid_operation
{
public:
	set_invalid_operation()
		{ }

	virtual ~set_invalid_operation() { }

	virtual void operator()() {
		// TODO - get a database, and call set_invalid(true) on that database
	}
};


// Delete the log files marked as 'archivable' pertaining to a database.
class remove_old_logs
{
public:
	remove_old_logs()
		{ }

	virtual ~remove_old_logs()
		{ }

	virtual void operator()() {
		// TODO - get a database, and remove all archivable logs for that database.
	}
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

	stldb::timer::enabled = properties.getProperty("timing", true);
	stldb::tracing::set_trace_level(stldb::finest_e);

	const int thread_count = properties.getProperty("threads", 4);
	const int loopsize = properties.getProperty("loopsize", 100);
	const int ops_per_txn = properties.getProperty("ops_per_txn", 10);
	g_num_db = properties.getProperty("databases", 4);
	g_maps_per_db = properties.getProperty("maps_per_db", 4);
	g_max_key = properties.getProperty("max_key", 10000);
	g_max_wait = boost::posix_time::millisec(properties.getProperty("max_wait", 10000));

	// The loop that the running threads will execute
	test_loop loop(loopsize);
	loop.add( new CRUD_transaction(ops_per_txn), 100 );

	// Start the threads which are going to run operations:
	boost::thread **workers = new boost::thread *[thread_count];
	for (int i=0; i<thread_count; i++) {
		workers[i] = new boost::thread( loop );
	}

	// now await their completion
	for (int i=0; i<thread_count; i++) {
		workers[i]->join();
		delete workers[i];
	}

	return 0;
}
