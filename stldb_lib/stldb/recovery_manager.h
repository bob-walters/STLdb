/*
 * recovery_manager.h
 *
 *  Created on: Jun 18, 2009
 *      Author: bobw
 */

#ifndef RECOVERY_MANAGER_H_
#define RECOVERY_MANAGER_H_

#include <map>

#include <boost/filesystem.hpp>
using namespace boost::filesystem;

#include <boost/archive/archive_exception.hpp>
#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

#include <stldb/logging.h>
#include <stldb/log_reader.h>
#include <stldb/allocators/scoped_allocation.h>
#include <stldb/container_proxy.h>
#include <stldb/containers/trans_map_entry.h>


namespace stldb
{

template <class ManagedRegionType>
class recovery_manager
{
public:
	typedef Database<ManagedRegionType> db_type;
	typedef container_proxy_base<ManagedRegionType>  container_proxy_type;
	typedef std::map<container_proxy_type*,transaction_id_t>  container_lsn_map_t;

	// Construct a recovery manager which can perform the reading in of previously logged
	// transactions, and facilitate the re-application of logged changes against the
	// containers in the database passed.
	recovery_manager(db_type &database, container_lsn_map_t &container_lsn, transaction_id_t starting_lsn );

	// perform recovery, using the log files in the logging directory passed.
	transaction_id_t recover();

private:
	transaction_id_t recover_txn( std::pair<log_header*,std::vector<char>*> header_and_data);

	container_lsn_map_t  container_lsn;
	transaction_id_t starting_lsn;
	db_type &db;

	// Stats about how much we've recovered from log files.
	std::size_t _txn_count;
	std::size_t _op_count;
};

template <class ManagedRegionType>
recovery_manager<ManagedRegionType>::recovery_manager(
		db_type &database, container_lsn_map_t &container_lsn, transaction_id_t starting_lsn )
	: container_lsn(container_lsn)
	, starting_lsn(starting_lsn)
	, db(database)
{
}


// perform recovery, using the log files in the logging directory passed.
template <class ManagedRegionType>
transaction_id_t recovery_manager<ManagedRegionType>::recover()
{
	std::map<transaction_id_t, boost::filesystem::path> log_files =
			detail::get_log_files(db.get_logging_directory());
	if (log_files.size()==0)
		return 0;

	// determine the first log file to start with:
	std::map<transaction_id_t, boost::filesystem::path>::iterator i = log_files.lower_bound(starting_lsn);
	if ( i != log_files.begin() && (i == log_files.end() || i->first > starting_lsn ))
		i --; // we will need the log file previous as well.
	STLDB_TRACE(info_e, "determined first log file needed for recovery: " << (i==log_files.end() ? "None" : i->second.string()));

	if (i->first > starting_lsn) {
		// hmmm, the operator deleted some log files which are needed to perform
		// recovery.  That's not good.  The database is corrupt, and will have to
		// be recovered from a backup copy.
		throw stldb_exception("Can't recover because needed log files have been prematurely removed");
	}

	transaction_id_t max_lsn = no_transaction;
	transaction_id_t lsn = no_transaction;

	// It is reasonable to assume that when operations are being recovered, objects
	// might get constructed initially using a default constructor, and then deserialized
	// from bstream.  Etablish that for the remainder of this function, this thread
	// should default all allocator's to using db's region.
	stldb::scoped_allocation<typename ManagedRegionType::segment_manager> a(db.getRegion().get_segment_manager());

	//now proceed with actually redoing operations:
	try
	{
		// move forward to the starting point (starting_lsn) in this first file.
		log_reader reader(i->second.string().c_str());
		lsn = reader.seek_transaction(starting_lsn);

		// and then recover everything through EOF for that file.
		while (lsn != no_transaction) {
			this->recover_txn( reader.get_transaction() );
			max_lsn = lsn;
			lsn = reader.next_transaction();
		}
		STLDB_TRACE(info_e, "Completed recovery of logfile, " << _txn_count << " transactions, " << _op_count << " operations recovered thus far");
		reader.close();

		// and now recover the transactions from every additional log file after the first.
		for ( i++; i != log_files.end(); i++ ) {
			log_reader nextLog(i->second.string().c_str());
			while ((lsn = nextLog.next_transaction()) != no_transaction) {
				this->recover_txn( nextLog.get_transaction() );
				max_lsn = lsn;
			}
		}

		// At the conclusion of this process, we know the maximum LSN recovered
		STLDB_TRACE(fine_e, "recovery complete, recovered transactions through LSN: " << max_lsn);
	}
	catch(recover_from_log_failed &ex) {
		// Note how far we got before the error occurred.  It may still be
		// possible to complete recovery with lost transactions.
		STLDB_TRACE(severe_e, ex.what());
	}
	catch (std::ios_base::failure &ex) {
		STLDB_TRACE(severe_e, ex.what());
	}
	return max_lsn;
}


// Recover the transaction currently in _buffer
template <class ManagedRegionType>
transaction_id_t recovery_manager<ManagedRegionType>::recover_txn( std::pair<log_header*,std::vector<char>*> header_and_data)
{
	uint32_t i=0;

	log_header &header = *header_and_data.first;
	std::vector<char> &buffer = *header_and_data.second;

	try {
		// Wrap the vector of bytes directly in streams.
		boost::interprocess::basic_ivectorstream< std::vector<char> > vbuffer(buffer.get_allocator());
		vbuffer.swap_vector(buffer);
		boost_iarchive_t bstream(vbuffer);

		for (; i<header.op_count; i++)
		{
			// stream in the 'header' of each transactional operation.
			std::pair<std::string,int> op_header = TransactionalOperation::deserialize_header(bstream);

			// Get the container proxy for the container that this record refers to
			container_proxy_base<ManagedRegionType>* proxy = db.getContainerProxy(op_header.first.c_str());

			// Conceivably proxy could be null if a table was removed, and then the database crashed
			// before a subsequent checkpoint could be completed.  In that case the log could still
			// contain records pertaining to the deleted table.
			if (proxy != NULL) {
				proxy->recoverOp(op_header.second, bstream);
				_op_count++;
			}
		}
		_txn_count++;

		// At the end of this, let _buffer once again own it's prior contents.
		vbuffer.swap_vector(buffer);
	}
	catch (boost::archive::archive_exception &ex) {
		std::ostringstream msg;
		msg << "Log Recovery: boost::archive::archive_exception: " << ex.what();
		msg << ".  Processing txn_id: " << header.txn_id << ", operation " << i << " of " << header.op_count;
		STLDB_TRACE(severe_e, msg.str());
		throw recover_from_log_failed(msg.str().c_str(), header.txn_id, -1, "");
	}
	return header.txn_id;
}

} // namespace

#endif /* RECOVERY_MANAGER_H_ */
