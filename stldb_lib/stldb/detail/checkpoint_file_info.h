/*
 * checkpoint_file_info.h
 *
 *  Created on: Jan 17, 2010
 *      Author: bobw
 */

#ifndef CHECKPOINT_FILE_INFO_H_
#define CHECKPOINT_FILE_INFO_H_

#include <map>

#include <stldb/cachetypes.h>
#include <stldb/detail/os_file_functions_ext.h>

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

namespace stldb {

/**
 * checkpoint_file_info corresponds to the metadata which is stored
 * within a
 */
struct checkpoint_file_info {
	std::string ckpt_filename;  // filename only, no path
	std::string meta_filename;  // filename only, no path
	std::string container_name;
	std::size_t file_length;
	transaction_id_t lsn_at_start;
	transaction_id_t lsn_at_end;
	std::multimap<std::size_t,boost::interprocess::offset_t> free_space;

	checkpoint_file_info()
		: ckpt_filename(), meta_filename(), container_name(), file_length(0)
		, lsn_at_start(0), lsn_at_end(0), free_space()
		{ }

	// serializable
	template<class archive_t>
	void serialize(archive_t &ar, const unsigned int version)
	{
	    ar & ckpt_filename
		   & meta_filename
	       & container_name
	       & file_length
	       & lsn_at_start
	       & lsn_at_end
	       & free_space;
	}
};

inline std::ostream& operator<<(std::ostream &s, struct checkpoint_file_info &meta)
{
	s << "checkpoint filename: " << meta.ckpt_filename << std::endl
	  << "metadata filename: " << meta.meta_filename << std::endl
	  << "container name: " << meta.container_name << std::endl
	  << "file length: " << meta.file_length << std::endl
	  << "LSN at start of checkpoint: " << meta.lsn_at_start << std::endl
	  << "LSN at end of checkpoint: " << meta.lsn_at_end << std::endl
	  << "free space:";

	std::multimap<std::size_t,boost::interprocess::offset_t>::iterator i = meta.free_space.begin();
	while (i != meta.free_space.end()) {
		s << "[" << i->second << "," << i->first << "]";
		i++;
	}
	s << std::endl;
	return s;
}

}  // namespace

#endif /* CHECKPOINT_FILE_INFO_H_ */
