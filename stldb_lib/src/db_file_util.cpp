/*
 * db_file_util.cpp
 *
 *  Created on: Aug 6, 2009
 *      Author: rwalter3
 */
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>

#define BOOST_STLDB_SOURCE
#include <stldb/stldb.hpp>
#include <stldb/cachetypes.h>
#include <stldb/trace.h>
#include <stldb/detail/db_file_util.h>
#include <stldb/logging.h>

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
typedef boost::archive::binary_oarchive boost_oarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
typedef boost::archive::text_oarchive boost_oarchive_t;
#endif

using boost::filesystem::path;
using boost::filesystem::directory_iterator;

namespace stldb {
namespace detail {

// standard file extensions.
static const char *ckpt = ".ckpt";
static const char *meta = ".meta";
static const char *meta_wip = ".meta_wip";

BOOST_STLDB_DECL
std::map<transaction_id_t,boost::filesystem::path> get_log_files(
		const boost::filesystem::path &logging_path)
{
	std::map<transaction_id_t,boost::filesystem::path> result;
	if ( !exists( logging_path ) )
		return result; // empty map

	directory_iterator end_itr; // default construction yields past-the-end
	for ( directory_iterator itr( logging_path ); itr != end_itr; ++itr )
	{
		if ( !is_directory(itr->status()) )
		{
			// checkpoint filename structure:  L<LSN>.log
			transaction_id_t starting_lsn = get_logfile_starting_lsn( itr->path() );
			if (starting_lsn != -1) {
				result.insert( std::make_pair(starting_lsn, itr->path()) );
				STLDB_TRACE(finest_e, "found log file: " << itr->path().filename());
			}
		}
    }
	return result;
}

BOOST_STLDB_DECL
std::vector<checkpoint_file_info> get_checkpoints(
		const boost::filesystem::path &checkpoint_path)
{
	std::vector<checkpoint_file_info> result;
	if ( !exists( checkpoint_path ) )
		return result; // empty result.

	directory_iterator end_itr; // default construction yields past-the-end
	checkpoint_file_info info;
	for ( directory_iterator itr( checkpoint_path ); itr != end_itr; ++itr )
	{
		if ( !is_directory(itr->status()) && itr->path().extension() == meta )
		{
			// found a metafile per naming convention: <Container>.<LSN>.meta
			try {
				get_checkpoint_file_info( itr->path(), info );
			}
			catch (...) {
				STLDB_TRACE(warning_e, "found *.meta file with invalid contents: " << itr->path() );
			}
			result.push_back( info );
			STLDB_TRACE(finest_e, "found checkpoint metafile: " << info.meta_filename);
	    }
	}
	return result;
}

BOOST_STLDB_DECL
std::map<std::string,checkpoint_file_info> get_current_checkpoints(
		const boost::filesystem::path &checkpoint_path)
{
	std::map<std::string,checkpoint_file_info> result;

	std::vector<checkpoint_file_info> chkpts = get_checkpoints(checkpoint_path);
	for (std::vector<checkpoint_file_info>::iterator i = chkpts.begin(); i != chkpts.end(); i++ ) {

		std::map<std::string,checkpoint_file_info>::iterator ci = result.find(i->container_name);
		if (ci == result.end() ) {
			result.insert( make_pair(i->container_name, *i) );
			STLDB_TRACE(finer_e, "detected checkpoint metafile: " << i->meta_filename);
		}
		else if ( i->lsn_at_start > ci->second.lsn_at_start ) {
			STLDB_TRACE(finer_e, "detected more recent checkpoint metafile: " << i->meta_filename << ", supersedes : " << ci->second.meta_filename);
			ci->second = *i;
		}
	}
	return result;
}

BOOST_STLDB_DECL
boost::filesystem::path get_current_metafile(
		const boost::filesystem::path &checkpoint_path,
		const char* container_name)
{
	boost::filesystem::path result;
	if ( !exists( checkpoint_path ) )
		return result; // empty result.

	transaction_id_t startlsn = 0;
	checkpoint_file_info metainfo;

	directory_iterator end_itr; // default construction yields past-the-end
	checkpoint_file_info info;
	for ( directory_iterator itr( checkpoint_path ); itr != end_itr; ++itr )
	{
		if ( !is_directory(itr->status()) && itr->path().extension() == meta ) {
			std::string filename( itr->path().filename() );
			if (filename.find(container_name) == 0) {
				get_checkpoint_file_info(itr->path(), metainfo);
				if (metainfo.lsn_at_start > startlsn) {
					result = itr->path();
					startlsn = metainfo.lsn_at_start;
				}
			}
		}
	}
	return result;
}


BOOST_STLDB_DECL
std::pair<transaction_id_t,transaction_id_t> get_checkpoint_lsn_range(
		const boost::filesystem::path &checkpoint_dir)
{
	typedef std::map<std::string,checkpoint_file_info> checkpoint_map_t;
	checkpoint_map_t chkpts = get_current_checkpoints(checkpoint_dir);
	if (chkpts.size()==0)
		return std::pair<transaction_id_t,transaction_id_t>(0,0);

	std::pair<transaction_id_t,transaction_id_t> result( std::numeric_limits<transaction_id_t>::max(), 0 );
	for ( checkpoint_map_t::iterator i=chkpts.begin(); i!=chkpts.end(); i++ ) {
		if ( i->second.lsn_at_start < result.first )
			result.first = i->second.lsn_at_start;
		if ( i->second.lsn_at_end > result.second )
			result.second = i->second.lsn_at_end;
	}
	return result;
}

BOOST_STLDB_DECL
std::map<transaction_id_t,boost::filesystem::path>::iterator get_first_needed(
		std::map<transaction_id_t,boost::filesystem::path> &logfiles,
		const boost::filesystem::path &checkpoint_dir )
{
	transaction_id_t min_chkpt_txn = get_checkpoint_lsn_range(checkpoint_dir).first;

	std::map<transaction_id_t,boost::filesystem::path>::iterator i = logfiles.begin();
	for ( ; i != logfiles.end(); i++ ) {
		if ( i->first >= min_chkpt_txn ) {
			break;
		}
	}
	if ( i != logfiles.begin() && (i == logfiles.end() || i->first > min_chkpt_txn ))
		i --; // we will need the log file previous as well.
	STLDB_TRACE(finer_e, "determined first logfile needed for recovery: " << (i==logfiles.end() ? "None" : i->second.string()));
	return i;
}

BOOST_STLDB_DECL
std::vector<boost::filesystem::path> get_current_logs(
		const boost::filesystem::path &checkpoint_dir,
		const boost::filesystem::path &logging_dir)
{
	// Get a sorted list of existing log files from the recovery manager.
	typedef std::map<transaction_id_t,boost::filesystem::path> map_type;
	map_type logfiles = get_log_files(logging_dir);

	// Get the first one needed for recovery
	map_type::iterator i = get_first_needed(logfiles,checkpoint_dir);

	std::vector<boost::filesystem::path> result;
	for ( map_type::iterator j=i; j!=logfiles.end(); j++ ) {
		result.push_back(j->second);
		STLDB_TRACE(finer_e, "detected current logfile: " << j->second);
	}
	return result;
}

BOOST_STLDB_DECL
std::vector<boost::filesystem::path> get_archivable_logs(
		const boost::filesystem::path &checkpoint_dir,
		const boost::filesystem::path &logging_dir)
{
	// Get a sorted list of existing log files from the recovery manager.
	typedef std::map<transaction_id_t,boost::filesystem::path> map_type;
	map_type logfiles = get_log_files(logging_dir);

	// Get the first one needed for recovery
	map_type::iterator i = get_first_needed(logfiles,checkpoint_dir);

	std::vector<boost::filesystem::path> result;
	for ( map_type::iterator j=logfiles.begin(); j!=i; j++ ) {
		result.push_back(j->second);
		STLDB_TRACE(finer_e, "detected archiveable logfile: " << j->second);
	}
	return result;
}

// If the fname passed is a valid log filename, returns the LSN
// which can be inferred from its filename.  Otherwise,
// returns -1 if it doesn't look like a valid logfile.
BOOST_STLDB_DECL
transaction_id_t get_logfile_starting_lsn(const boost::filesystem::path &fname)
{
	// parse the pieces.
	char theL;
	transaction_id_t lsn = no_transaction;
	std::string suffix;
	std::istringstream fn( fname.filename() );
	fn >> theL >> std::hex >> lsn >> suffix;

	// If we got what we expected, we are done.
	if (theL == 'L' && suffix == ".log")
		return lsn;
	else
		return no_transaction;
}

BOOST_STLDB_DECL
void get_checkpoint_file_info(const boost::filesystem::path &metafilepath, checkpoint_file_info &info)
{
	// open the file and read its contents...
	std::ifstream metafile(metafilepath.string().c_str());
	boost_iarchive_t archive(metafile);

	archive & info;
}

BOOST_STLDB_DECL
std::string log_filename(const boost::filesystem::path &log_dir, transaction_id_t lsn)
{
	std::ostringstream fn;
	fn.fill('0');
	fn << "L" << std::right	<< std::setw(16) << std::hex << std::uppercase
		<< lsn << ".log";
	boost::filesystem::path fullname( log_dir / fn.str() );
	return fullname.string();
}



} // namespace detail
} // namespace stldb


