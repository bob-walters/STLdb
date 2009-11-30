//////////////////////////////////////////////////////////////////////////////
//
// (C) Copyright Ion Gaztanaga 2005-2008. Distributed under the Boost
// Software License, Version 1.0. (See accompanying file
// LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/interprocess for documentation.
//
//////////////////////////////////////////////////////////////////////////////

/**
 * NOTE: The contents of this file are meant to be donated back to boost::interprocess
 * in support of offset/len locking methods for the enhanced form of file_lock also
 * currently in this detail function.  This file contains methods which support that
 * paradigm.
 */
#ifndef BOOST_INTERPROCESS_DETAIL_OS_FILE_FUNCTIONS_EXT_HPP
#define BOOST_INTERPROCESS_DETAIL_OS_FILE_FUNCTIONS_EXT_HPP
#include <boost/interprocess/detail/os_file_functions.hpp>

namespace boost {
namespace interprocess {
namespace detail{


#if (defined BOOST_INTERPROCESS_WINDOWS)

inline bool acquire_file_lock(file_handle_t hnd, offset_t offset, size_t size)
{
   winapi::interprocess_overlapped overlapped;
   std::memset(&overlapped, 0, sizeof(overlapped));
   overlapped.dummy.offset = offset;
   const unsigned long len = (unsigned long)size;
   return winapi::lock_file_ex
      (hnd, winapi::lockfile_exclusive_lock, 0, len, 0, &overlapped);
}

inline bool try_acquire_file_lock(file_handle_t hnd, offset_t offset, size_t size, bool &acquired)
{
   winapi::interprocess_overlapped overlapped;
   std::memset(&overlapped, 0, sizeof(overlapped));
   overlapped.dummy.offset = offset;
   const unsigned long len = (unsigned long)size;
   if(!winapi::lock_file_ex
      (hnd, winapi::lockfile_exclusive_lock | winapi::lockfile_fail_immediately,
       0, len, 0, &overlapped)){
      return winapi::get_last_error() == winapi::error_lock_violation ?
               acquired = false, true : false;

   }
   return (acquired = true);
}

inline bool release_file_lock(file_handle_t hnd, offset_t offset, size_t size)
{
   const unsigned long len = (unsigned long)size;
   winapi::interprocess_overlapped overlapped;
   std::memset(&overlapped, 0, sizeof(overlapped));
   overlapped.dummy.offset = offset;
   return winapi::unlock_file_ex(hnd, 0, len, 0, &overlapped);
}

inline bool acquire_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size)
{
   const unsigned long len = (unsigned long)size;
   winapi::interprocess_overlapped overlapped;
   std::memset(&overlapped, 0, sizeof(overlapped));
   overlapped.dummy.offset = offset;
   return winapi::lock_file_ex(hnd, 0, 0, len, 0, &overlapped);
}

inline bool try_acquire_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size, bool &acquired)
{
   const unsigned long len = (unsigned long)size;
   winapi::interprocess_overlapped overlapped;
   std::memset(&overlapped, 0, sizeof(overlapped));
   overlapped.dummy.offset = offset;
   if(!winapi::lock_file_ex
      (hnd, winapi::lockfile_fail_immediately, 0, len, 0, &overlapped)){
      return winapi::get_last_error() == winapi::error_lock_violation ?
               acquired = false, true : false;
   }
   return (acquired = true);
}

inline bool release_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size)
{  return release_file_lock(hnd, offset, size);   }


#else    //#if (defined BOOST_WINDOWS) && !(defined BOOST_DISABLE_WIN32)


inline bool acquire_file_lock(file_handle_t hnd, offset_t offset, size_t size)
{
   struct ::flock lock;
   lock.l_type    = F_WRLCK;
   lock.l_whence  = SEEK_SET;
   lock.l_start   = offset;
   lock.l_len     = size;
   return -1 != ::fcntl(hnd, F_SETLKW, &lock);
}

inline bool try_acquire_file_lock(file_handle_t hnd, offset_t offset, size_t size, bool &acquired)
{
   struct ::flock lock;
   lock.l_type    = F_WRLCK;
   lock.l_whence  = SEEK_SET;
   lock.l_start   = offset;
   lock.l_len     = size;
   int ret = ::fcntl(hnd, F_SETLK, &lock);
   if(ret == -1){
      return (errno == EAGAIN || errno == EACCES) ?
               acquired = false, true : false;
   }
   return (acquired = true);
}

inline bool release_file_lock(file_handle_t hnd, offset_t offset, size_t size)
{
   struct ::flock lock;
   lock.l_type    = F_UNLCK;
   lock.l_whence  = SEEK_SET;
   lock.l_start   = offset;
   lock.l_len     = size;
   return -1 != ::fcntl(hnd, F_SETLK, &lock);
}

inline bool acquire_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size)
{
   struct ::flock lock;
   lock.l_type    = F_RDLCK;
   lock.l_whence  = SEEK_SET;
   lock.l_start   = offset;
   lock.l_len     = size;
   return -1 != ::fcntl(hnd, F_SETLKW, &lock);
}

inline bool try_acquire_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size, bool &acquired)
{
   struct flock lock;
   lock.l_type    = F_RDLCK;
   lock.l_whence  = SEEK_SET;
   lock.l_start   = offset;
   lock.l_len     = size;
   int ret = ::fcntl(hnd, F_SETLK, &lock);
   if(ret == -1){
      return (errno == EAGAIN || errno == EACCES) ?
               acquired = false, true : false;
   }
   return (acquired = true);
}

inline bool release_file_lock_sharable(file_handle_t hnd, offset_t offset, size_t size)
{  return release_file_lock(hnd,offset,size);   }


#endif   //#if (defined BOOST_WINDOWS) && !(defined BOOST_DISABLE_WIN32)

}  //namespace detail {
}  //namespace interprocess {
}  //namespace boost {

#endif   //BOOST_INTERPROCESS_DETAIL_OS_FILE_FUNCTIONS_EXT_HPP
