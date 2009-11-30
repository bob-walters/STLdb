/*
 * Database.h
 *
 *  Created on: Feb 21, 2008
 *      Author: Bob Walters
 */

#ifndef STLDB_DATABASE_H_
#define STLDB_DATABASE_H_

#include <string>
#include <list>
#include <vector>
#include <map>

#include <boost/interprocess/detail/os_thread_functions.hpp> //getpid
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/streams/vectorstream.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/filesystem/path.hpp>

using boost::interprocess::detail::OS_process_id_t;
using boost::intrusive::optimize_size;
// tag
using boost::intrusive::void_pointer;
// tag
using boost::intrusive::cache_last;
using boost::intrusive::slist_base_hook;
using boost::intrusive::slist;

#include <stldb/sync/bounded_interprocess_mutex.h>
#include <stldb/cachetypes.h>
#include <stldb/creation_tags.h>
#include <stldb/commit_buffer.h>
#include <stldb/container_proxy.h>
#include <stldb/logger.h>
#include <stldb/sync/file_lock.h>
#include <stldb/statistics.h>
#include <stldb/detail/db_file_util.h>

using boost::interprocess::set;
using boost::interprocess::vector;
using boost::interprocess::map;
using boost::interprocess::deque;
using boost::interprocess::basic_string;
using boost::interprocess::allocator;
using boost::interprocess::interprocess_condition;
using boost::interprocess::basic_ovectorstream;

namespace stldb {

// Forward definitions
class Transaction;
class exclusive_transaction;
template<class void_alloc_t> class database_registry;


/**
 * The master database information record - stored in the shared region.
 */
template<class void_allocator_t, class mutex_t>
struct DatabaseInfo {

	typedef typename void_allocator_t::template rebind<char>::other
			shm_char_allocator_t;
	typedef boost::interprocess::basic_string<char, std::char_traits<char>,
			shm_char_allocator_t> shm_string;

	//! This region needs a mutex to protect its contents from concurrent modification.
	mutex_t mutex;

	//! lock for coordinating regular vs exclusive transactions
	boost::interprocess::interprocess_upgradable_mutex  transaction_lock;

	//! In progress transactions have one set of Ids.  When they are committed, they are
	//! assigned a log sequence number (see the logger's shared data)
	transaction_id_t next_txn_id;

	/**
	 * An intrusive list of available commit buffers.  These are created as needed
	 * and then reused across processes to minimize allocation.
	 */
	slist<commit_buffer_t<void_allocator_t> , cache_last<true> > _buffer_queue;

	/**
	 * Data held in shared memory for supporting the write-ahead log.
	 */
	SharedLogInfo<void_allocator_t, mutex_t> logInfo;

	/* Checkpoint tracking - for switching between full and incremental checkpoints. */
	typedef typename boost::interprocess::map<shm_string, transaction_id_t, std::less<shm_string>,
		typename void_allocator_t::template rebind< std::pair<shm_string,transaction_id_t> >::other>
			ckpt_history_map_t;

	ckpt_history_map_t ckpt_history_map;
	float max_incremental_percent;

	/* Info about the database */
	shm_string database_name;
	shm_string database_directory;
	shm_string checkpoint_directory;

	/* The containers which are within this region are assigned a unique integer ID which is
	 * used in logging */
//	int next_container_id;

//	typedef typename void_allocator_t::template rebind<std::pair<
//			const shm_string, int> >::other shm_map2_allocator_t;
//	boost::interprocess::map<shm_string, int, std::less<shm_string>,
//			shm_map2_allocator_t> _containers;

	DatabaseInfo(const void_allocator_t &alloc) :
		mutex()
		, transaction_lock()
		, next_txn_id(1)
		, _buffer_queue()
		, logInfo(alloc)
		, ckpt_history_map(std::less<shm_string>(), alloc)
		, max_incremental_percent(0)
		, database_name(alloc)
		, database_directory(alloc)
		, checkpoint_directory(alloc)
//		, next_container_id(0)
//		, _containers(std::less<shm_string>(), shm_map2_allocator_t(alloc))
	{ }

private:
	// Not allowed
	DatabaseInfo(void);

};


/**
 * @brief Class which allows applications to connect to and use an STLdb
 * database.
 *
 * ManagedRegionType is one of boost::interprocess::managed_shared_memory,
 * managed_mapped_file, managed_heap_memory, managed_external_buffer, or
 * managed_windows_shared_memory.  Lock type is the mutex type which the database will
 * use when synchronizing access to the database itself, and its own internal structures.
 */
template<class ManagedRegionType>
class Database {
	// Types acquired from the Allocation Algorithm of the Boost Managed Memory Region
	// These define the types of locks to use for the Database.
	typedef typename ManagedRegionType::mutex_family mutex_family;
	typedef typename mutex_family::mutex_type mutex_type;
	typedef typename boost::interprocess::interprocess_condition condition_type;

