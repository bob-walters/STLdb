/*
 * database_registry.cpp
 *
 *  Created on: Jan 17, 2009
 *      Author: bobw
 */

#include <algorithm>
#include <stldb/database_registry.h>
#include <stldb/exceptions.h>
#include <stldb/trace.h>

namespace stldb {


// Predicate used by check_need_for_recovery
// Checks if the pid at the iterator position still has its
// expected file lock.
struct CheckLock: public std::unary_function<OS_process_id_t, bool>
{
	bool *invalid_ind;
	std::string m_database_name;

	bool operator()(OS_process_id_t pid)
	{
		if (pid == boost::interprocess::detail::get_current_process_id())
			return false;

		bool result = true;
		std::ostringstream s;
		s << m_database_name << ".pid." << pid;
		std::string filename = s.str();

		// must use a file lock which doesn't create the file if it doesn't exist.
		// This is true of the boost::interprocess::file_lock class, but not the stldb one.
		try {
			boost::interprocess::file_lock flock(filename.c_str());
			result = flock.try_lock();
			if (result)
				flock.unlock();
			else
				STLDB_TRACE(error_e, "registry check: process " << pid << " is still connected");
		}
		catch (...)
		{
			// This probably means the file doesn't exit.
			STLDB_TRACE(error_e, "registry check: exception on file_lock constructor for " << filename);
			return true;
		}
		if (result)
		{
			STLDB_TRACE(error_e, "registry check: process " << pid << " disconnected from " << m_database_name << " improperly.");
			*invalid_ind = true;
			// release our lock, we have confirmed that pid is not holding
			// its lock file
			try {
				boost::filesystem::remove(s.str());
			}
			catch (boost::filesystem::filesystem_error &ex) {
				STLDB_TRACE(warning_e, "error removing registry lock file: " << filename << " (debris left in filesystem): " << ex.what() );
			}
		}
		return result;
	}
};

// Check to see if this Database is valid.
template <class void_alloc_t>
bool database_registry<void_alloc_t>::check_need_for_recovery()
{
	if (!_registry_construct_complete) {
		STLDB_TRACE(error_e, "Previous registry constructor did not complete.  Need for recovery has been detected!  Database_invalid_flag has been set. (" << _database_name.c_str() << ")");
		return true;
	}

	boost::filesystem::path fullname( _database_dir.c_str() );
	fullname /= _database_name.c_str();

	stldb::CheckLock predicate;
	predicate.m_database_name = fullname.string();
	predicate.invalid_ind = &_database_invalid;

	typename boost::interprocess::vector<OS_process_id_t, pid_alloc_t>::iterator end
		= std::remove_if(_registry.begin(), _registry.end(), predicate);

	bool dead_processes = (end != _registry.end());
	_registry.erase(end, _registry.end());

	// if _database_invalid then always return true;
	if (_database_invalid || !_database_construct_complete)
		dead_processes = true;

	if ( dead_processes )
		STLDB_TRACE(error_e, "Need for recovery has been detected!  Database_invalid_flag has been set. (" << _database_name.c_str() << ")");
	return dead_processes;
}

// Wait for all processes connected to the Database to disconnect from it.
template <class void_alloc_t>
void database_registry<void_alloc_t>::await_complete_disconnect(
		boost::interprocess::scoped_lock<stldb::file_lock> &held_lock)
{
	if (!_registry_construct_complete)
		return; // nothing could be registered.

	// Start waiting for processes to disconnect
	// Note: The calling process is not yet in the registry.
	bool waiting_on_mypid = false;
	OS_process_id_t pid = boost::interprocess::detail::get_current_process_id();
	while (_registry.size() > 0) {
		// Message about other running processes.
		std::ostringstream pidlist;
		for (unsigned int i=0; i<_registry.size(); i++) {
			pidlist << _registry[i] << (i==_registry.size()-1 ? "" : ", ");
			if (_registry[i] == pid)
				waiting_on_mypid = true;
		}
		STLDB_TRACE(info_e, "Awaiting the disconnect of pids: [" << pidlist.str() << "]");

		if (waiting_on_mypid) {
			stldb_exception ex("Usage Error: Process cannot delete region or reconnect to it while previous instance is open.  Close existing Database instance before trying to construct another or call to Database::remove()");
			STLDB_TRACE(error_e, ex.what());
			throw ex;
		}

		// pick the first pid in the list.
		int pid = _registry[0];

		boost::filesystem::path fullname( _database_dir.c_str() );
		fullname /= _database_name.c_str();
		std::ostringstream s;
		s << fullname.string() << ".pid." << pid;
		std::string lockfilename( s.str() );

		// release the file lock on the registry, this will make it possible for
		// the other process to call unregister_pid().
		held_lock.unlock();

		// Now wait for the process to unlock it's pid file.
		try
		{
			// must use a file lock which doesn't create the file if it doesn't exist.
			// This is true of the boost::interprocess::file_lock class, but not the stldb one.
			boost::interprocess::file_lock flock(lockfilename.c_str());
			flock.lock(); // will wait if file exists and is locked.
			// lock released once flock goes out of scope.
		}
		catch (...) {
			// If the flock.lock() throws, it is probably because the file was deleted.
		}
		// re-acquire our registry lock & continue.
		held_lock.lock();

		// We either wake up because a running process calls unregister or because it dies.
		// Calling this method again will weed out any additional process deaths.
		this->check_need_for_recovery();
	}
}

template <class void_alloc_t>
file_lock* database_registry<void_alloc_t>::register_pid()
{
	OS_process_id_t pid = boost::interprocess::detail::get_current_process_id();

	// safety mechanism, to prevent an application from opening multiple
	// copies of an environment within one app.  (undesirable, and known
	// to cause race conditions on calls to methods like checkpoint.)
	for (unsigned int i=0; i<_registry.size(); i++) {
		if (_registry[i] == pid) {
			std::ostringstream why;
			why << "Process " << pid << " is attempting open of already opened cache environment " << _database_dir.c_str() << "/" << _database_name.c_str() << ", without closing the previous Database instance.  Found " << pid << " already in the process registry (database_registry::register_pid())";
			STLDB_TRACE(error_e, why.str());
			throw stldb_exception(why.str().c_str());
		}
	}

	_registry.push_back(pid);

	std::ostringstream s;
	s << _database_name.c_str() << ".pid." << pid;
	boost::filesystem::path lockpath( _database_dir.c_str() );
	lockpath /= s.str();

	file_lock *flock = new file_lock( lockpath.string().c_str() );
	flock->lock();
	return flock;
}

template <class void_alloc_t>
void database_registry<void_alloc_t>::unregister_pid(file_lock *flock)
{
	// processes attempt to unregister.
	OS_process_id_t my_pid = boost::interprocess::detail::get_current_process_id();
	typename boost::interprocess::vector<OS_process_id_t, pid_alloc_t>::iterator i =
			std::find(_registry.begin(), _registry.end(), my_pid);
	if ( i != _registry.end() ) {
		_registry.erase(i);
	}

	flock->unlock();
	std::string fname( flock->filename() );
	delete flock; // should close the file

	try {
		boost::filesystem::remove(fname);
	}
	catch (boost::filesystem::filesystem_error &ex) {
		STLDB_TRACE(warning_e, "error removing registry lock file: " << fname << " (debris left in filesystem): " << ex.what() );
	}
}


} // namespace

