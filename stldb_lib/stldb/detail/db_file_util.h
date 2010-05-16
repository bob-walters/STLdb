/*
 * db_file_util.h
 *
 *  Created on: Aug 6, 2009
 *      Author: rwalter3
 */

#ifndef DB_FILE_UTIL_H_
#define DB_FILE_UTIL_H_

#include <string>
#include <map>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/serialization/map.hpp>

#include <stldb/stldb.hpp>
#include <stldb/cachetypes.h>
#include <stldb/detail/checkpoint_file_info.h>
#include <stldb/detail/os_file_functions_ext.h>

namespace stldb {


/**
 * This namespace includes a set of utility functions which encapsulate the
 * conventions that STLdb uses when working with checkpoint and log files.
 */
namespace detail {

	// Get all log files found in the logging_directory
	BOOST_STLDB_DECL
	std::map<transaction_id_t,boost::filesystem::path> get_log_files(
			const boost::filesystem::path &logging_path);

	// Get all checkpoint meta files found in the checkpoint directory.
	// return them as a vector<checkpoint_file_info>
	BOOST_STLDB_DECL
	std::vector<checkpoint_file_info> get_checkpoints(
			const boost::filesystem::path &checkpoint_path);

	// Get all current checkpoints - being the checkpoint files which would
	// be used to load the respective containers at the start of db recovery
	// The resulting map is keyed by container name.
	BOOST_STLDB_DECL
	std::map<std::string,checkpoint_file_info> get_current_checkpoints(
			const boost::filesystem::path &checkpoint_path);

	// Return the path of the current metafile for the container_name
	BOOST_STLDB_DECL
	boost::filesystem::path get_current_metafile(
			const boost::filesystem::path &checkpoint_path,
			const char * container_name);

	// For the current set of checkpoint files for all containers in the database,
	// returns the range of transactions which would be partially restored
	// when those checkpoints are loaded.  i.e. pair.first is the beginning of
	// this range and denotes where log file recovery must begin.  pair.second
	// is the end of this range, and denotes the minimum log-based recovery which
	// must be accomplished in order to have the database in a consistent state.
	// The failure to recover transactions after pair.second does not put the
	// database in an inconsistent state, rather transactions have simply been lost.
	BOOST_STLDB_DECL
	std::pair<transaction_id_t,transaction_id_t> get_checkpoint_lsn_range(
			const boost::filesystem::path &checkpoint_path);

	// Returns the current set of logs - the ones which would be used during a
	// subsequent recovery operation.  That set has to be determined relative to the
	// existing checkpoints, which is why checkpoint_dir is also passed.
	BOOST_STLDB_DECL
	std::vector<boost::filesystem::path> get_current_logs(
			const boost::filesystem::path &checkpoint_dir,
			const boost::filesystem::path &logging_dir);

	// Return the first logfile, from the set passed, which is needed for recovery
	// based on the checkpoints which exist in checkpoint_dir.
	BOOST_STLDB_DECL
	std::map<transaction_id_t,boost::filesystem::path>::iterator get_first_needed(
			std::map<transaction_id_t,std::string> &logfiles,
			const boost::filesystem::path &checkpoint_dir );

	BOOST_STLDB_DECL
	std::vector<boost::filesystem::path> get_archivable_logs(
			const boost::filesystem::path &checkpoint_dir,
			const boost::filesystem::path &logging_dir);

	// Return the starting LSN of a log file.  This value can be derived from its name.
	BOOST_STLDB_DECL
	transaction_id_t get_logfile_starting_lsn(
			const boost::filesystem::path &fname);

	// Return the checkpoint info for the checkpoint file passed.
	BOOST_STLDB_DECL
	void get_checkpoint_file_info(
			const boost::filesystem::path &fname,
			checkpoint_file_info &info);

	// Generates a full log filename based on the lsn passed.
	BOOST_STLDB_DECL
	std::string log_filename(
			const boost::filesystem::path &log_dir,
			transaction_id_t lsn);


} // namespace fileutil

} // namespace stldb

#endif /* DB_FILE_UTIL_H_ */