	typedef container_proxy_base<ManagedRegionType> container_proxy_type;

public:
	typedef ManagedRegionType RegionType;

	/**
	 * Create or recover a database.  If no database exists, it is created.  If the database
	 * already exists, its shared memory region is deleted and reinitialized via recovery,
	 * regardless of whether that region appears valid.  Opening a database in this way
	 * can recover from some types
	 * of errors in which the shared region contains badly corrupt data.
	 */
	Database(create_or_recover_t
			, const char* database_name // the name of this database (used for region name)
			, const char* database_directory // the location of metadata & lock files
			, std::size_t initial_size // the initial database size.
			, void* fixed_mapping_addr // fixed mapping address (optional)
			, const char* checkpoint_directory // where the persistent containers are stored
			, const char* log_directory // where the log files are written
			, std::size_t max_log_file_len
			, bool synchronous_logging
			, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
			);

	/**
	 * Open or Create a database, and recover as needed.  If no database exists
	 * it is created.  If the database exists, but its shared region is in need of
	 * recovery, then recovery is performed.  Otherwise, the existing region is
	 * simply opened and reused.  This method is fast if there is no need to create
	 * or recover the database.
	 */
	Database(open_create_or_recover_t
			, const char* database_name // the name of this database (used for region name)
			, const char* database_directory // the location of metadata & lock files
			, std::size_t initial_size // the initial database size.
			, void* fixed_mapping_addr // fixed mapping address (optional)
			, const char* checkpoint_directory // where the persistent containers are stored
			, const char* log_directory // where the log files are written
			, std::size_t max_log_file_len
			, bool synchronous_logging
			, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
			);

	/**
	 * Open and optionally Create the database.  If no database exists this method will create it.
	 * If a database already exists, this method will connect to the existing database region,
	 * unless recovery is needed.  If recovery is needed, this constructor will throw an
	 * exception.
	 */
	Database(open_or_create_t
			, const char* database_name // the name of this database (used for region name)
			, const char* database_directory // the location of metadata & lock files
			, std::size_t initial_size // the initial database size.
			, void* fixed_mapping_addr // fixed mapping address (optional)
			, const char* checkpoint_directory // where the persistent containers are stored
			, const char* log_directory // where the log files are written
			, std::size_t max_log_file_len
			, bool synchronous_logging
			, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
			);

	/**
	 * Open an existing database.  If the database region does not already exist, an
	 * exception is thrown.  If the region exists but appears to need recovery, then recovery is
	 * done and the region is recreated with the same size and configuration as the existing
	 * region.
	 */
	Database(open_or_recover_t
			, const char* database_name // the name of this database (used for region name)
			, const char* database_directory // the location of metadata & lock files
			, std::size_t initial_size // the initial database size.
			, void* fixed_mapping_addr // fixed mapping address (optional)
			, const char* checkpoint_directory // where the persistent containers are stored
			, const char* log_directory // where the log files are written
			, std::size_t max_log_file_len
			, bool synchronous_logging
			, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
			);

	/**
	 * Open an existing database.  If the database region does not already exist, an
	 * exception is thrown.  If the region exists but appears to need recovery, then an
	 * exception is thrown.  This constructor is guaranteed to be fast if it succeeds.
	 */
	Database(open_only_t
			, const char* database_name // the name of this database (used for region name)
			, const char* database_directory // the location of metadata & lock files
			, void* fixed_mapping_addr // fixed mapping address (optional)
			, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
			);

	/**
	 * The destructor closes the database.  If there are outstanding transactions
	 * in effect when the destructor is called, then the destructor will block
	 * until those transactions
	 * are either resolved, or time out (i.e. gating behavior.)
	 */
	~Database();

	/**
	 * Removes the indicated database, and all of its containers, including backing
	 * checkpoints and logs.  This method is only safe on a closed database, and an
	 * exception is thrown if any process has the database open.
	 */
	static void remove(const char *database_name
			, const char *database_directory
			, const char *checkpoint_directory
			, const char *log_directory);

	/**
	 * Remove the shared memory region for the indicated database.  On some systems, this
	 * can only be done while no processes are attached to the database.  The removal of
	 * the region forces it to be reloaded during a subsequent open request.
	 */
	static void remove_region(const char *database_name
			, const char *database_directory);

