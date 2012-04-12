/*
 * checkpoint_metafile.cpp
 *
 *  Created on: Jan 12, 2010
 *      Author: bobw
 */
#define BOOST_STLDB_SOURCE

#include <iomanip>
#include <boost/assert.hpp>
#include <boost/filesystem.hpp>

#include <stldb/stldb.hpp>
#include <stldb/logging.h>
#include <stldb/checkpoint.h>
#include <stldb/detail/db_file_util.h>
#include <boost/assert.hpp>

using boost::filesystem::path;
using boost::filesystem::directory_iterator;

namespace stldb {

// construct from the contents of the specific metafile passed.
checkpoint_fstream_base::checkpoint_fstream_base(const boost::filesystem::path &metafile)
	: checkpoint_dir(), meta()
{
	detail::get_checkpoint_file_info(metafile, meta);
	checkpoint_dir = metafile.parent_path();
	prepare_free_by_offset();
}

// construct by searching the checkpoint directory for a current
// metafile for the container with the indicated name.
checkpoint_fstream_base::checkpoint_fstream_base(const boost::filesystem::path &checkpoint_path,
		const char * container_name)
	: checkpoint_dir(checkpoint_path), meta()
{
	boost::filesystem::path metafile( detail::get_current_metafile(checkpoint_path, container_name) );
	STLDB_TRACE(info_e, "Current checkpoint metafile: " << metafile.filename());

	if (metafile.empty()) {
		// no existing checkpoint for this container.
		meta.container_name = container_name;
		meta.ckpt_filename = container_name;
		meta.ckpt_filename.append(".ckpt");
	}
	else {
		detail::get_checkpoint_file_info(metafile, meta);
	}
	prepare_free_by_offset();
}

checkpoint_fstream_base::checkpoint_fstream_base(const boost::filesystem::path &checkpoint_path,
		const checkpoint_file_info &checkpoint_info)
	: checkpoint_dir(checkpoint_path), meta(checkpoint_info)
{
	prepare_free_by_offset();
}

void checkpoint_fstream_base::prepare_free_by_offset() {
	STLDB_TRACE(finer_e, "prepare_free_by_offset:");
	std::multimap<std::size_t,boost::interprocess::offset_t>::iterator i (meta.free_space.begin() );
	while (i != meta.free_space.end()) {
		BOOST_VERIFY( free_by_offset.insert( std::make_pair( i->second, i->first )).second ); 
		STLDB_TRACE_DEBUG(finer_e, "[" << i->second << "," << i->first << "]");
		i++;
	}
	BOOST_ASSERT( free_by_offset.size() == meta.free_space.size() );
}

checkpoint_fstream_base::~checkpoint_fstream_base() {
}

checkpoint_ofstream::~checkpoint_ofstream() {
	filestream.close();
}

// present in order to be invokable from debugger.
void print_map( const std::map<boost::interprocess::offset_t,std::size_t> &m )
{
	std::cerr << "DEBUG map:" << std::endl;
	std::map<boost::interprocess::offset_t,std::size_t>::const_iterator j = m.begin();
	while ( j != m.end()) {
		std::cerr << "[" << j->first << "," << j->second << "]" << std::endl;
		j++;
	}
}

// present in order to be invokable from debugger.
void print_mmap( const std::multimap<std::size_t,boost::interprocess::offset_t> &mmap )
{
	std::cerr << "DEBUG multimap:" << std::endl;
	std::multimap<std::size_t,boost::interprocess::offset_t>::const_iterator l = mmap.begin();
	while (l != mmap.end()) {
		std::cerr << "[" << l->second << "," << l->first << "]" << std::endl;
		l++;
	}
}

checkpoint_loc_t checkpoint_ofstream::allocate(std::size_t size)
{
    // round size needed to alignment, and account for size_t header and uint32_t
    // checksum
	std::size_t needed = size + sizeof(std::size_t) + sizeof(uint32_t);
    needed = (needed / entry_alignment + ( needed % entry_alignment ? 1 : 0)) * entry_alignment;

    // find unit of allocation
    std::multimap<std::size_t, boost::interprocess::offset_t>::iterator space = meta.free_space.lower_bound( needed );
    if (space == meta.free_space.end()) {
    	// extend the file's free space with a segment to accomodate entry.
        std::size_t ext_size = extension_size;
        if (ext_size < needed )
            ext_size = needed;
        meta.free_space.insert( std::make_pair(ext_size, meta.file_length));
        free_by_offset.insert( std::make_pair(meta.file_length, ext_size));
        meta.file_length += ext_size;
        space = meta.free_space.lower_bound( needed );
    }
	BOOST_ASSERT(space != meta.free_space.end());
    boost::interprocess::offset_t off = space->second;

    // adjust freelist to reflect allocation.
    std::size_t leftover = space->first - needed;
	std::map<boost::interprocess::offset_t,std::size_t>::iterator i = free_by_offset.find(off);
	// DEBUG:
	if ( i == free_by_offset.end() ) {
		print_mmap(meta.free_space);
		print_map(free_by_offset);
		abort();
	}
	// END DEBUG
    meta.free_space.erase(space);
	free_by_offset.erase( i );
	BOOST_ASSERT( meta.free_space.size() == free_by_offset.size() );
    if (leftover > 0) {
        meta.free_space.insert( std::make_pair(leftover,off+needed) );
        BOOST_VERIFY( free_by_offset.insert( std::make_pair(off+needed,leftover) ).second );
    }
	BOOST_ASSERT( meta.free_space.size() == free_by_offset.size() );

	STLDB_TRACE(finer_e, "Allocated: [" << off << "," << needed << "]");
    return std::make_pair(off,needed);
}


void checkpoint_ofstream::open_checkpoint()
{
	boost::filesystem::path fullname( checkpoint_dir );
	fullname /= meta.ckpt_filename;
	filestream.open( fullname.string().c_str(), std::ios::in | std::ios::out | std::ios::ate);
	if (!filestream) {
		// file doesn't exist, try creating it
		filestream.clear();
		filestream.open( fullname.string().c_str(), std::ios::out );
	}
	if (!filestream) {
		std::ostringstream error;
		error << "fstream.open() failed in checkpoint_ofstream::open_checkpoint for "
			  << fullname.string();
		throw std::ios_base::failure( error.str() );
	}
}

// write a serialized image to the checkpoint file at the offset indicated.
// throws ios_base::failure upon I/O error.

void checkpoint_ofstream::write( boost::interprocess::offset_t offset, std::size_t length, std::string image)
{
	// seek to off, write image, for image.size() bytes
	filestream.seekp(offset, std::ios_base::beg);
	// TODO - should this be in network byte order for portability.?
	// are binary streams from Boost.Serialization portable to some extent?
	filestream.write(reinterpret_cast<char*>(&length), sizeof(length));
    // compute the checksum for image, and write that next
    uint32_t checksum = adler(reinterpret_cast<const uint8_t*>(image.data()), image.size());
	filestream.write(reinterpret_cast<char*>(&checksum), sizeof(checksum));
	filestream.write(image.data(), image.size());
	// in case of a failed write...
	if (!filestream) {
		throw std::ios_base::failure( "fstream.write() failed in checkpoint_file::write" );
	}
}

// adds the map of free space to the free_space member, reconciling
// adjacent regions into one larger region in the process.

void checkpoint_ofstream::add_free_space()
{
#ifdef BOOST_ENABLE_ASSERT_HANDLER
	if(::stldb::tracing::get_trace_level() == stldb::finest_e) {
		STLDB_TRACE(stldb::finest_e, "(1) new_free_space at start of commit: ");
		for ( std::map<boost::interprocess::offset_t,std::size_t>::const_iterator
			  i = new_free_space.begin(); i != new_free_space.end(); i++ )
		{
			STLDB_TRACE(stldb::finest_e, "[" << i->first << "," << i->second << "]");
		}
		STLDB_TRACE(stldb::finest_e, "(2) free_by_offset at start of commit: ");
		for ( std::map<boost::interprocess::offset_t,std::size_t>::const_iterator
			  i = free_by_offset.begin(); i != free_by_offset.end(); i++ )
		{
			STLDB_TRACE(stldb::finest_e, "[" << i->first << "," << i->second << "]");
		}
	}
#endif

	// Reconcile new_free_space with free_space using free_by_offset
	// Combine all adjacent entries (defragment)
    free_by_offset.insert( new_free_space.begin(), new_free_space.end() );
    new_free_space.clear();

    for (std::map<boost::interprocess::offset_t,std::size_t>::iterator i = free_by_offset.begin();
         i != free_by_offset.end(); i++)
    {
         std::map<boost::interprocess::offset_t,std::size_t>::iterator j = i;
         j++;
         while (j != free_by_offset.end() && j->first <= i->first + boost::interprocess::offset_t(i->second)) {
		 	BOOST_ASSERT(j->first == i->first + boost::interprocess::offset_t(i->second));
             i->second += j->second;
             free_by_offset.erase(j);  // assumption: does not invalidate i
             j = i;
             j++;
         }
    }

#ifdef BOOST_ENABLE_ASSERT_HANDLER
	if(::stldb::tracing::get_trace_level() == stldb::finest_e) {
		STLDB_TRACE(stldb::finest_e, "(3) reconciled free_by_offset at end of commit: ");
		for ( std::map<boost::interprocess::offset_t,std::size_t>::const_iterator
			  i = free_by_offset.begin(); i != free_by_offset.end(); i++ )
		{
			STLDB_TRACE(stldb::finest_e, "[" << i->first << "," << i->second << "]");
		}
	}
#endif

    meta.free_space.clear();
    for (std::map<boost::interprocess::offset_t,std::size_t>::iterator i = free_by_offset.begin();
         i != free_by_offset.end(); i++) {
    	meta.free_space.insert(std::make_pair(i->second,i->first));
    }
}

// clear the checkpoint file.  All currently occupied space becomes part of
// new_free_space, to be made available on the next checkpoint.  This corresponds
// to cases where an operation like clear() or swap() invalidates everything which
// was previously checkpointed as part of that container.

void checkpoint_ofstream::clear()
{
	boost::interprocess::offset_t last_start = 0;
	for (std::map<boost::interprocess::offset_t,std::size_t>::iterator i = free_by_offset.begin();
	     i != free_by_offset.end(); i++)
	{
		if (last_start < i->first)
			new_free_space.insert( std::make_pair(last_start, i->first - last_start));
		last_start = i->first + i->second;
	}
	if (last_start < (boost::interprocess::offset_t)meta.file_length)
		new_free_space.insert( std::make_pair(last_start, meta.file_length - last_start));
}

// write out a new metafile instance with the indicated starting_lsn and ending_lsn

void checkpoint_ofstream::commit( transaction_id_t lsn_at_start, transaction_id_t lsn_at_end )
{
	// accumulated free space now becomes real free space for the next checkpoint
	add_free_space();

	std::ostringstream metafilename;
	metafilename.fill('0');
	metafilename <<  meta.container_name << "."
		<< std::right << std::setw(16) << std::hex << std::uppercase
		<< lsn_at_start << ".meta";

	meta.meta_filename = metafilename.str();
	meta.lsn_at_start = lsn_at_start;
	meta.lsn_at_end = lsn_at_end;

	// start by flushing all writes made to the checkpoint file, so that
	// it will be insync with the new metafile.
	filestream.flush();
	filestream.close();

	// write the metafile initially to a temp filename.
	boost::filesystem::path tempfilepath( checkpoint_dir );
	tempfilepath /= meta.meta_filename;
	tempfilepath.replace_extension("meta_wip");

	std::ofstream newfile(tempfilepath.string().c_str());
	boost_oarchive_t archive(newfile);
	archive & meta;

	// make certain that the contents of the new file are flushed to disk
	newfile.flush();
	newfile.close();

	// make this the new metafile by renaming it.
	boost::filesystem::path finalpath( checkpoint_dir );
	finalpath /= meta.meta_filename;
	try {
		boost::filesystem::rename( tempfilepath, finalpath );
	}
	catch (boost::filesystem::filesystem_error &ex) {
		STLDB_TRACE(severe_e, "Failed to rename " << tempfilepath.string() << " to " << finalpath.string() << ", " << ex.what());
		throw;
	}

	// clean up:
	// remove all previous meta files or meta_wip with this container name
	directory_iterator end_itr; // default construction yields past-the-end
	checkpoint_file_info info;
	std::vector<boost::filesystem::path> to_remove;
	for ( directory_iterator itr( checkpoint_dir ); itr != end_itr; ++itr )
	{
		if ( !is_directory(itr->status()) 
			 && (itr->path().extension() == ".meta" || itr->path().extension() == ".meta_wip") 
			 && itr->path() != finalpath
			 && itr->path().filename().compare(0, meta.container_name.length(), meta.container_name) == 0
			 && itr->path().filename()[meta.container_name.length()] == '.')
		{
			to_remove.push_back(itr->path());
	    }
	}
	for( std::vector<boost::filesystem::path>::const_iterator i = to_remove.begin();
			i != to_remove.end(); i++ )
	{
		try {
			boost::filesystem::remove( *i );
		}
		catch (...) {
			STLDB_TRACE(warning_e, "can't remove (clean-up) file: " << *i );
		}
	}
}


checkpoint_ifstream::~checkpoint_ifstream() {
	filestream.close();
}


void checkpoint_ifstream::open_checkpoint()
{
	boost::filesystem::path fullname( checkpoint_dir );
	fullname /= meta.ckpt_filename;
	filestream.open(fullname.string().c_str(), std::ios_base::in);
	if (!filestream) {
		std::ostringstream error;
		error << "fstream.open() failed in checkpoint_ifstream::open_checkpoint for "
			  << fullname.string();
		throw std::ios_base::failure( error.str() );
	}
}

} // namespace
