/*
 * statistics.h
 *
 *  Created on: Jul 20, 2009
 *      Author: RWalter3
 */

#ifndef STATISTICS_H_
#define STATISTICS_H_

#include <iostream>
#include <boost/serialization/nvp.hpp>

namespace stldb {

// Structure for holding some accumuldated logging stats
struct log_stats {
	uint64_t    total_commits;
	uint64_t    total_write_commits;
	uint64_t    total_sync_only_commits;
	uint64_t    total_free_commits;
	uint64_t    total_writes;
	uint64_t    max_writes_per_commit;
	uint64_t    max_buffers_per_commit;
	uint64_t    total_bytes_written;
	uint64_t    total_syncs;

	log_stats()
		: total_commits(0)
		, total_write_commits(0)
		, total_sync_only_commits(0)
		, total_free_commits(0)
		, total_writes(0)
		, max_writes_per_commit(0)
		, max_buffers_per_commit(0)
		, total_bytes_written(0)
		, total_syncs(0)
	{ }

	template<class stream>
	void print(stream &s) {
		s << "Total Commits:         " << total_commits << std::endl
		  << " Write & Sync:         " << total_write_commits << std::endl
		  << "    Sync Only:         " << total_sync_only_commits << std::endl
		  << "         Free:         " << total_free_commits << std::endl
		  << "Total write():         " << total_writes << std::endl
		  << "Total fsync()s:        " << total_syncs << std::endl
		  << "Total Bytes Written:   " << total_bytes_written << std::endl
		  << "Max Writes per Commit: " << max_writes_per_commit << std::endl
		  << "Max Txns per Commit:   " << max_buffers_per_commit << std::endl
		  << std::endl;
	}

	// provide for serialization of an XML-based form of DatabaseInfo contents
	template <class Archive>
	void serialize(Archive &ar, const unsigned int /* version */ )
	{
		ar & BOOST_SERIALIZATION_NVP(total_commits)
		   & BOOST_SERIALIZATION_NVP(total_write_commits)
		   & BOOST_SERIALIZATION_NVP(total_sync_only_commits)
		   & BOOST_SERIALIZATION_NVP(total_free_commits)
		   & BOOST_SERIALIZATION_NVP(total_writes)
		   & BOOST_SERIALIZATION_NVP(total_syncs)
		   & BOOST_SERIALIZATION_NVP(total_bytes_written)
		   & BOOST_SERIALIZATION_NVP(max_writes_per_commit)
		   & BOOST_SERIALIZATION_NVP(max_buffers_per_commit);
	}
};


struct database_stats {
	uint64_t region_size;
	uint64_t region_free;
	uint64_t num_named_objects;
	uint64_t num_unique_objects;
	struct log_stats logging_stats;

	template<class stream>
	void print(stream &s) {
		s << "region_size: " << region_size << std::endl
		  << "region_free: " << region_free << std::endl
		  << "num_named_objects: " << num_named_objects << std::endl
		  << "num_unique_objects: " << num_unique_objects << std::endl;
		logging_stats.print(s);
	}
};

} // namespace stldb {

#endif /* STATISTICS_H_ */
