/*
 *  Database.cpp
 *
 *  Created by Bob Walters on 2/21/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include <limits>
#include <list>
#include <ios>
#include <fstream>
#include <cstdlib>

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/managed_heap_memory.hpp>
#include <boost/interprocess/managed_external_buffer.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/filesystem.hpp>
#if (defined BOOST_INTERPROCESS_WINDOWS)
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>
#endif

#include <boost/interprocess/file_mapping.hpp>
#include <boost/filesystem/path.hpp>

#include <stldb/database_registry.h>
#include <stldb/transaction.h>
#include <stldb/timing/timer.h>
#include <stldb/trace.h>
#include <stldb/recovery_manager.h>
#include <stldb/detail/db_file_util.h>
#include <stldb/detail/region_util.h>

using boost::interprocess::scoped_lock;
using boost::interprocess::map;
using boost::interprocess::anonymous_instance;

using namespace boost::filesystem;

using stldb::commit_buffer_t;



namespace stldb
{



/**
 * Open or Create a database, and recover as needed.  If no database exists
 * it is created.  If the database exists, but its shared region is in need of
 * recovery, then recovery is performed.  Otherwise, the existing region is
 * simply opened and reused.  This method is fast if there is no need to create
 * or recover the database.
 */

template<class ManagedRegionType>
Database<ManagedRegionType>::Database(
		open_create_or_recover_t open_create_or_recover_tag
		, const char* database_name // the name of this database (used for region name)
		, const char* database_directory   // the location of metadata & lock files
		, std::size_t initial_size // the initial database size.
		, void* fixed_mapping_addr // fixed mapping address (optional)
		, const char* checkpoint_directory // where the persistent containers are stored
		, const char* log_directory // where the log files are written
		, std::size_t max_log_file_len
		, bool synchronous_logging
		, std::list<container_proxy_type*> containers // the containers you want created/opened in the database
)
// Construct the region for create or open
:	_region(NULL)
	, _flock(ManagedRegionNamer<ManagedRegionType>::getFullName(database_directory, database_name).append(".flock").c_str())
	, _dbinfo(NULL), _registry(NULL), _logger()
	, _registry_lock(database_registry<region_allocator_t>::filelock_name(database_directory, database_name).c_str())
	, _registry_pid_lock(NULL)
{
	// Start off by getting the file lock.  We must get this BEFORE opening the region.
	STLDB_TRACE(stldb::finest_e, "acquiring file lock on " << database_name << ".flock");
	scoped_lock<file_lock> lock(_flock);
	STLDB_TRACE(stldb::finest_e, "file lock acquired.");

	bool creating_region = false;

	// Attach (and optionally create) the managed memory region
	std::string fullname = ManagedRegionNamer<ManagedRegionType>::getFullName(database_directory, database_name);
	STLDB_TRACE(fine_e, "opening/creating region " << fullname);
	_region = new ManagedRegionType(boost::interprocess::open_or_create,
			fullname.c_str(), initial_size, fixed_mapping_addr);
	STLDB_TRACE(fine_e, "region opened.");

	// Now, get the registry data structures within the region.
	_registry = (_region->template find<database_registry<region_allocator_t> >
			(boost::interprocess::unique_instance)).first;

	bool recovery_needed = false;
	if (_registry == NULL)
	{
		STLDB_TRACE(fine_e, "no registry found.  creating region data structures");
		// This means (since we have the _flock) that we are creating or
		// recovering the managed memory region from scratch.
		creating_region = true;
	}
	else
	{
		STLDB_TRACE(fine_e, "registry found. checking need for recovery");
		STLDB_TRACE(finest_e, "acquiring registry mutex");
		scoped_lock<stldb::file_lock> reglock(_registry_lock);
		STLDB_TRACE(finest_e, "registry mutex acquired.");

		// We have attached to a region with something already in it.
		// Using just the RegistryInfo structure, let's see if it looks valid.
		recovery_needed = _registry->check_need_for_recovery();
		if (!recovery_needed)
			recovery_needed = !_region->check_sanity();
		if (recovery_needed)
		{
			STLDB_TRACE(warning_e, "recovery is needed.");

#if (defined BOOST_INTERPROCESS_WINDOWS)
			// On Unix systems, removing a shared region while other processes
			// are attached to it is OK.  On some other OSes, it isn't OK.
			// So for portability, we wait for processes to all detach before
			// deleting the region.
			STLDB_TRACE(finest_e, "awaiting disconnect by all process currently using database.");
			_registry->await_complete_disconnect(reglock);
			STLDB_TRACE(finest_e, "all other processes have disconnected.");
#endif
		}
		else {
			STLDB_TRACE(fine_e, "no recovery is needed, passed disconnect check and check_sanity()");
		}
	}
	if (recovery_needed)
	{
		STLDB_TRACE(warning_e, "performing recovery");
		STLDB_TRACE(finest_e, "closing existing region");
		delete _region; // close the region
		STLDB_TRACE(fine_e, "removing existing region");
		// time for a fresh start
		RegionRemover<ManagedRegionType>::remove(database_directory,database_name);

		// Recreate the managed memory region
		STLDB_TRACE(fine_e, "recreating region");
		_region = new ManagedRegionType(boost::interprocess::create_only,
				fullname.c_str(), initial_size, fixed_mapping_addr);

		creating_region = true;
	}

	// The structures of the database will need an allocator.
	// We can safely create this now because _region is stable 
	// (either created, reused or recovered)
	region_allocator_t alloc(_region->get_segment_manager());

	if (creating_region)
	{
		STLDB_TRACE(fine_e, "creating registry data structures");
		_registry = _region->template construct<database_registry<region_allocator_t> >
			(boost::interprocess::unique_instance)
			(database_directory, database_name, alloc);
	}

	// At this point, there is a registry.  We can check at this point to see
	// if there's any need to resize the region based on a config change
	scoped_lock<stldb::file_lock> reglock(_registry_lock);
	if (_registry->connected_pids()==0 && initial_size != _region->get_size()) {
		_region = RegionResizer<ManagedRegionType>::resize( _region, fullname.c_str(), initial_size, fixed_mapping_addr);

		STLDB_TRACE(info_e, "Region resized to " << _region->get_size() << " bytes");

		// Restore the _registry pointer, and then continue.
		_registry = (_region->template find<database_registry<region_allocator_t> >
				(boost::interprocess::unique_instance)).first;
	}

	// At this point, I can register.
	STLDB_TRACE(fine_e, "registering this process");
	_registry_pid_lock = _registry->register_pid();
	reglock.unlock();

	// find or construct all of the internal database structures, including
	// the container instances.
	STLDB_TRACE(fine_e, "finding/creating database info data structures");
	this->find_or_construct_databaseinfo(containers, database_name,
				database_directory, checkpoint_directory,
				log_directory, max_log_file_len, synchronous_logging );

	// open the containers stored within the database
	STLDB_TRACE(fine_e, "finding/creating containers");
	this->open_containers(containers);

	if (creating_region)
	{
		// whether because it didn't exist, or because we are recreating it
		// as part of recovery, we now need to reload any containers from any
		// checkpoints and/or logs
		STLDB_TRACE(fine_e, "loading containers from checkpoints");
		std::map<container_proxy_type*,transaction_id_t> container_lsn;
		std::pair<transaction_id_t,transaction_id_t> lsns = this->load_containers(containers, container_lsn);
		transaction_id_t recovery_start_lsn = lsns.first;

		STLDB_TRACE(fine_e, "recovering transactions from log records, sarting at LSN: " << recovery_start_lsn);
		recovery_manager<ManagedRegionType> recovery(*this, container_lsn, recovery_start_lsn);
		transaction_id_t last_lsn = recovery.recover();
		STLDB_TRACE(fine_e, "recovered all transactions up to LSN: " << last_lsn);
		if (last_lsn < lsns.second) {
			// We have a problem.  Some log record, which should have been available for
			// recovery at the end of the log sequence were missing for some reason.  (i.e.
			// A file was truncated or corrupt.)  As a result, tables may have inconsistencies,
			// so we have to fail here.
			STLDB_TRACE(fine_e, "Recovery FAILED.  Needed to recover through LSN: " << lsns.second << ", but made it only to: " << last_lsn);
			_registry->set_invalid(true);
			throw stldb_exception("Recovery Failed: detected missing log records that are needed to arrive at a consistent database image.");
		}

		// Subsequent commits will add to what already exists.
		_dbinfo->logInfo._next_commit_txn_id = last_lsn+1;
		_dbinfo->logInfo._last_write_txn_id = last_lsn;
		_dbinfo->logInfo._last_sync_txn_id = last_lsn;

		// Mark the region info as valid now that everything is constructed, and
		// all containers have been properly loaded.
		STLDB_TRACE(fine_e, "marking database construction as completed.");
		scoped_lock<stldb::file_lock> reglock(_registry_lock);
		_registry->construction_complete();
	}
	// _flock released as we go out of scope...
}


