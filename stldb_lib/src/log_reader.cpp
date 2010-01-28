/*
 * log_reader.cpp
 *
 *  Created on: Aug 28, 2009
 *      Author: rwalter3
 */

#define BOOST_STLDB_SOURCE

#include <stldb/log_reader.h>
#include <stldb/trace.h>
#include <stldb/exceptions.h>

namespace stldb {

log_reader::log_reader( const char* filename)
	: _filename( filename )
	, _logfd( 0 )
	, _filesize(0)
	, _offset(0)
	, _txn_count(0)
	, _header()
	, _buffer()
	, _last_txn(no_transaction)
{
	// now open _shm_info->log_filename
	path fullname( filename );
	if ( !boost::filesystem::exists( fullname ) ) {
		std::ostringstream error;
		error << "Log filename passed to log_reader does not exist: " << fullname.string();
		STLDB_TRACE(error_e, error.str());
		throw std::ios_base::failure( error.str() );
	}

	_filesize = boost::filesystem::file_size( fullname );
	STLDB_TRACE(info_e, "Recovering from logfile " << fullname.string() << " of size: " << _filesize);

	_logfd = boost::interprocess::detail::open_existing_file( fullname.file_string().c_str(),
						boost::interprocess::read_only );
	if (_logfd < 0) {
		/* TODO - Too OS specific? */
		std::ostringstream error;
		error << "stldb::open() of log file '" << filename << "' failed.  errno=" << errno << ": " << strerror(errno);
		STLDB_TRACE(error_e, error.str());
		throw std::ios_base::failure( error.str() );
	}
}


log_reader::~log_reader()
{
	if (_logfd != 0)
		this->close();
}


void log_reader::close()
{
	// close whatever file we used to have open.
	boost::interprocess::detail::close_file(_logfd);
	_logfd = 0;
}


transaction_id_t log_reader::seek_transaction(transaction_id_t starting_lsn)
{
	transaction_id_t lsn = read_next_txn();
	while ( lsn < starting_lsn && lsn != no_transaction ) {
		_last_txn = lsn;
		lsn = read_next_txn();
	}
	if (lsn == no_transaction) {
		STLDB_TRACE(fine_e, "Located last transaction in file, LSN: " << _last_txn << " at offset: " << (_offset-sizeof(_header)-_header.segment_size));
		return _last_txn;
	}

	STLDB_TRACE(fine_e, "Located first non-checkpointed transaction, LSN: " << lsn << " at offset: " << (_offset-sizeof(_header)-_header.segment_size));

	_last_txn = lsn;
	_txn_count++;
	return lsn;
}


transaction_id_t log_reader::next_transaction()
{
	transaction_id_t lsn = read_next_txn();
	if (lsn != no_transaction) {
		_last_txn = lsn;
		_txn_count++;
	}
	return lsn;
}


transaction_id_t log_reader::read_next_txn()
{
	// Start by reading in the next header.
	memset( &_header, 0, sizeof(_header) );
	std::size_t bytes_read = stldb::io::read_file(_logfd, &_header, sizeof(_header));
	if (bytes_read < sizeof(_header)) {
		if (!_header.empty())
		throw recover_from_log_failed("Log Recovery: Log truncation detected while attempting to read transaction",
				_header.lsn, _offset, _filename.c_str());
	}

	// log files are sparse files, so typically, this implies the end of the file
	if (_header.empty() )
		return no_transaction;

	// make sure the header made it to disk ok.  validate the checksum.
	uint32_t chk = _header.header_checksum;
	_header.header_checksum = 0;
	_header.header_checksum = adler(reinterpret_cast<const uint8_t*>(&_header),sizeof(_header));
	if ( _header.header_checksum != chk) {
		throw recover_from_log_failed("Log Recovery: Header checksum failure",
				_last_txn, _offset, _filename.c_str());
	}

	// padding headers can have a segment_size == 0 if exactly sizeof(header) bytes were needed as padding.
	if (_header.segment_size >0) {
		// Resize _buffer as needed and read the txn data directly into it.
		_buffer.resize( _header.segment_size );
		bytes_read = stldb::io::read_file(_logfd, &_buffer[0], _header.segment_size );
		if (bytes_read != _header.segment_size ) {
			throw recover_from_log_failed("Log Recovery: Log truncation detected while attempting to read transaction",
					_header.lsn, _offset, _filename.c_str());
		}
		if (!_header.padding()) {
			chk = adler(reinterpret_cast<const uint8_t*>(&_buffer[0]), _header.segment_size);
			if ( _header.segment_checksum != chk ) {
				throw recover_from_log_failed("Log Recovery: Buffer checksum failure",
						_header.lsn, _offset, _filename.c_str());
			}
		}
	}

	_offset += (sizeof(_header) + _header.segment_size);

	if (_header.padding())
		return this->read_next_txn();

	return _header.lsn;
}

} // namespace

