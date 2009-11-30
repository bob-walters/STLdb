
#ifndef STLDB_TEST_DATABASE_H
#define STLDB_TEST_DATABASE_H

#include <cassert>
#include <map>
#include <string>

#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <stldb/allocators/swap_workaround.h>

#include <stldb/Database.h>
#include <stldb/allocators/scope_aware_allocator.h>
#include <stldb/containers/trans_map.h>
#include <stldb/containers/string_serialize.h>
#include <stldb/allocators/allocator_gnu.h>

#include "properties.h"

using boost::interprocess::allocator;
using boost::interprocess::managed_mapped_file;
using boost::interprocess::basic_string;
using boost::interprocess::vector;
using boost::interprocess::map;

using stldb::Database;
using stldb::container_proxy_base;
using stldb::container_proxy;
using stldb::Transaction;

//Named object creation managed memory segment
//All objects are constructed in the memory-mapped file
//   Names are c-strings,
//   Default memory management algorithm(rbtree_best_fit with no mutexes)
//   Name-object mappings are stored in the iset_index
//	 POINTER TYPE is void*
typedef boost::interprocess::basic_managed_mapped_file <
   char,
   boost::interprocess::rbtree_best_fit<boost::interprocess::mutex_family, void*>,
   boost::interprocess::iset_index
>  fixed_managed_mapped_file;


// An example database for testing purposes.
template <class region_type, class map_type>
class TestDatabase {
public:
	typedef Database<region_type> db_type;

	// Constructor
	TestDatabase(const char *name, const char *db_directory=NULL,
		const char *checkpoint_directory=NULL, const char *log_directory=NULL )
		: db(NULL), map(NULL), diskless(false)
	{
		// The "schema" of the database - a set of containers of various types.

		std::list<container_proxy_base<region_type>*> schema;
		schema.push_back(new container_proxy<region_type, map_type> (
			"TheMapTable"));

		// Get root directory
		std::string root_dir( properties.getProperty<std::string>("rootdir",std::string(".")) );
		std::string db_dir( db_directory ? db_directory : root_dir.c_str() );
		std::string checkpoint_dir( checkpoint_directory ? checkpoint_directory : (root_dir + "\\checkpoint").c_str());
		std::string log_dir( log_directory ? log_directory : (root_dir + "\\log").c_str());

		// Get the diskless setting
		diskless = properties.getProperty("diskless",false);

		std::string propName = std::string("region_size.") + name;
		long region_size = properties.getProperty(propName.c_str(),
				properties.getProperty("region_size", (long)256*1024*1024));

		propName = std::string("max_log.") + name;
		long max_log = properties.getProperty(propName.c_str(),
				properties.getProperty("max_log", (long)256*1024*1024));

		propName = std::string("sync_logging.") + name;
		bool sync_logging = properties.getProperty(propName.c_str(),
				properties.getProperty("sync_logging", false));

		// The constructor just establishes the configuration data which will apply to the
		// database.
		try
		{
			db = new Database<region_type>(
				stldb::open_create_or_recover	// open existing, create, or recover
				, name			// the name of this database (used for region name)
				, db_dir.c_str()	// the directory to create everything in.)
				, region_size   // the initial database size.
				, NULL       	// fixed mapping address (optional) 0x0 -> OS chooses address
				, checkpoint_dir.c_str()   // where the persistent containers are stored
				, log_dir.c_str()	// where the log files are written
				, max_log  		// max log fil length
				, sync_logging 	// synchronous logging
				, schema     	// the containers you want created/opened in the database
			);

			// Get the handles to our now created or loaded containers
			map = db->template getContainer<map_type>("TheMapTable");
		}
		catch( std::bad_alloc &x )
		{
			std::cerr << "Caught a bad_alloc exception" << std::endl;
			std::cerr << x.what() << std::endl;
		}
		catch( boost::interprocess::bad_alloc &x )
		{
			std::cerr << "Caught an boost::interprocess::bad_alloc exception" << std::endl;
			std::cerr << x.what() << std::endl;
			std::cerr << x.get_error_code() << std::endl;
			std::cerr << x.get_native_error() << std::endl;
		}
		catch( boost::interprocess::interprocess_exception &x )
		{
			std::cerr << "Caught an interprocess_exception (from boost::interprocess library)" << std::endl;
			std::cerr << x.what() << std::endl;
			std::cerr << x.get_error_code() << std::endl;
			std::cerr << x.get_native_error() << std::endl;
		}
		catch( std::exception &x )
		{
			std::cerr << "An exception was thrown..." << std::endl;
			std::cerr << x.what() << std::endl;
		}
	}

	~TestDatabase() {
		if (db)
			this->close();
	}

	Transaction* beginTransaction(Transaction *reusable_t = NULL) {
		return db->beginTransaction(reusable_t);
	}

	int commit( Transaction *t ) {
		return db->commit(t, diskless);
	}

	int rollback( Transaction *t ) {
		return db->rollback(t);
	}

	Database<region_type> *getDatabase() {
		return db;
	}

	region_type& getRegion() {
		return db->getRegion();
	}

	map_type* getMap() {
		return map;
	}

	void close(bool final_checkpoint = false) {
		stldb::database_stats stats = db->get_stats(false);
		std::cout << "Database Stats: " << std::endl
			<< "region_size: " << stats.region_size << std::endl
			<< "region_free: " << stats.region_free << std::endl
			<< "named objects: " << stats.num_named_objects << std::endl
			<< "unique objects: " << stats.num_unique_objects << std::endl;
		bool die = properties.getProperty("die", false);
		if (!die || final_checkpoint) {
			// shutdown properly.
			db->close(final_checkpoint);
			delete db;
			db = NULL;
		}
	}

private:
	Database<region_type> *db;
	map_type *map;
	bool diskless;

};

#endif