template<class ManagedRegionType>
void
Database<ManagedRegionType>::close(bool final_checkpoint)
{
	if (_registry_pid_lock != NULL)
	{
		// release our registry lock.
		scoped_lock<stldb::file_lock> reglock(_registry_lock);
		if (_registry->is_valid() && _registry->attached_pids()==1 && final_checkpoint) {
			reglock.unlock();
			this->checkpoint();
			reglock.lock();
		}
		STLDB_TRACE(finer_e, "unregistering process from database registry");
		_registry->unregister_pid(_registry_pid_lock);
		_registry_pid_lock = NULL;
	}
	// Destructor closes the region.
	delete _region;
	_region = NULL;
}


template<class ManagedRegionType>
Database<ManagedRegionType>::~Database()
{
	if (_region)
		close(false);
}


template<class ManagedRegionType>
bool Database<ManagedRegionType>::check_integrity()
{
	scoped_lock<stldb::file_lock> reglock(_registry_lock);
	if (!_registry->is_valid())
		return false;

	// We have attached to a region with something already in it.
	// Using just the RegistryInfo structure, let's see if it looks valid.
	bool recovery_needed = _registry->check_need_for_recovery();
	// Also check the sanity of the memory map internal data structures.
	if (!recovery_needed)
		recovery_needed = !_region->check_sanity();
	if (recovery_needed) {
		STLDB_TRACE(finer_e, "need for recovery has been detected" );
	}
	return !recovery_needed;
}


