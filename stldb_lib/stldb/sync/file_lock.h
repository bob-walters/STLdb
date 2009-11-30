/*
 * file_lock.h
 *
 *  Created on: Mar 8, 2009
 *      Author: bobw
 */

#ifndef FILE_LOCK_H_
#define FILE_LOCK_H_

#include <cstdio>
#include <ios>
#include <fstream>

#include <stldb/sync/detail/os_file_functions_ext.hpp>
#include <stldb/sync/detail/file_lock.hpp>

using std::ofstream;
using boost::interprocess::offset_t;

namespace stldb {

/**
 * This is a file_lock which creates the file being locked, and
 * destroys the file when the lock is released.  Useful for cases
 * where the file exists for no other purpose than to facilitate the lock
 */
class file_lock : public ofstream, public boost::interprocess::ext::file_lock
{
public:
	   /// @cond
	   //Non-copyable
	   file_lock();
	   file_lock(const file_lock &);
	   file_lock &operator=(const file_lock &);
	   /// @endcond

public:
	   //!Opens a file lock. Throws interprocess_exception if the file does not
	   //!exist or there are no operating system resources.
	   file_lock(const char *name);

	   //!Opens a file lock.  The lock is based on a portion of the file beginning
	   //!at offset bytes from the start of the file, and extending from there
	   //!for size bytes.
	   file_lock(const char *name, offset_t offset, size_t size);

	   //!Closes a file lock. Does not throw.
	   ~file_lock();

	   const char* filename() { return _filename.c_str(); }

private:
	std::string _filename;
};

//!Opens a file lock. Throws interprocess_exception if the file does not
//!exist or there are no operating system resources.
inline file_lock::file_lock(const char *name)
	: ofstream(name, ios_base::out)
	, boost::interprocess::ext::file_lock(name)
	, _filename(name)
{ }

//!Opens a file lock.  The lock is based on a portion of the file beginning
//!at offset bytes from the start of the file, and extending from there
//!for size bytes.
inline file_lock::file_lock(const char *name, offset_t offset, size_t size)
	: ofstream(name, std::ios_base::out)
	, boost::interprocess::ext::file_lock(name, offset, size)
	, _filename(name)
{ }

//!Deletes the file
inline file_lock::~file_lock()
{
	ofstream::close();
	// ::remove( _filename.c_str() );  // shouldn't be automatic.
}


} // namespace

#endif /* FILE_LOCK_H_ */