	/**
	 * Begins a (normal) transaction.  The new transaction is returned.
	 * Note that to improve efficiency, an existing completed (previously committed or
	 * aborted) transaction object may be passed as reusable_t.  It is an error
	 * to pass an in-progress transaction in this way.  Note that this method
	 * can block if there is currently an exclusive transaction in progress, or
	 * a thread waiting to start an exclusive transaction.
	 */
	Transaction* beginTransaction(Transaction *reusable_t = NULL);

	/**
	 * Begins an exclusive transaction.  Exclusive transactions are required for
	 * some operations on containers, as a matter of container design.  Examples will
	 * be methods like swap() and clear().  An exclusive transaction is guaranteed to
	 * be the only transaction which is running against the database when it exists.
	 * Calling this method will most likely block the caller, as all existing regular
	 * transactions must be completed before the exclusive transaction is begun.
	 */
	exclusive_transaction *begin_exclusive_transaction(exclusive_transaction *reusable_t = NULL);

	/**
	 * Commit the transaction (or exclusive transaction) passed.
	 */
	int commit(Transaction *t, bool diskless = false);

	/**
	 * Rollback the transaction (or exclusive transaction) passed.
	 */
	int rollback(Transaction *t);

	/**
	 * Writes out a checkpoint for each container which is registered with this
	 * database.  Returns the highest txn_id which the checkpoint has guaranteed
	 * to be durable.  This value can be used to eliminate log files which are
	 * no longer needed.
	 * NOTE: This works by calling a checkpoint method on each container in turn.
	 */
	transaction_id_t checkpoint();

	/**
	 * Get archivable logs.  This returns the set of log files which are no longer
	 * needed for recovery processing.  This static version takes an explicit
	 * checkpoint_directory and logging_directory, and does not require an open database.
	 */
	std::vector<boost::filesystem::path> get_archivable_logs();

	/**
	 * Get the set of obsolete checkpoint files in the directory passed.
	 * @returns a vector of checkpoint files no longer needed for recovery processing.
	 */
	std::vector<checkpoint_file_info> get_archivable_checkpoints();

	/**
	 * Get the set of log files which contain transactions that are not yet reflected in
	 * any checkpoints.  This returns the set of log files which are needed for recovery processing.
	 * @returns a vector of log file names.
	 */
	std::vector<boost::filesystem::path> get_current_logs();

	/**
	 * Get the set of current checkpoint files, containing the most recent completed
	 * checkpoint for each container in the database.
	 * @returns a map of container names to current checkpoint filename.
	 */
	std::map<std::string, checkpoint_file_info> get_current_checkpoints();

	/**
	 * Return a reference to the Managed Shared Memory Region that this database
	 * uses for its in-memory containers.
	 */
	ManagedRegionType& getRegion() {
		return *_region;
	}

	/**
	 * Gets a Container by Name.  The Container must be one mentioned in the
	 * list given to the Database constructor.
	 */
	template<class ContainerType>
	ContainerType* getContainer(const char *name);

	/**
	 * Adds the container, specified by the proxy, to the database.
	 * This works by invoking the openContainer() API of the proxy.
	 * No effort is made to load the container from prior checkpoints.
	 * The container is returned (as a void*).
	 * This is intended for adding new containers to an already open database.
	 */
	void* add_container(container_proxy_type *proxy);

	/**
	 * delete a container from the database.
	 */
	template<class ContainerType>
	bool remove_container(const char *name);

	/**
	 * Gets a container_proxy by id.  The Container must be one mentioned in the
	 * list given to the Database constructor.
	 */
	container_proxy_type* getContainerProxy(std::string container_name) {
		return _proxies_by_name[container_name];
	}

	//! Returns the name of the database, as given to the constructor
	const char *get_database_name() const {
		return _dbinfo->database_name.c_str();
	}

	//! Returns the name of the database, as given to the constructor
	const char *get_database_directory() const {
		return _dbinfo->database_directory.c_str();
	}

	//! Returns the checkpoint directory, as given to the constructor
	const char *get_checkpoint_directory() const {
		return _dbinfo->checkpoint_directory.c_str();
	}

	//! Returns the directory being used for logging.
	const char *get_logging_directory() const {
		return _dbinfo->logInfo.log_dir.c_str();
	}

	//! Returns the maximum log file size, as specified to the constructor.
	std::size_t get_max_logfile_size() const {
		return _dbinfo->logInfo.log_max_len;
	}

	//! Returns whether syncronous logging is configured.
	bool get_synchronous_logging() const {
		return _dbinfo->logInfo.log_sync;
	}