template<class ManagedRegionType>
void Database<ManagedRegionType>::remove_region(const char *database_name
		, const char*database_directory)
{
	// Start off by getting the file lock.  We must get this BEFORE opening the region to synchronize properly.
	STLDB_TRACE(stldb::finest_e, "acquiring file lock on " << database_name << ".flock");
	stldb::file_lock flock(ManagedRegionNamer<ManagedRegionType>::getFullName(database_directory, database_name).append(".flock").c_str());
	scoped_lock<file_lock> lock(flock);
	STLDB_TRACE(stldb::finest_e, "file lock acquired.");

	remove_region_impl(database_name, database_directory);
}


// Caller must hold file lock on the database.
template<class ManagedRegionType>
void Database<ManagedRegionType>::remove_region_impl(const char *database_name
		, const char*database_directory)
{
	// Attach (but do not create) the managed memory region
	std::string fullname = ManagedRegionNamer<ManagedRegionType>::getFullName(database_directory, database_name);
	STLDB_TRACE(fine_e, "opening region " << fullname << "in order to set the invalid bit (evicting currently attached processes)");
	ManagedRegionType *region;
	try {
		region = new ManagedRegionType(boost::interprocess::open_only, fullname.c_str());
	}
	catch (boost::interprocess::interprocess_exception &x) {
		STLDB_TRACE(fine_e, "region not found during region_remove() call");
		return;
	}
	STLDB_TRACE(fine_e, "region opened.");

	// Now, find the registry data structures within the region.
	database_registry<region_allocator_t> *registry
		= (region->template find<database_registry<region_allocator_t> >
				(boost::interprocess::unique_instance)).first;

	stldb::file_lock reg_file_lock(
			database_registry<region_allocator_t>::filelock_name(database_directory, database_name).c_str() );

	if (registry != NULL)
	{
		STLDB_TRACE(fine_e, "registry found. setting invalid bit");
		scoped_lock<stldb::file_lock> reglock(reg_file_lock);
		registry->set_invalid(true);

#if (defined BOOST_INTERPROCESS_WINDOWS)
		// On Unix systems, removing a shared region while other processes
		// are attached to it is OK.  On some other OSes, it isn't OK.
		// So for portability, we wait for processes to all detach before
		// deleting the region.
		STLDB_TRACE(finest_e, "awaiting disconnect by all process currently using database.");
		registry->await_complete_disconnect(reglock);
		STLDB_TRACE(finest_e, "all other processes have disconnected.");
#endif
	}

	// we don't need to delete registry, it is within region.
	// but we do delete region, severing the connection.
	delete region;
	// Now remove the region.  If this fails here, we do want an exception.
	RegionRemover<ManagedRegionType>::remove(database_directory, database_name);
}


