
#ifndef STLDB_LOGGING_H_
#define STLDB_LOGGING_H_

#include <sstream>

#include <stldb/stldb.hpp>
#include <stldb/cachetypes.h>

// Contains common structures used by both the log_writer and the log_reader classes
namespace stldb
{

// A routine used for 32-bit checksums on log buffer and checkpoint data regions.
BOOST_STLDB_DECL
uint32_t adler(const uint8_t *data, std::size_t len);

/**
 * Each transaction log record is proceeded by a fixed size header which indicates the
 * length of the ensuing log records, and provides a checksum value.
 */
struct log_header {
	uint32_t op_count;
	uint32_t segment_size;
	transaction_id_t lsn;
	uint32_t segment_checksum;
	uint32_t header_checksum;

	inline log_header()
		: op_count(0), segment_size(0), lsn(0), segment_checksum(0), header_checksum(0)
		{ }

	inline bool padding() const {
		return (segment_checksum == 0 && op_count == 0
				&& lsn == 0);
	}
	inline bool empty() const {
		return (header_checksum == 0 && op_count == 0 && segment_checksum == 0
				&& segment_size == 0 && lsn == 0 );
	}
	inline void finalize() {
		header_checksum = 0;
		header_checksum = adler( reinterpret_cast<const uint8_t*>(this),sizeof(struct log_header));
	}

};



} //namespace

#endif
