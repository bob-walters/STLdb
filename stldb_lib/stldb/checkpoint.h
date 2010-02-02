/*
 * checkpoint_metafile.h
 *
 *  Created on: Jan 12, 2010
 *      Author: bobw
 */

#ifndef STLDB_CHECKPOINT_H_
#define STLDB_CHECKPOINT_H_

#include <map>
#include <iterator>
#include <fstream>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/serialization/map.hpp>

#include <stldb/cachetypes.h>
#include <stldb/trace.h>
#include <stldb/detail/os_file_functions_ext.h>
#include <stldb/detail/checkpoint_file_info.h>

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


typedef std::pair<boost::interprocess::offset_t, std::size_t> checkpoint_loc_t;


class checkpoint_fstream_base {
public:
	// construct from the contents of the specific metafile passed.
	checkpoint_fstream_base(const boost::filesystem::path &metafile);

	// construct by searching the checkpoint directory for a current
	// metafile for the container with the indicated name.
	checkpoint_fstream_base(const boost::filesystem::path &checkpoint_path,
			const char * container_name);

	// construct based on the already read contents of the metafile
	checkpoint_fstream_base(const boost::filesystem::path &checkpoint_path,
			const checkpoint_file_info &checkpoint_info);

	virtual ~checkpoint_fstream_base();

	static const int extension_size = 64*1024*1024; // TODO - part of config
	static const int entry_alignment = 64;  // TODO - part of config.  To improve chance of reuse with fragmentation

	boost::filesystem::path  checkpoint_dir;
	checkpoint_file_info  meta;

	std::map<boost::interprocess::offset_t,std::size_t> free_by_offset;

private:
	void prepare_free_by_offset();

	checkpoint_fstream_base();  //unimplemented
	checkpoint_fstream_base(const checkpoint_fstream_base &rarg); // unimplemented
	checkpoint_fstream_base& operator=(const checkpoint_fstream_base &rarg); // unimplemented
};


class checkpoint_ofstream : protected checkpoint_fstream_base
{
public:
	// construct from the contents of the specific metafile passed.
	checkpoint_ofstream(const boost::filesystem::path &metafile)
		: checkpoint_fstream_base(metafile), filestream()
		  { open_checkpoint(); }

	// construct by searching the checkpoint directory for a current
	// metafile for the container with the indicated name.
	checkpoint_ofstream(const boost::filesystem::path &checkpoint_path,
			const char * container_name)
		: checkpoint_fstream_base(checkpoint_path,container_name), filestream()
		  { open_checkpoint(); }

	checkpoint_ofstream(const boost::filesystem::path &checkpoint_path,
			const checkpoint_file_info &checkpoint_info)
		: checkpoint_fstream_base(checkpoint_path,checkpoint_info), filestream()
		  { open_checkpoint(); }

	virtual ~checkpoint_ofstream();

	// have the ofstream set the new free space within the checkpoint to include
	// all currently used space.  This fits when the checkpoint is being done after
	// a container has been cleared, or has been swapped with another container.
	void clear();

	// used to initialize new_free_space with space accumulated on a container
	// from entry erasure.
	template <class InputIterator>
	void add_free_space( InputIterator begin, InputIterator end ) {
		new_free_space.insert(begin,end);
	}

	// allocate space for a new copy of obj from among the committed free space.
	// return the location that the new copy of the object has been stored in.
	// add the previous checkpoint_loc_t to the pending free space.  The pending
	// free space is made permanent when the commit() method is called.
	template <class T>
	checkpoint_loc_t
	write( const T& obj, checkpoint_loc_t prev_loc)
	{
		// If the value we are saving previously occupied some portion
		// of the checkpoint file, we can add its previous region to the
		// new free space.
		if (prev_loc.second >0) {
			STLDB_TRACE(finer_e, "Space freed by new copy of previously checkpointed object: [offset,size] : [" << prev_loc.first << "," << prev_loc.second << "]");
			new_free_space.insert( prev_loc );
		}

		// serialize the entry into a self-contained serialized image.
		stringbuff.str(""); // reset buffer contents
	    boost_oarchive_t archive(stringbuff);
	    archive & obj;
	    std::string image = stringbuff.str();

	    // allocate space from within the free space in the checkpoint file.
	    checkpoint_loc_t space = this->allocate( image.size() );

	    // write the object to the checkpoint file.
	    this->write( space.first, space.second, image );
	    return space;
	}

	// sync all writes and write a new metafile, so that all changes made thus far
	// become permanant.
	void commit(transaction_id_t lsn_at_start, transaction_id_t lsn_at_end);

protected:
	// allocate a region in the checkpoint file of the designated size
	// from among the currently available free space.  Best-fit algorithm
	checkpoint_loc_t allocate(std::size_t size);

	// write a serialized image to the checkpoint file at the offset indicated.
	// throws ios_base::failure upon I/O error.
	void write( boost::interprocess::offset_t offset, std::size_t length, std::string image);

