/*
 * commit_buffer.h
 *
 *  Created on: Apr 18, 2009
 *      Author: bobw
 */

#ifndef COMMIT_BUFFER_H_
#define COMMIT_BUFFER_H_

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/streams/vectorstream.hpp>
#include <boost/intrusive/slist.hpp>

using boost::intrusive::optimize_size;  // tag
using boost::intrusive::void_pointer;  // tag
using boost::intrusive::cache_last;
using boost::intrusive::slist_base_hook;
using boost::intrusive::slist;

using boost::interprocess::vector;
using boost::interprocess::allocator;
using boost::interprocess::basic_ovectorstream;

namespace stldb {

/**
 * Commit buffers are vector<chars> wrapped with basic_vectorstreams that are used to
 * prepare and hold the serialized representation of transactions.  Once serialization
 * is complete, the log writing can be done using low-level I/O operations directly on
 * the accumulated vector contents.
 *
 * Commit buffers are allocated within the shared regions of databases, and  when a
 * process is done with them, they are recycled by putting them back into an intrusive list
 * in the dbinfo of the database.  This permits buffer reuse across processes, and also
 * supports the aggregate transaction writing strategy of the Logger.
 */
template <class void_alloc_type>
class commit_buffer_t
	: public boost::interprocess::vector<char,typename void_alloc_type::template rebind<char>::other>
	, public slist_base_hook<optimize_size<false>,
	                         void_pointer<typename void_alloc_type::pointer> >
	{
	public:
		int op_count;

		// Construct
		inline commit_buffer_t(const void_alloc_type &alloc)
			: boost::interprocess::vector<char, typename void_alloc_type::template rebind<char>::other>( alloc )
			, op_count(0)
			{ }
	};

}  // namespace

#endif /* COMMIT_BUFFER_H_ */
