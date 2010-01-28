/*
 * LogReader.h
 *
 *  Created on: May 4, 2009
 *      Author: bobw
 */

#ifndef STLDB_LOGREADER_H_
#define STLDB_LOGREADER_H_

#include <stldb/stldb.hpp>

#include <boost/filesystem.hpp>
using boost::filesystem::path;

#include <string>

#include <stldb/cachetypes.h>
#include <stldb/detail/os_file_functions_ext.h>
#include <stldb/logging.h>

namespace stldb
{


class BOOST_STLDB_DECL log_reader
{
public:
	//! Constructs a log_reader to read the contents of the indicated log file.
	log_reader(const char* filename);

	//! Closes any open log file.
	~log_reader();

	//! Closes any open log file.
	void close();

	/**
	 * Reads the log file until finding the first LSN which is equal to or greater
	 * than the LSN passed.  Returns the transaction_id which is found.  If the
	 * EOF is reached prior to finding an eligible transaction, the last transaction
	 * read in is returned (i.e. a value which could be less than lsn)
	 */
	transaction_id_t seek_transaction(transaction_id_t lsn);

	/**
	 * Reads the next transaction from the log file.
	 * Returns the transaction_id which is found.  Returns
	 * no_transaction if the EOF is reached.
	 * throws if an error is encountered with the content of the log file.
	 */
	transaction_id_t next_transaction();

	/**
	 * Returns the header and data buffer of the last transacton read from disk via a
	 * call to seekTransacton() or nextTransaction().
	 * The returned pointers are only valid until the next call to either of those methods.
	 */
	std::pair<log_header*,std::vector<char>*> get_transaction() {
		return make_pair(&_header, &_buffer);
	}

private:
	/**
	 * Read the next transaction from the file into _buffer.
	 * Confirm its checkpoints, and return the header of that transaction
	 */
	transaction_id_t read_next_txn();

	// The details of the open file as this process currently knows it.
	std::string _filename;
	boost::interprocess::file_handle_t  _logfd;

	// Size of the logfile as seen on the filesystem.
	boost::uintmax_t _filesize;

	// The # of bytes into the file that we have read thus far.
	boost::uintmax_t _offset;

	// Stats about how much we've read.
	std::size_t _txn_count;

	// The last read header and  buffer.
	log_header         _header;
	std::vector<char>  _buffer;

	// The last transaction read.
	transaction_id_t _last_txn;
};


} //namespace

#endif /* LOGREADER_H_ */
