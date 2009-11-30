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

#include <stldb/stldb.hpp>
#include <stldb/cachetypes.h>

namespace stldb {

/**
 * Structure used with the public database APIs regarding checkpoint files.
 */
struct checkpoint_file_info {
	std::string filename;
	std::string container_name;
	transaction_id_t lsn_at_start;
	transaction_id_t lsn_at_end;
};

/**
 * This namespace includes a set of utility functions which encapsulate the
 * conventions that STLdb uses when working with checkpoint and log files.
 */
namespace detail {

	// Get all log files found in the logging_directory
	BOOST_STLDB_DECL
	std::map<transaction_id_t,boost::filesystem::path> get_log_files(
			const boost::filesystem::path &logging_path);

	// Get all checkpoint files found in the checkpoint directory.
	BOOST_STLDB_DECL
	std::vector<checkpoint_file_info> get_checkpoints(
			const boost::filesystem::path &checkpoint_path);

	// Get all current checkpoints - being the checkpoint files which would
	// be used to load the respective containers at the start of db recovery
	// The resulting map is keyed by container name.
	BOOST_STLDB_DECL
	std::map<std::string,checkpoint_file_info> get_current_checkpoints(
			const boost::filesystem::path &checkpoint_path);

	// Get all archivable checkpoints; checkpoints which would not be used
	// during any subsequent database recovery because there are newer
	// versions available which would be used instead.
	BOOST_STLDB_DECL
	std::vector<checkpoint_file_info> get_archivable_checkpoints(
			const boost::filesystem::path &checkpoint_path);

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
	checkpoint_file_info get_checkpoint_file_info(
			const boost::filesystem::path &fname);

	// Generates a full log filename based on the lsn passed.
	BOOST_STLDB_DECL
	std::string log_filename(const boost::filesystem::path &log_dir,
			transaction_id_t lsn);

	// Generates a temporary checkpoint filename based on the data passed
	BOOST_STLDB_DECL
	std::string checkpoint_work_filename(const boost::filesystem::path &checkpoint_dir,
			const char *container_name, transaction_id_t lsn_at_start);

	// Completes a checkpoint by renaming it and incorporating lsn_at_end into the name
	BOOST_STLDB_DECL
	void complete_checkpoint_file(const boost::filesystem::path &tempfilename,
			transaction_id_t lsn_at_end);

} // namespace fileutil

} // namespace stldb

#endif /* DB_FILE_UTIL_H_ */
