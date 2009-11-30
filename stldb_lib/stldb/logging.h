
#ifndef STLDB_LOGGING_H_
#define STLDB_LOGGING_H_

#include <sstream>

#include <stldb/stldb.hpp>
#include <stldb/cachetypes.h>

// Contains common structures used by both the log_writer and the log_reader classes
namespace stldb
{

/**
 * Each transaction log record is proceeded by a fixed size header which indicates the
 * length of the ensuing log records, and provides a checksum value.
 */
struct log_header {
	uint32_t op_count;
	uint32_t segment_size;
	transaction_id_t txn_id;
	uint32_t segment_checksum;
	uint32_t header_checksum;
};

// A routine used for 32-bit checksums on log buffer and checkpoint data regions.
BOOST_STLDB_DECL
uint32_t adler(const uint8_t *data, std::size_t len);


} //namespace

#endif