template<class ManagedRegionType>
void Database<ManagedRegionType>::remove(const char *database_name
		, const char *database_directory
		, const char *checkpoint_directory
		, const char *log_directory)
{
	// Start off by getting the file lock.  We must get this BEFORE opening the region to synchronize properly.
	STLDB_TRACE(stldb::finest_e, "acquiring file lock on " << database_name << ".flock");
	stldb::file_lock flock(ManagedRegionNamer<ManagedRegionType>::getFullName(database_directory, database_name).append(".flock").c_str());
	scoped_lock<file_lock> lock(flock);
	STLDB_TRACE(stldb::finest_e, "file lock acquired.");

	remove_region_impl(database_name, database_directory);

	// Delete all checkpoint files
	std::vector<checkpoint_file_info> chkpts = detail::get_checkpoints(checkpoint_directory);
	for ( std::vector<checkpoint_file_info>::iterator i = chkpts.begin(); i != chkpts.end(); i++ ) {
		boost::filesystem::path fullname( checkpoint_directory );
		fullname /= i->filename;
		STLDB_TRACE(fine_e, "Removing checkpoint: " << fullname.string() );
		try {
			boost::filesystem::remove( fullname.string() );
		}
		catch (boost::filesystem::filesystem_error &ex) {
			STLDB_TRACE(severe_e, "Error Removing log file: " << fullname.string() << ": " << ex.what() );
		}
	}

	// Delete all log files
	std::map<transaction_id_t,boost::filesystem::path> logfiles = detail::get_log_files(log_directory);
	for ( typename std::map<transaction_id_t,boost::filesystem::path>::iterator i = logfiles.begin();
			i != logfiles.end(); i++ ) {
		STLDB_TRACE(fine_e, "Removing log files: " << i->second.string() );
		try {
			boost::filesystem::remove( i->second );
		}
		catch (boost::filesystem::filesystem_error &ex) {
			STLDB_TRACE(severe_e, "Error Removing log file: " << i->second.string() << ": " << ex.what() );
		}
	}
}


template<class ManagedRegionType>
void Database<ManagedRegionType>::find_or_construct_databaseinfo(
		std::list<container_proxy_type*>& containers
		, const char *database_name
		, const char *database_dir
		, const char *checkpoint_dir
		, const char *log_directory
		, std::size_t max_log_file_size
		, bool synchronous_logging )
{
	//typedef allocator<void, typename ManagedRegionType::segment_manager> alloc_t;

	// find or construct _dbinfo.
	_dbinfo = _region->template find_or_construct<DatabaseInfo<
			region_allocator_t, mutex_type> > ("DBInfo")(region_allocator_t(
			_region->get_segment_manager()));

	if (_dbinfo->database_name.empty()) {
		_dbinfo->database_name = database_name;
	}
	if (_dbinfo->database_directory.empty()) {
		_dbinfo->database_directory = database_dir;
	}
	if (_dbinfo->checkpoint_directory.empty()) {
		_dbinfo->checkpoint_directory = checkpoint_dir;
	}

	SharedLogInfo<region_allocator_t,mutex_type> &loginfo = _dbinfo->logInfo;
	if (loginfo.log_dir.empty()) {
		loginfo.log_dir = log_directory;
		loginfo.log_max_len = max_log_file_size;
		loginfo.log_sync = synchronous_logging;
	}

	// The logger uses the shared info in _dbinfo->logInfo
	_logger.set_shared_info(&loginfo);
}


/**
 * Create_or_open containers within the shared region.
 * If created, they will be empty.
 */
