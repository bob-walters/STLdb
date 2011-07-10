/*
 *  logger.h
 *  ACIDCache
 *
 *  Created by Bob Walters on 2/24/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef STLDB_LOGGER_H
#define STLDB_LOGGER_H 1

#include <cstdlib>
#include <ios>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>

#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/streams/vectorstream.hpp>

#include <stldb/cachetypes.h>
#include <stldb/commit_buffer.h>
#include <stldb/containers/string_compare.h>
#include <stldb/logging.h>
#include <stldb/detail/os_file_functions_ext.h>
#include <stldb/timing/timer.h>
#include <stldb/statistics.h>
#include <stldb/trace.h>
#include <stldb/detail/db_file_util.h>

using boost::interprocess::basic_string;
using boost::interprocess::basic_ovectorstream;

using std::cout;
using std::endl;

namespace stldb {


/**
 * The Logging information kept in shared memory
 */
template <class void_alloc_t, class mutex_type>
struct SharedLogInfo
{
	typedef typename void_alloc_t::template rebind<char>::other  shm_char_alloc_t;
	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_alloc_t>  shm_string;

	// For synchronization
	mutex_type      _queue_mutex;
	mutex_type      _file_mutex;

	// This sequence value is used to ensure that the order of disk writes
	// reflects the order of transaction commits.
	transaction_id_t _next_lsn;

	// Info about what has been written, and synced to disk...
	transaction_id_t _last_write_txn_id;
	transaction_id_t _last_sync_txn_id;

	// queue of transactions waitng for disk write.
	typedef stldb::commit_buffer_t<void_alloc_t>*  waiting_write;
	typedef typename void_alloc_t::template rebind<waiting_write>::other deque_alloc_t;
	boost::interprocess::deque<waiting_write,deque_alloc_t> waiting_txns;	// transactions waiting to enter log().

	// Configured log directory and current filename
	shm_string  log_dir;		// config - directory to contain log files
	shm_string  log_filename;	// form: LXXXXXXXXXXXXXXXX.log, where 16X = the 64-bit value of the first txn_id in the log

	boost::interprocess::offset_t   log_len;        // current - current len.
	boost::interprocess::offset_t	log_max_len;    // config - max len of any one log file

	bool        log_sync;       // config - should disk sync be done after write?

	struct log_stats  stats;	// stats

	// Meant to be constructed within a shared region.
	SharedLogInfo(const void_alloc_t &alloc)
		: _queue_mutex(), _file_mutex()
		, _next_lsn(1)
		, _last_write_txn_id(0), _last_sync_txn_id(0)
		, waiting_txns(alloc)
		, log_dir(alloc), log_filename(alloc)
		, log_len(0), log_max_len(256*1024*1024)
		, log_sync(true), stats()
		{ }

	// provide for serialization of an XML-based form of DatabaseInfo contents
	template <class Archive>
	void serialize(Archive &ar, const unsigned int version)
	{
		std::size_t num_waiting_txns = waiting_txns.size();
		ar & BOOST_SERIALIZATION_NVP(_next_lsn)
		   & BOOST_SERIALIZATION_NVP(_last_write_txn_id)
		   & BOOST_SERIALIZATION_NVP(_last_sync_txn_id)
		   & BOOST_SERIALIZATION_NVP( num_waiting_txns)
		   & BOOST_SERIALIZATION_NVP(log_dir)
		   & BOOST_SERIALIZATION_NVP(log_filename)
		   & BOOST_SERIALIZATION_NVP(log_len)
		   & BOOST_SERIALIZATION_NVP(log_sync)
		   & BOOST_SERIALIZATION_NVP(stats);
	}
};

/**
 * The logger is invoked when transactions are committing.
 */
template <class void_alloc_t, class mutex_type>
class Logger
{
public:
	Logger()
		: padding_buffer()
		, _shm_info(NULL)
		, _logfd(0) /* TODO - Too OS specific an initializer? */
		, _my_log_filename()
		, _my_fp_offset(0)
	{ }

	/**
	 * Set the shared log info.
	 */
	void set_shared_info( SharedLogInfo<void_alloc_t,mutex_type> *log_info) {
		_shm_info = log_info;
	}