	/**
	 * Returns a stats object about the region, and the behavior of the
	 * associated logging subsystem.
	 */
	database_stats get_stats(bool reset = false) {
		database_stats result;
		result.logging_stats = _logger.get_stats(reset);
		result.region_size = _region->get_size();
		result.region_free = _region->get_free_memory();
		result.num_named_objects = _region->get_num_named_objects();
		result.num_unique_objects = _region->get_num_unique_objects();
		return result;
	}

	/**
	 * Check to see if the Database integrity is assured.  This performs
	 * the registry check function to see if any process have disconnected.
	 * @returns true if the registry check passes, false if it fails.
	 * if returns false, the panic bit in the registry of the existing region
	 * is also set to prevent any further transactions from being started.
	 */
	bool check_integrity();

	/**
	 * Closes the database.
	 */
	void close(bool final_checkpoint = false);

	/**
	 * sets or clears the internal 'valid' flag on the Database.
	 * set_invalid(true), is identical to a Database failing a call to
	 * check_integrity().  All processes will start seeing recovery_needed
	 * exceptions until they close and Reconstruct the database, permitting
	 * recovery to occur.  set_invalid(false) clears this flag,
	 * permitting processes to carry on using the current shared memory region,
	 * even though it may be corrupt.
	 */
	void set_invalid(bool value) {
		boost::interprocess::scoped_lock<stldb::file_lock> reglock(_registry_lock);
		_registry->set_invalid(value);
	}

private:
	// method to find or construct the shared _dbinfo structure.
	void find_or_construct_databaseinfo(
			std::list<container_proxy_type*>& containers,
			const char *database_name, const char *database_dir,
			const char *checkpoint_dir, const char *log_directory,
			std::size_t max_log_file_size, bool synchronous_logging);

	// open & load containers from checkpoint files.
	void open_containers(std::list<container_proxy_type*>& containers);

	// returns the (min,max) lsn values indicating the starting LSN of log recovery,
	// and the ending LSN which must be reached in order to ensure that all containers
	// are recovered completely.
	std::pair<transaction_id_t, transaction_id_t>
	load_containers(std::list<container_proxy_type*>& containers, std::map<
			container_proxy_type*, transaction_id_t> &container_lsn);

	// Caller must hold file lock on the database.
	static void remove_region_impl(const char *database_name, const char*database_directory);

	/**
	 * Check to see if the _registry->_database_invalid flag has been set, and if it
	 * throw a recovery_needed exception.  This is used internally by other methods
	 * of Database to protect the app from using a region which may have hung locks or
	 * other forms of corruption.
	 */
	void safety_check() throw (recovery_needed) {
		// This is done very frequently, so I omit the lock.  This should be
		// safe since _register->is_valid only goes from false back to true via
		// recovery & reconstruction of the region.
		if (!_registry->is_valid())
			throw recovery_needed();
	}

	// The attached managed region, holding all structures.
	ManagedRegionType *_region;

	// The master lock of the database, used to synchronize the execution of the constructors
	// across all processes. It's a file lock in order to provide
	// the assurance that upon untimely process death, the lock will be released.
	stldb::file_lock _flock;

	// The type of allocator which is used with this Managed Region Type
	// for allocating the pairs in the _containers map.
	typedef boost::interprocess::allocator<void,
			typename ManagedRegionType::segment_manager> region_allocator_t;

	// A map of container addresses back to their original container_proxy
	// (as originally passed to the database constructor)  used in the
	// transactional subsystem.
	std::map<void*, container_proxy_type*> _container_proxies;

	// A map of container names back to their original container_proxy.
	std::map<std::string, container_proxy_type*> _proxies_by_name;

// IN SHARED MEMORY (_region):
	// Pointer to the DB info inside the managed region.
	DatabaseInfo<region_allocator_t, mutex_type> *_dbinfo;

	// The registry of processes in the managed region, and the lock held by THIS process.
	database_registry<region_allocator_t> *_registry;
// END IN SHARED MEMORY:

	// Logging sub-logic.  Uses data in _dbinfo.
	Logger<region_allocator_t, mutex_type> _logger;

	// The "dbname.reglock" file lock which guards the registry itself
	stldb::file_lock _registry_lock;

	// The file lock that our process holds on our "database_name.pid.XXXXX" file to signal our
	// processes health.
	stldb::file_lock *_registry_pid_lock;
};

} // namespace stldb;

#ifndef AUTO_TEMPLATE
#include <stldb/Database.tcc>
#endif

#endif /* DATABASE_H_ */