template<class ManagedRegionType>
void Database<ManagedRegionType>::open_containers(
		std::list<container_proxy_type*>& containers )
{
	for (typename std::list<container_proxy_type*>::iterator i =
		 containers.begin(); i != containers.end(); i++)
	{
		add_container(*i);
	}
}


/**
 * Load containers from their relevant checkpoint files.
 */
template<class ManagedRegionType>
std::pair<transaction_id_t,transaction_id_t>
Database<ManagedRegionType>::load_containers(
		std::list<container_proxy_type*>& containers,
		std::map<container_proxy_type*,transaction_id_t> &container_lsn)
{
// A problem on MSVC - some header defines a macro named 'max'.  Nice....
#ifdef max
#undef max
#endif
	transaction_id_t recovery_start_lsn = std::numeric_limits<transaction_id_t>::max();  // the lsn to start recovery at.
	transaction_id_t recovery_end_lsn = -1;

	std::map<std::string,checkpoint_file_info> current_checkpoints = this->get_current_checkpoints();

	// This declaration of scoped allocation is a precaution, in case the K or V type
	// uses an allocator, in which case it may need to use a scope_aware_allocator
	// or similar construct that supports a default constructor, based on how trans_map::save_checkpoint
	// is written.
	stldb::scoped_allocation<typename ManagedRegionType::segment_manager> default_alloc( _region->get_segment_manager() );
	region_allocator_t alloc( _region->get_segment_manager() );

	for (typename std::map<void*,container_proxy_type*>::iterator i =
		 _container_proxies.begin(); i != _container_proxies.end(); i++)
	{
		// find_or_create each container in turn.
		container_proxy_type* proxy = i->second;

		// find the most recent checkpoint file for this container.
		std::map<std::string,checkpoint_file_info>::iterator chkpt = current_checkpoints.find( proxy->getName() );
		if (chkpt != current_checkpoints.end() ) {
			std::string filename = chkpt->second.filename;

			STLDB_TRACE(fine_e, "found checkpoint " << filename << " for container: " << proxy->getName());
			if (chkpt->second.lsn_at_start < recovery_start_lsn)
				recovery_start_lsn = chkpt->second.lsn_at_start;
			if (chkpt->second.lsn_at_end > recovery_end_lsn)
				recovery_end_lsn = chkpt->second.lsn_at_end;

			// filename
			std::string fullname( _dbinfo->checkpoint_directory.c_str() );
			fullname.append( "/" );
			fullname.append( filename );
			std::ifstream in(fullname.c_str(), std::ios_base::in);

			// now load the container from its most recent checkpoint (if any)
			STLDB_TRACE(fine_e, "loading checkpoint: " << fullname);
			proxy->load_checkpoint( in );

			typename DatabaseInfo<region_allocator_t, mutex_type>::shm_string
					container_name( chkpt->first.c_str(), alloc );
			_dbinfo->ckpt_history_map[container_name] = chkpt->second.lsn_at_start;

			container_lsn[proxy] = chkpt->second.lsn_at_start;
		}
		else {
			// recover_start_lsn has to be set to 0, since this container
			// never managed even one successful checkpoint.
			STLDB_TRACE(fine_e, "no checkpoint found for container: " << proxy->getName());
			recovery_start_lsn = 0;
			container_lsn[proxy] = 0;

			typename DatabaseInfo<region_allocator_t, mutex_type>::shm_string
					container_name( proxy->getName().c_str(), alloc );
			_dbinfo->ckpt_history_map[container_name] = 0;
		}
	}
	return std::make_pair( recovery_start_lsn, recovery_end_lsn );
}


template<class ManagedRegionType>
template<class ContainerType>
ContainerType* Database<ManagedRegionType>::getContainer(const char *name)
{
	safety_check();
	return _region->template find<ContainerType> (name).first;
}


/**
 * Adds the container specified by the proxy passed to the database.
 * Note that if a container with that name already exists in the database,
 * that container will be returned by this call, and a new instance will
 * not be created.  Also no effort is made to load the container.
 */
