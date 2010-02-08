/*
 * database_registry.h
 *
 *  Created on: Jan 17, 2009
 *      Author: bobw
 */

#ifndef DATABASE_REGISTRY_H_
#define DATABASE_REGISTRY_H_

#include <vector>
#include <map>
#include <set>

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/detail/os_thread_functions.hpp> //getpid
#include <boost/filesystem.hpp>
#include <boost/serialization/vector.hpp>

#include <stldb/sync/file_lock.h>
#include <stldb/containers/string_serialize.h>

using boost::interprocess::allocator;
using boost::interprocess::vector;
using boost::interprocess::basic_string;
using boost::interprocess::detail::OS_process_id_t;
using boost::interprocess::detail::get_current_process_id;


namespace stldb {

/**
 * Object in shared memory region.  This object is used to help track the validity of
 * the database.
 */
template <class void_alloc_t>
class database_registry {
private:
	// Confirms that the database_registry constructor completed.
	// used for detecting process aborts during construction
	bool _registry_construct_complete;

    typedef typename void_alloc_t::template rebind<char>::other  char_alloc_t;
	typedef boost::interprocess::basic_string<char, std::char_traits<char>, char_alloc_t>  shm_string;
    shm_string  _database_dir;
    shm_string  _database_name;

    typedef typename void_alloc_t::template rebind<OS_process_id_t>::other  pid_alloc_t;
	boost::interprocess::vector<OS_process_id_t, pid_alloc_t>  _registry;

    // This is set to true once some thread successfully completes the initialization of
    // the database containing this region.
    bool _database_construct_complete;

    // Indicates that the region needs to undergo recovery processing.
    bool _database_invalid;

public:

	//! Construct a database registry for db_name within the region that alloc
	//~ refers to
	database_registry(const char *database_dir, const char *db_name, const void_alloc_t &alloc)
		: _registry_construct_complete(false)
        , _database_dir(database_dir,char_alloc_t(alloc))
		, _database_name(db_name,char_alloc_t(alloc))
        , _registry( pid_alloc_t(alloc) )
		, _database_construct_complete(false)
		, _database_invalid(false)
		{
			_registry.reserve(256);
			_registry_construct_complete = true;
		}

	// method which yields the name of the registry lock file for a given database.
	static std::string filelock_name(const char *database_dir, const char *db_name) {
		boost::filesystem::path lockpath( database_dir );
		lockpath /= db_name;
		lockpath.replace_extension(".reglock");
		return lockpath.string();
	}

	~database_registry() { }

	// Check to see if this Database may need recovery.  A Database is deemed to
	// need recovery whenever any previously running process disconnects from the
	// database abruptly, i.e. without a call to the destructor of the Database.
	// If false is returned, the _database_invalid flag is set in the registry.
	bool check_need_for_recovery();

	// Wait for all processes connected to the Database to disconnect from it.
	// Requires that called pass in the file lock they already hold on the registry's file lock
	// (as returned via lock_name)
	void await_complete_disconnect( boost::interprocess::scoped_lock<stldb::file_lock> &held_lock );

	// Register the calling process.  As part of this, a file lock is
	// acquired on behalf of the current process and returned.
	stldb::file_lock* register_pid();

	// Unregister the calling process.  The file lock returned from a
	// previous call to register_pid() is passed.  Note that lock is deleted by this call.
	void unregister_pid( stldb::file_lock *lock );

	// Return the number of processes connected to the
	int connected_pids() {
		return _registry.size();
	}

	// Mark that the construction of a region is complete
	void construction_complete() {
		_database_construct_complete = true;
	}

	// set/clear the regions invalid indicator
	void set_invalid( bool value ) {
		_database_invalid = value;
	}

	// return whether the database is current valid.
	bool is_valid() {
		return (!_registry_construct_complete || !_database_invalid);
	}

	// return the number of attached processes.
	int attached_pids() {
		if (!_registry_construct_complete)
			return 0;
		return _registry.size();
	}

	template <class Archive>
    void serialize(Archive & ar, const unsigned int /* file_version */ ){
        std::vector<OS_process_id_t> registry;
    	registry.insert( registry.end(), _registry.begin(), _registry.end() );
        int attached_pids = this->attached_pids();
        bool recovery_needed = this->check_need_for_recovery();

        ar & BOOST_SERIALIZATION_NVP(_registry_construct_complete)
           & BOOST_SERIALIZATION_NVP(_database_dir)
           & BOOST_SERIALIZATION_NVP(_database_name)
           & BOOST_SERIALIZATION_NVP( attached_pids)
           & BOOST_SERIALIZATION_NVP( registry)
           & BOOST_SERIALIZATION_NVP( recovery_needed)
           & BOOST_SERIALIZATION_NVP(_database_construct_complete)
           & BOOST_SERIALIZATION_NVP(_database_invalid);
    }

private:
	database_registry(); // not allowed
	database_registry(const database_registry &);
	database_registry &operator=(const database_registry &);
};


} // namespace stldb

#ifndef AUTO_TEMPLATE
#include <stldb/database_registry.tcc>
#endif

#endif /* DATABASE_REGISTRY_H_ */
