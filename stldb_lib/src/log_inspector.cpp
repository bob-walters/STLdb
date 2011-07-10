/*
 * log_inspector.cpp
 *
 *  Created on: Aug 28, 2009
 *      Author: rwalter3
 */
#define BOOST_STLDB_SOURCE

#include <boost/archive/archive_exception.hpp>
#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

#ifdef __GNUC__
#	include <stldb/containers/gnu/basic_string.h>
#	include <stldb/allocators/allocator_gnu.h>
#else
#	include <boost/interprocess/containers/string.hpp>
#endif

// For the serialization of std::pair
#include <boost/serialization/utility.hpp>

#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/streams/vectorstream.hpp>

#include <stldb/allocators/region_or_heap_allocator.h>
#include <stldb/logging.h>
#include <stldb/log_reader.h>
#include <stldb/containers/trans_map_entry.h>
#include <stldb/containers/string_serialize.h>
#include <stldb/exceptions.h>

using boost::interprocess::managed_mapped_file;

#ifdef __GNUC__
	// On Unix/Linux/Solaris:
	// Using stldb::basic_string for the string data type.  (shm safe, perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		stldb::gnu_adapter<
			boost::interprocess::allocator<
				char, managed_mapped_file::segment_manager> > > shm_char_allocator_t;

	typedef stldb::basic_string<char, std::char_traits<char>, shm_char_allocator_t>  std_shm_string;
#else
	// On Windows:
	// Using boost::interprocess::basic_string for the string data type.  (shm safe, not perf optimized)

	// Allocator of char in shared memory, with support for default constructor
	typedef stldb::region_or_heap_allocator<
		boost::interprocess::allocator<
			char, managed_mapped_file::segment_manager> > shm_char_allocator_t;

	typedef boost::interprocess::basic_string<char, std::char_traits<char>, shm_char_allocator_t> std_shm_string;
#endif


using namespace std;
using stldb::transaction_id_t;
using stldb::log_header;
using stldb::log_reader;
using stldb::TransactionalOperation;
using stldb::no_transaction;

template <typename _CharT, class _Traits>
std::basic_ostream<_CharT,_Traits>& operator<<(std::basic_ostream<_CharT,_Traits> &s, log_header& header) {
	s << "Transaction Header:" << endl
	  << "\t lsn:   " << header.lsn << endl
	  << "\t op_count: " << header.op_count << endl
	  << "\t segment_size: " << header.segment_size << endl
	  << "\t segment_checksum: " << header.segment_checksum << endl
	  << "\t header_checksum: " << header.header_checksum << endl;
	return s;
}

template <typename _CharT, class _Traits>
std::basic_ostream<_CharT,_Traits>& operator<<(std::basic_ostream<_CharT,_Traits> &s, std_shm_string &str) {
	unsigned int iters = str.length()/12 + (str.length() % 12 ? 1 : 0 );
	for (unsigned int i=0; i<iters; i++) {
		s.fill('0');
		s << "\t\t ";
		for (unsigned int j=i*12; j<str.length() && j<12+i*12; j++) {
			unsigned short val = (unsigned char&)str[j];
			s << " " << std::hex << std::setw(2) << val;
		}
		s << "  |";
		for (unsigned int j=i*12; j<str.length() && j<12+i*12; j++) {
			if (str[j] >= 32 && str[j] <= 126) 
				s << str[j];
			else
				s << ".";
		}
		s << "|" << endl;
	}
	return s;
}

template <typename _CharT, class _Traits>
std::basic_ostream<_CharT,_Traits>& operator<<(std::basic_ostream<_CharT,_Traits> &s, const std::pair<log_header*, std::vector<char>*> &txn) {
	log_header &header = *(txn.first);
	std::vector<char> &buffer = *(txn.second);

	typedef std::pair<std_shm_string, stldb::TransEntry<std_shm_string> > value_type;
	value_type    temp;
	std_shm_string key;

	s << header;
	unsigned i=0;
	try {
		// Wrap the vector of bytes directly in streams.
		boost::interprocess::basic_ivectorstream< std::vector<char> > vbuffer(buffer.get_allocator());
		vbuffer.swap_vector(buffer);
		boost_iarchive_t bstream((std::istream&)vbuffer);

		for (; i<header.op_count; i++)
		{
			s << "\t Operation " << std::dec << i << " of " << header.op_count << endl;
			// stream in the 'header' of each transactional operation.
			std::pair<std::string,int> op_header = TransactionalOperation::deserialize_header(bstream);
			s << "\t\t Container:" << op_header.first << endl;
			s << "\t\t opcode:";

			switch (op_header.second) {
			case stldb::No_op:
				s << "No_op (that's not supposed to happen)" << endl;
				break;
			case stldb::Lock_op:
				s << "Lock_op (that's not supposed to happen)" << endl;
				break;
			case stldb::Insert_op:
				s << "Insert_op" << endl;
				bstream & temp;
				s << "\t\t key:" << endl;
				s << temp.first;
				s << "\t\t value:" << endl;
				s << temp.second;
				break;
			case stldb::Update_op:
				s << "Update_op" << endl;
				bstream & temp;
				s << "\t\t key:" << endl;
				s << temp.first;
				s << "\t\t value:" << endl;
				s << temp.second;
				break;
			case stldb::Delete_op:
				s << "Delete_op" << endl;
				bstream & key;
				s << "\t\t key:" << endl;
				s << key;
				break;
			case stldb::Clear_op:
				s << "Clear_op" << endl;
				break;
			case stldb::Swap_op:
				s << "Swap_op" << endl;
				bstream & key;
				s << "\t\t swapped container name: " << endl;
				s << key;
				break;
			default:
				s << "INVALID (that's not supposed to happen)" << endl;
				break;
			}
		}

		// At the end of this, let _buffer once again own it's prior contents.
		vbuffer.swap_vector(buffer);
	}
	catch (boost::archive::archive_exception &ex) {
		s << "boost::archive::archive_exception: " << ex.what();
		s << ".  Processing lsn: " << header.lsn << ", operation " << i << " of " << header.op_count;
	}
	return s;
}



int
main(int argc, const char *argv[])
{
	std::vector<string> logfiles;
	transaction_id_t txn_id = 0;

	cout << argc << endl;
	for (int i=1; i<argc; i++) {
		string arg(argv[i]);
		if (arg.find("--txn") != string::npos) {
			txn_id = atol(argv[++i]);
		}
		else {
			logfiles.push_back(arg);
			cout << arg << endl;
		}
	}

	for (std::vector<string>::iterator fileiter = logfiles.begin();
			fileiter != logfiles.end(); fileiter++)
	{
		transaction_id_t txn_found;
		log_reader reader(fileiter->c_str());
		try {
			if (txn_id) {
				txn_found = reader.seek_transaction(txn_id);
				if (txn_found != stldb::no_transaction) {
					cout << txn_found << endl << reader.get_transaction();
				}
			}
			else {
				while ((txn_found = reader.next_transaction()) != no_transaction) {
					cout << txn_found << endl << reader.get_transaction();
				}
			}
		}
		catch( stldb::recover_from_log_failed &ex ) {
			cout << "Recover_from_log_failed exception received: " << ex.what() << endl;
			cout << "Occurred at byte " << ex.offset_in_file() << " of file: " << ex.filename() << endl;
			cout << (*reader.get_transaction().first);
			return 1;
		}
	}
	return 0;
}