	/**
	 * Destructor closes this processes file handle to any open log file.
	 */
	~Logger()
	{
		if (!_my_log_filename.empty()) {
			boost::interprocess::detail::close_file(_logfd);
		}
	}

	// Called by higher levels of the stack to record that a diskless commit
	// has occurred.  This is for cases of
	void record_diskless_commit() {
		boost::interprocess::scoped_lock<mutex_type> file_lock_holder(_shm_info->_file_mutex);
		_shm_info->stats.total_commits++;
		_shm_info->stats.total_free_commits++;
	}

	// returns a copy of current logging statistics
	log_stats get_stats(bool reset=false) {
		boost::interprocess::scoped_lock<mutex_type> file_lock_holder(_shm_info->_file_mutex);
		log_stats result = _shm_info->stats;
		if (reset) {
			_shm_info->stats = log_stats(); // reset via re-construction
		}
		return result;
	}

	/**
	 * If there is no txn currently waiting for a shot at the log, then immediately
	 * grant the process access to the log.  Otherwise, it joins the waiting list.
	 */
	transaction_id_t queue_for_commit( stldb::commit_buffer_t<void_alloc_t> *buff )
	{
		stldb::timer t1("Logger::queue_for_commit");

		// sanity test (debugging)
		if (buff->size()==0 || buff->op_count==0) {
			std::ostringstream error;
			error << "Assertion: detected commit buffer of size==0 or ops==0 in log() method";
			STLDB_TRACE(severe_e, error.str() );
			throw std::ios_base::failure( error.str() );
		}

		buff->prepare_header();

		boost::interprocess::scoped_lock<mutex_type> lock_holder(_shm_info->_queue_mutex);

		transaction_id_t lsn = _shm_info->_next_lsn++;
		buff->finalize_header(lsn);
		_shm_info->waiting_txns.push_back( buff );
		return lsn;
	}

