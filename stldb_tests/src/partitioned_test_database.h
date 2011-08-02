
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

// An example database for testing purposes.
template <class region_type, class map_type>
class PartitionedTestDatabase {
public:
	typedef Database<region_type> db_type;

	// Constructor
	PartitionedTestDatabase(const char *name, const char *db_directory=NULL,
			const char *checkpoint_directory=NULL, const char *log_directory=NULL,
			int partitions=20)
		: db(NULL), _partitions(partitions),
		  map( new map_type*[partitions] ), diskless(false)
	{
		// The "schema" of the database - a set of containers of various types.

		std::list<container_proxy_base<region_type>*> schema;
		char sname[255];
		for (int i=0; i<_partitions; i++) {
			sprintf(sname, "P%d", i);
			schema.push_back(new container_proxy<region_type, map_type>(sname));
		}

		// Get root directory
		std::string root_dir( properties.getProperty<std::string>("rootdir",std::string(".")) );
		std::string db_dir( db_directory ? db_directory : root_dir.c_str() );
		std::string checkpoint_dir( checkpoint_directory ? checkpoint_directory : (root_dir + "/checkpoint").c_str());
		std::string log_dir( log_directory ? log_directory : (root_dir + "/log").c_str());

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
				, db_dir.c_str()
				, region_size   // the initial database size.
				, NULL       	// fixed mapping address (optional) 0x0 -> OS chooses address
				, checkpoint_dir.c_str()   // where the persistent containers are stored
				, log_dir.c_str()	// where the log files are written
				, max_log  		// max log fil length
				, sync_logging 	// synchronous logging
				, schema     	// the containers you want created/opened in the database
			);

			// Get the handles to our now created or loaded containers
			for (int i=0; i<_partitions; i++) {
				sprintf(sname, "P%d", i);
				map[i] = db->template getContainer<map_type>(sname);
				assert(map[i] != NULL);
			}
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
			std::cerr << "A std::exception was thrown..." << std::endl;
			std::cerr << x.what() << std::endl;
		}
	}

	~PartitionedTestDatabase() {
		if (db)
			this->close();
	}

	void close() {
		stldb::database_stats stats = db->get_stats(false);
		std::cout << "Database Stats: " << std::endl
			<< "region_size: " << stats.region_size << std::endl
			<< "region_free: " << stats.region_free << std::endl
			<< "named objects: " << stats.num_named_objects << std::endl
			<< "unique objects: " << stats.num_unique_objects << std::endl;
		bool die = properties.getProperty("die", false);
		if (!die) {
			// shutdown properly.
			delete db;
		}
		db = NULL;
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

	map_type* getMap(int partition) {
		return map[partition%_partitions];
	}

private:
	Database<region_type> *db;
	int _partitions;
	map_type **map;
	bool diskless;

};

#endif