template<class ManagedRegionType>
void* Database<ManagedRegionType>::add_container(container_proxy_type *proxy)
{
	safety_check();
	void *container = proxy->find_or_construct_container(*this);

	// make record of the containers and this proxy class.
	scoped_lock<mutex_type> guard(_dbinfo->mutex);

	_container_proxies[container] = proxy;
	_proxies_by_name[proxy->getName()] = proxy;

	STLDB_TRACE(fine_e, "opened or created container: " << proxy->getName());
	return container;
}


/**
 * delete a container from the database.
 */
template<class ManagedRegionType>
template<class ContainerType>
bool Database<ManagedRegionType>::remove_container(const char *name)
{
	safety_check();
	scoped_lock<mutex_type> guard(_dbinfo->mutex);
	return _region->template destroy<ContainerType>(name);
	STLDB_TRACE(fine_e, "destroyed container: " << name);

}


template <class ManagedRegionType>
Transaction* Database<ManagedRegionType>::beginTransaction(Transaction *reusable_t)
{
	stldb::timer t("Database::beginTransaction");
	safety_check();
	Transaction *result = reusable_t;
	if (!result)
		result = new Transaction();

	// To begin a normal transaction, we get a shared lock on the database's
	// transaction lock, which just ensures that no exclusive transaction is
	// operational while this one is.
	result->lock_database(_dbinfo->transaction_lock);

	// Now assign it a transaction_id;
	{
		scoped_lock<mutex_type> guard(_dbinfo->mutex);
		result->_transId = _dbinfo->next_txn_id++;
	}

	return result;
}


template <class ManagedRegionType>
exclusive_transaction *Database<ManagedRegionType>::begin_exclusive_transaction(exclusive_transaction *reusable_t)
{
	stldb::timer t("Database::begin_exclusive_transaction");
	safety_check();
	exclusive_transaction *result = reusable_t;
	if (!result)
		result = new exclusive_transaction();

	// To begin an exclusive transaction, it must establish an exclusive lock
	// on the database's transaction lock, thereby guaranteeing that it is the
	// only transaction running.
	result->lock_database(_dbinfo->transaction_lock);

	// Now assign a transaction_id.
	{
		scoped_lock<mutex_type> guard(_dbinfo->mutex);
		result->_transId = _dbinfo->next_txn_id++;
	}

	return result;
}


template <class ManagedRegionType>
int Database<ManagedRegionType>::commit(Transaction *transaction, bool diskless)
{
	try {
		stldb::timer t1("Database::commit(transaction*)");
		safety_check();

		// We need to get a commit txn_id from the dbinfo region
		commit_buffer_t<region_allocator_t>* buffer = NULL;
		{
			scoped_lock<mutex_type> guard(_dbinfo->mutex);

			// If a reusable commit buffer is available, reuse it.
			if (_dbinfo->_buffer_queue.size()>0) {
				buffer = & (_dbinfo->_buffer_queue.front());
				_dbinfo->_buffer_queue.pop_front();
			}
		}
		if (buffer == NULL) {
			stldb::timer t2("create commit_buffer");
			// need to construct a new commit buffer.
			region_allocator_t alloc(_region->get_segment_manager());
			typedef commit_buffer_t<region_allocator_t>  commit_buffer_type;
			buffer = _region->template construct< commit_buffer_type >(anonymous_instance)(alloc);
		}
		else {
			// found reusable buffer
			buffer->op_count = 0;
			buffer->clear();
		}

		transaction->commit( _container_proxies, _logger, diskless, buffer );
		transaction->unlock_database();

		{ // scope
			stldb::timer t3("store commit_buffer in reuse queue");
			scoped_lock<mutex_type> guard(_dbinfo->mutex);
			// return the commit buffer to the queue.
			_dbinfo->_buffer_queue.push_front(*buffer);
		}
	}
	catch ( ... ) {
		// If any exception is throw, still release the _dbinfo->transaction_mutex
		transaction->unlock_database();
		throw;
	}
	return 0;
}


template <class ManagedRegionType>
int Database<ManagedRegionType>::rollback(Transaction *transaction)
{
	try {
		stldb::timer t("Database::rollback(Transaction *)");
		safety_check();

		transaction->rollback(_container_proxies);
		transaction->unlock_database();
	}
	catch ( ... ) {
		// If any exception is throw, still release the _dbinfo->transaction_mutex
		transaction->unlock_database();
		throw;
	}
	return 0;
}


