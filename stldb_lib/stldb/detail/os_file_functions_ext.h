/*
 * io_posix.h
 *
 *  Created on: May 2, 2009
 *      Author: bobw
 */

#ifndef STLDB_OS_FILE_FUNCTIONS_EXT_H
#define STLDB_OS_FILE_FUNCTIONS_EXT_H

#include <boost/interprocess/detail/os_file_functions.hpp>

#if (!defined BOOST_INTERPROCESS_WINDOWS)
#include <sys/uio.h>   // struct iovec
#endif


namespace stldb {
namespace io {


#if (defined BOOST_INTERPROCESS_WINDOWS)

static const char* file_seperator="\\";

typedef boost::interprocess::file_handle_t file_handle_t;
typedef struct iovec {
	void       *iov_base;
	std::size_t iov_len;
} write_region_t;

namespace winapi {

#if !defined( BOOST_USE_WINDOWS_H )
  extern "C" __declspec(dllimport) int __stdcall ReadFile(void *hnd, void *buffer, unsigned long bytes_to_read,
		unsigned long* bytes_read, boost::interprocess::winapi::interprocess_overlapped* overlapped);
#endif

  static inline bool read_file(void *hnd, void *buffer, unsigned long bytes_to_read, unsigned long *bytes_read,
		boost::interprocess::winapi::interprocess_overlapped* overlapped)
  { return 0 != ReadFile(hnd, buffer, bytes_to_read, bytes_read, overlapped); }

} // namespace winapi

inline std::size_t read_file(file_handle_t f, void *buf, std::size_t nbyte) {
	unsigned long read = 0;
	return (0 != winapi::read_file(f, buf, (unsigned long)nbyte, &read, 0)
		? (std::size_t)read : 0);
}

inline bool write_file(file_handle_t f, const void *buf, std::size_t nbytes) {
	return boost::interprocess::detail::write_file(f, buf, nbytes);
}

inline bool gathered_write_file(file_handle_t f, write_region_t *buffs, int buf_count) {
	bool result = true;
	for (int i=0; i<buf_count && result; i++) {
		result &= write_file(f, buffs[i].iov_base, (unsigned long)buffs[i].iov_len);
	}
	return result;
}

inline int sync(file_handle_t f) {
	return 0;
}

#else

// Define the structure defining one segment of memory to write for the gathering
// write method.
typedef boost::interprocess::file_handle_t file_handle_t;
typedef struct iovec write_region_t;

/**
 * Read up to nbyte bytes of data from file f, into the buffer
 * pointed to by buf.  Return the # of bytes read.  0 indicates
 * EOF, a negative value indicates some class of error which is OS specific.
 * (see errno)
 */
inline std::size_t read_file(file_handle_t f, void *buf, std::size_t nbyte) {
	std::size_t read, total = 0;
	while ((read = ::read(f, buf, nbyte)) < nbyte) {
		if (read == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (read == 0) { // EOF
			return total;
		}
		buf = reinterpret_cast<char*>(buf) + read;
		nbyte -= read;
		total += read;
	}
	total += read;
	return total;
}

/**
 * Write N bytes of data, from buff, out to f.  if interrupted or
 * only some bytes are written, will issue additional write calls until
 * either all nbytes are written out, or an error occurs.
 */
inline bool write_file(file_handle_t f, const void *buf, std::size_t nbytes) {
	ssize_t written = 0, total_written = 0;
	while ((written = ::write(f, buf, nbytes)) < (ssize_t)nbytes) {
		if (written == -1) {
			if (errno == EINTR)
				continue;
			return false;
		}
		buf = reinterpret_cast<const char*>(buf) + written;
		nbytes -= written;
		total_written += written;
	}
	total_written += written;
	return ((std::size_t)total_written == nbytes);
}

/**
 * Write out all of the data contained in buffs, issuing multiple writes
 * in the case of interrupts, of partial completion.
 */
inline bool gathered_write_file(file_handle_t f, struct iovec *buffs, int buf_count) {
	ssize_t written = 0, total_written = 0;
	size_t total_to_write = 0, bytes_remaining = 0;
	for (int i=0; i<buf_count; i++) {
		total_to_write += buffs[i].iov_len;
	}
	bytes_remaining = total_to_write;
	while ((written = ::writev(f, buffs, buf_count)) < (ssize_t)bytes_remaining) {
		if (written == -1) {
			if (errno == EINTR)
				continue;  // just try again...
			return false;
		}
		// set buffs and buf_count to the amount which remains to write.
		total_written += written;
		bytes_remaining -= written;
		while (buf_count && ssize_t(buffs[0].iov_len) < written) {
			written -= buffs[0].iov_len;
			buffs++;
			buf_count--;
		}
		buffs[0].iov_base = reinterpret_cast<char*>(buffs[0].iov_base) + written;
		buffs[0].iov_len -= written;
	}
	total_written += written;
	return ( total_written ==  (ssize_t)total_to_write );
}

/**
 * Synchronize the files in-core (memory) state with the disk.  Of course, the disk is not
 * absolutely guaranteed to have written out it's entire
 */
inline int sync(file_handle_t f) {
	return ::fsync(f);
}

#endif

} // namespace io
} // namespace stldb

#endif /* IO_POSIX_H_ */