	/**
	 * Acquire the right to write to the log file, and then write data from the commit
	 * queue as necessary until out buffer has been written.  Once the data has been
	 * written, optionally call sync() to wait for that data to make it out of OS
	 * memory and to the disk.
	 */
	void log( transaction_id_t my_commit_seq )
	{
		stldb::timer t1("Logger::log");
		boost::interprocess::scoped_lock<mutex_type> file_lock_holder(_shm_info->_file_mutex);

		uint64_t writes=0, txn_written=0, bytes=0, syncs=0;

		// we need to write commit buffers, until ours is written.
		while (_shm_info->_last_write_txn_id < my_commit_seq) {

			// ensure we have an open logfile, and if the current one has
			// reached its maximum size, open a new one.
			ensure_open_logfile();

			// get a set of commit buffers up the the limits allowed,
			// and write them all to disk.
			size_t txn_count = 0;
			transaction_id_t max_lsn = _shm_info->_last_write_txn_id;
			boost::interprocess::offset_t new_file_len = _shm_info->log_len;

			boost::interprocess::scoped_lock<mutex_type> queue_lock_holder(_shm_info->_queue_mutex);

			stldb::timer t4("fetch commit_buffers, do checksums");
			while (txn_count < max_txn_per_write
				   && !_shm_info->waiting_txns.empty()
				   && new_file_len < _shm_info->log_max_len ) {

				// get one txn buffer, create a header record for it.
				stldb::commit_buffer_t<void_alloc_t> *buff = _shm_info->waiting_txns[0];

				// add the header and the buffer to the io_vec structures.
				iov[2*txn_count].iov_base = &(buff->header);
				iov[2*txn_count].iov_len = sizeof(struct log_header);
				iov[2*txn_count+1].iov_base = const_cast<char*>(&*(buff->begin()));
				iov[2*txn_count+1].iov_len = buff->size()*sizeof(char);

				// stats keeping
				bytes += iov[2*txn_count].iov_len + iov[2*txn_count+1].iov_len;
				txn_written++;

				max_lsn = buff->header.lsn;
				new_file_len += iov[2*txn_count].iov_len + iov[2*txn_count+1].iov_len;
				txn_count++;

				_shm_info->waiting_txns.pop_front();
			}
			t4.end();

			// we can release the queue lock at this point...
			queue_lock_holder.unlock();

			// pad the writes, as necessary, in order to ensure that each
			// write can begin on sector/page boundaries, per optimum_write_alignment.
			int buffercount = txn_count*2;
			uint64_t padding = (new_file_len % optimum_write_alignment != 0) 
				? optimum_write_alignment - (new_file_len % optimum_write_alignment) : 0;
			if (padding != 0 && padding < sizeof(struct log_header))
				padding += optimum_write_alignment;
			if (padding != 0) {
				// we can add a padding transaction record in order to
				// promote disk writes of 'optimum_write_alignment' increments.
				padding_header.segment_size = padding - sizeof(struct log_header);
				padding_header.finalize();
				iov[2*txn_count].iov_base = &padding_header;
				iov[2*txn_count].iov_len = sizeof(struct log_header);
				buffercount++;
				if (padding - sizeof(struct log_header) > 0) {
					iov[2*txn_count+1].iov_base = padding_buffer;
					iov[2*txn_count+1].iov_len = padding - sizeof(struct log_header);
					buffercount++;
				}
				new_file_len += padding;
				bytes += padding;
			}

			// make sure our fd is pointing to the correct offset in the file.
			if (_my_fp_offset != _shm_info->log_len) {
				boost::interprocess::detail::set_file_pointer(_logfd, _shm_info->log_len,
					boost::interprocess::file_begin);
			}

			// Write the data we have gathered to the file
			stldb::timer t5("write()");
			bool written = stldb::io::gathered_write_file(_logfd, &iov[0], buffercount);
			if (!written) {
				std::ostringstream error;
				error << "stldb::io::gathered_write_file() to log file failed.  errno: " << errno;
				throw std::ios_base::failure( error.str() );
			}
			_my_fp_offset = new_file_len;
			writes++;
			t5.end();

			// Update the info about file len, and last written log_seq
			_shm_info->_last_write_txn_id = max_lsn;
			_shm_info->log_len = new_file_len;

		} // while max log_seq written is < my log_seq

		// Ok, once here, our commit buffer has been written, but we still might
		// need to wait for it to go to disk (via stldb::io::sync())
		if (_shm_info->_last_sync_txn_id < my_commit_seq && _shm_info->log_sync ) {
			this->sync_log(file_lock_holder);
			syncs++;
		}

		// record stats:
		_shm_info->stats.total_commits++;
		_shm_info->stats.total_write_commits += (writes>0) ? 1 : 0;
		_shm_info->stats.total_sync_only_commits += (writes==0 && syncs>0) ? 1 : 0;
		_shm_info->stats.total_free_commits += (writes==0 && syncs==0) ? 1 : 0;
		_shm_info->stats.total_writes += writes;
		if ( _shm_info->stats.max_writes_per_commit < writes )
			_shm_info->stats.max_writes_per_commit = writes;
		if ( _shm_info->stats.max_buffers_per_commit < txn_written )
			_shm_info->stats.max_buffers_per_commit = txn_written;
		_shm_info->stats.total_bytes_written += bytes;
		_shm_info->stats.total_syncs += syncs;

		// file_lock_holder releases as it goes out of scope.
	};

	// Sync the log to disk.   CALLER MUST HOLD FILE LOCK.
	void sync_log(boost::interprocess::scoped_lock<mutex_type> &held_file_lock)
	{

		transaction_id_t new_sync_txn_id = _shm_info->_last_write_txn_id;

		// TODO - it might be possible to allow a thread to do additional 
		// writes to this file while the current thread oes an fsync to 
		// sync the last write to disk.  This behavior is enabled by 
		// uncommenting the unlock() and lock() pair below.
		// held_file_lock.unlock();

		// may block for some time....
		stldb::timer t6("sync()");
		stldb::io::sync(_logfd);
		t6.end();

		// now re-acquire the file mutex..
		// held_file_lock.lock();

		if (_shm_info->_last_sync_txn_id < new_sync_txn_id) {
			_shm_info->_last_sync_txn_id = new_sync_txn_id;
		}
	}