template <class ManagedRegionType>
transaction_id_t Database<ManagedRegionType>::checkpoint()
{
	safety_check();
	std::map<void*, container_proxy_type*> containers;
	transaction_id_t start_lsn;
	{ 	// lock scope - to protect Database data structures
		scoped_lock<mutex_type> guard(_dbinfo->mutex);
		containers = _container_proxies;
		start_lsn = _dbinfo->logInfo._last_write_txn_id;
	}

	// This declaration of scoped allocation is a precaution, in case the K or V type
	// uses an allocator, in which case it may need to use a scope_aware_allocator
	// or similar construct that supports a default constructor, based on how trans_map::save_checkpoint
	// is written.
	stldb::scoped_allocation<typename ManagedRegionType::segment_manager> default_alloc( _region->get_segment_manager() );
	region_allocator_t alloc( _region->get_segment_manager() );

	// At present the checkpoint is done sequentially, one container at a time.
	for (typename std::map<void*, container_proxy_type*>::iterator i = containers.begin();
		 i != containers.end(); i++)
	{
		// Make sure that there has been at least one change since the last checkpoint
		// done for this container, as seen by an increase in the last written lsn.
		scoped_lock<mutex_type> guard(_dbinfo->mutex);
		transaction_id_t my_start_lsn = _dbinfo->logInfo._last_write_txn_id;
		typename DatabaseInfo<region_allocator_t, mutex_type>::shm_string
				container_name( i->second->getName().c_str(), alloc );
		typename DatabaseInfo<region_allocator_t, mutex_type>::ckpt_history_map_t::iterator
				entry = _dbinfo->ckpt_history_map.find( container_name );
		if (entry != _dbinfo->ckpt_history_map.end() &&
				entry->second == my_start_lsn )
		{
			STLDB_TRACE(fine_e, "Skipping checkpoint of " << i->second->getName() << ". There has been no DB activity since its last checkpoint.")
			continue;
		}
		guard.unlock();

		// checkpoint filename structure:  <ContainerName>.<LSN>.ckpt
		std::string tempfilename = detail::checkpoint_work_filename(
				_dbinfo->checkpoint_directory.c_str(),
				i->second->getName().c_str(), my_start_lsn);
		STLDB_TRACE(finer_e, "Starting checkpoint of " << i->second->getName() << " to file " << tempfilename << " as of LSN: " << my_start_lsn );
		std::ofstream checkfile( tempfilename.c_str() );

		try {
			i->second->save_checkpoint(checkfile);
		}
		catch (...) {
			STLDB_TRACE(error_e, "Exception during checkpoint write for container: " << i->second->getName());
			checkfile.close();
			continue;
		}

		// upon completing the write of all data, we can close the checkpoint file.
		// and remove the _wip from it's name.
		guard.lock();
		transaction_id_t my_end_lsn = _dbinfo->logInfo._last_write_txn_id;
		_dbinfo->ckpt_history_map[ container_name ] = my_start_lsn;
		guard.unlock();

		checkfile.close();
		STLDB_TRACE(finer_e, "Checkpoint write completed.");

		// rename the file to it's final (completed) filename.
		detail::complete_checkpoint_file(tempfilename, my_end_lsn);
	}
	return start_lsn;
}


template <class ManagedRegionType>
std::vector<boost::filesystem::path> Database<ManagedRegionType>::get_archivable_logs()
{
	safety_check();
	return detail::get_archivable_logs(get_checkpoint_directory(), get_logging_directory());
}


template <class ManagedRegionType>
std::vector<checkpoint_file_info> Database<ManagedRegionType>::get_archivable_checkpoints()
{
	safety_check();
	return detail::get_archivable_checkpoints(get_checkpoint_directory());
}


template <class ManagedRegionType>
std::vector<boost::filesystem::path> Database<ManagedRegionType>::get_current_logs()
{
	safety_check();
	return detail::get_current_logs(get_checkpoint_directory(), get_logging_directory());
}


template <class ManagedRegionType>
std::map<std::string,checkpoint_file_info> Database<ManagedRegionType>::get_current_checkpoints()
{
	safety_check();
	return detail::get_current_checkpoints(get_checkpoint_directory());
}






} // namespace stldb;