	// adds 'new_free_space' to the free_space on the metadata object, reconciling
	// adjacent regions into one larger region in the process.
	void add_free_space();

private:
	std::map<boost::interprocess::offset_t,std::size_t> new_free_space;
	std::ofstream filestream;
	std::ostringstream stringbuff;

	void open_checkpoint();
};

template <class T> class checkpoint_iterator;

class checkpoint_ifstream : protected checkpoint_fstream_base {
public:
	// construct from the contents of the specific metafile passed.
	checkpoint_ifstream(const boost::filesystem::path &metafile)
		: checkpoint_fstream_base(metafile), filestream()
		  { open_checkpoint(); }

	// construct by searching the checkpoint directory for a current
	// metafile for the container with the indicated name.
	checkpoint_ifstream(const boost::filesystem::path &checkpoint_path,
			const char * container_name)
		: checkpoint_fstream_base(checkpoint_path,container_name), filestream()
		  { open_checkpoint(); }

	checkpoint_ifstream(const boost::filesystem::path &checkpoint_path,
			const checkpoint_file_info &checkpoint_info)
		: checkpoint_fstream_base(checkpoint_path,checkpoint_info), filestream()
		  { open_checkpoint(); }

	virtual ~checkpoint_ifstream();

	// returns an input iterator positioned at the start of the file which can be
	// used to sequentially fetch the objects in the file.  T must be serializable.
	template<class T>
	checkpoint_iterator<T> begin() {
		return checkpoint_iterator<T>(*this, meta.file_length, 0);
	}

	// returns the input iterator designating the end of the checkpoint file.
	template<class T>
	checkpoint_iterator<T> end() {
		return checkpoint_iterator<T>(*this, meta.file_length, meta.file_length);
	}

private:
	std::ifstream filestream;

	template <class T> friend class checkpoint_iterator;

	checkpoint_ifstream();  //unimplemented
	checkpoint_ifstream(const checkpoint_ifstream &rarg); // unimplemented
	checkpoint_ifstream& operator=(const checkpoint_ifstream &rarg); // unimplemented

	void open_checkpoint();
};


/**
 * An input iterator which can be used to read the contents of a current
 * checkpoint file, sequentially, from start to end.  Usefull for load_checkpoint.
 */
template <class T>
class checkpoint_iterator : public std::iterator<std::input_iterator_tag, T,
		std::ptrdiff_t, const T*, const T&>
{
public:
	typedef T         value_type;
	typedef const T&  reference;
	typedef const T*  pointer;

	checkpoint_iterator(const checkpoint_iterator &rarg)
		: _offset(rarg._offset), _length(rarg._length), _checkpoint(rarg._checkpoint)
		, _current(rarg._current), _current_loc(rarg._current_loc)
		{ }

	bool operator==(const checkpoint_iterator& rarg) {
		return (this->_offset == rarg._offset);
	}
	bool operator!=(const checkpoint_iterator& rarg) {
		return (this->_offset != rarg._offset);
	}
	bool operator<(const checkpoint_iterator& rarg) {
		return (this->_offset < rarg._offset);
	}

	inline reference operator*() const
		{ return _current; }

	inline pointer operator->() const
		{ return &_current; }

	// Read next entry out of the file, into _current
	checkpoint_iterator& operator++()
		{ this->forward(); return *this; }

	checkpoint_iterator operator++(int n)
		{ checkpoint_iterator temp = *this; this->forward(); return temp; }

	checkpoint_loc_t checkpoint_location() const
		{ return _current_loc; }

private:
	friend class checkpoint_ifstream;

	// constructs an iterator at the indicated offset
	checkpoint_iterator(checkpoint_ifstream &ckpt, std::size_t ckpt_len, boost::interprocess::offset_t off = 0 )
		: _offset(off), _length(ckpt_len), _checkpoint(ckpt), _current(), _current_loc()
		{ this->forward(); }

	// advance forward to the next item in the file.
	void forward() 
	{
		std::map<boost::interprocess::offset_t,std::size_t>::const_iterator i(_checkpoint.free_by_offset.find(_offset));
		while (i != _checkpoint.free_by_offset.end()) {
			_offset += i->second;
			i = _checkpoint.free_by_offset.find(_offset);
		}
		if (std::size_t(_offset) == _length)
			return;

		std::size_t size;
		_checkpoint.filestream.seekg(_offset, std::ios_base::beg);
		// TODO - should this be in network byte order for portability.?
		// are binary streams from Boost.Serialization portable to some extent?
		_checkpoint.filestream.read(reinterpret_cast<char*>(&size), sizeof(size));
		boost_iarchive_t archive(_checkpoint.filestream);
        archive & _current;
		if (!_checkpoint.filestream) {
			throw std::ios_base::failure( "fstream.write() failed in checkpoint_file::write" );
		}
		_current_loc = std::make_pair(_offset, size);
		_offset += size;
	}

	boost::interprocess::offset_t _offset; // -1 for end of file.
	std::size_t _length; // length of checkpoint file
	checkpoint_ifstream &_checkpoint;
	T _current; // last deserialized instance of T
	checkpoint_loc_t _current_loc; // location of _current
};


} // namespace

#endif /* CHECKPOINT_METAFILE_H_ */