	// Called by checkpoint logic to deliberately advance the current logfile so that the
	// the first transaction to commit after the start of a checkpoint causes all preceding
	// logs to become archivable.  It minimizes the log reads needed during recovery.
	// CALLER must hold file lock.
	void advance_logfile() {
		// In this case it is time to advance to a new log file.
		_shm_info->log_filename = stldb::detail::log_filename(_shm_info->log_dir.c_str(), _shm_info->_last_write_txn_id).c_str();
		_shm_info->log_len = 0;
		STLDB_TRACE(fine_e, "Logger: starting new log file: " << _shm_info->log_filename.c_str());
	}

private:

	/**
	 * ensure that _logfd is referring to the correct (current) logfile,
	 * and that the logfile in question has not exceeded its maximum size.
	 * Caller holds _shm_info->file_mutex.
	 */
	void ensure_open_logfile() {
		// write the buff to the log file.
		if ( _shm_info->log_filename.empty()
			 || (_shm_info->log_len >= _shm_info->log_max_len)
		     || (_shm_info->log_filename.compare(_my_log_filename.c_str()) != 0) )
		{
			stldb::timer t1("Logger::open_logfile");
			// need to reopen our _logfd at a minimum.

			if (_shm_info->log_filename.empty() || _shm_info->log_len >= _shm_info->log_max_len)
			{
				// In this case it is time to advance to a new log file.
				this->advance_logfile();
			}

			if ( _shm_info->log_filename.compare( _my_log_filename.c_str() ) != 0
				 && !_my_log_filename.empty() ) {
				// close whatever file we used to have open.
				stldb::timer t2("Logger::close_logfile");
				boost::interprocess::detail::close_file(_logfd);
			}

			// now open _shm_info->log_filename
			STLDB_TRACE(fine_e, "Logger: create_or_open log file: " << _shm_info->log_filename.c_str());
			boost::interprocess::permissions perm;
			_logfd = boost::interprocess::detail::create_or_open_file( _shm_info->log_filename.c_str(),
					boost::interprocess::read_write, perm );
			if (_logfd < 0) {  /* TODO - Too OS specific */
				std::ostringstream error;
				error << "STLdb Logger: create_or_open_file() of log file '" << _shm_info->log_filename.c_str() << "' failed.  errno=" << errno << ": " << strerror(errno);
				STLDB_TRACE(severe_e, error.str());
				throw std::ios_base::failure( error.str() );
			}
			// most filesystems will benefit from this:
			// we seek to maxsize and write a byte, so that during subsequent I/O
			// into the file, we aren't changing the inode's length constantly.
			boost::interprocess::detail::set_file_pointer(_logfd, _shm_info->log_max_len,
					boost::interprocess::file_begin);

			char byte = 0;
			bool written = stldb::io::write_file(_logfd, &byte, 1);
			if (!written) {
				std::ostringstream error;
				error << "stldb::io::_write_file(1) at offset log_max_len failed.  errno: " << errno;
				throw std::ios_base::failure( error.str() );
			}
			_my_fp_offset = _shm_info->log_max_len +1;

			// Note that we now have this file open.
			_my_log_filename = _shm_info->log_filename.c_str();
		}
	}

	// hard-coded approach needed due to gcc-3.4.3 bug.  Corresponds
	// to IOV_MAX=16 (Solaris)
	//static const size_t max_txn_per_write = (io::max_write_region_per_call-1)/2;
	static const size_t max_txn_per_write = 7;
	static const size_t optimum_write_alignment = 512;

	// Used to hold structures used with writev()
	io::write_region_t iov[2*(max_txn_per_write+1)];

	// used to provide padding of writes.
	log_header padding_header;
	char padding_buffer[optimum_write_alignment + sizeof(struct log_header)];

	// details about logging kept in shared memory.
	SharedLogInfo<void_alloc_t,mutex_type> *_shm_info;

	// The details of the open file as this process currently knows it.
	boost::interprocess::file_handle_t  _logfd;
	std::string   _my_log_filename; // what _logfd refers to
	boost::interprocess::offset_t  _my_fp_offset;  // offset into _logfd which a write() would currently go to.
};

} // stldb namespace

#endif

