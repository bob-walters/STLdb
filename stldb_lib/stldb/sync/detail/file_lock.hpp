//////////////////////////////////////////////////////////////////////////////
//
// (C) Copyright Ion Gaztanaga 2005-2008. Distributed under the Boost
// Software License, Version 1.0. (See accompanying file
// LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/interprocess for documentation.
//
//////////////////////////////////////////////////////////////////////////////

#ifndef BOOST_INTERPROCESS_FILE_LOCK_EXT_HPP
#define BOOST_INTERPROCESS_FILE_LOCK_EXT_HPP

/**
 * NOTE: This is a slightly 'enhanced' form of boost::interprocess::file_lock which includes
 * support for offset/length based locking, rather than just full file locking
 */

#if (defined _MSC_VER) && (_MSC_VER >= 1200)
#  pragma once
#endif

#include <boost/interprocess/detail/config_begin.hpp>
#include <boost/interprocess/detail/workaround.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/detail/os_file_functions.hpp>
#include <stldb/sync/detail/os_file_functions_ext.hpp> // lock methods with offset support
#include <boost/interprocess/detail/os_thread_functions.hpp>
#include <boost/interprocess/detail/posix_time_types_wrk.hpp>
#include <boost/interprocess/detail/move.hpp>

//!\file
//!Describes a class that wraps file locking capabilities.

namespace boost {
namespace interprocess {
namespace ext {


//!A file lock, is a mutual exclusion utility similar to a mutex using a
//!file. A file lock has sharable and exclusive locking capabilities and
//!can be used with scoped_lock and sharable_lock classes.
//!A file lock can't guarantee synchronization between threads of the same
//!process so just use file locks to synchronize threads from different processes.
class file_lock
{
	/// @cond
	//Non-copyable
	BOOST_INTERPROCESS_MOVABLE_BUT_NOT_COPYABLE(file_lock)
	/// @endcond

	public:
   //!Constructs an empty file mapping.
   //!Does not throw
   file_lock()
      :  m_file_hnd(file_handle_t(detail::invalid_file()))
   {}

   //!Opens a file lock. Throws interprocess_exception if the file does not
   //!exist or there are no operating system resources.
   file_lock(const char *name);

   //!Moves the ownership of "moved"'s file mapping object to *this.
   //!After the call, "moved" does not represent any file mapping object.
   //!Does not throw
   file_lock(BOOST_INTERPROCESS_RV_REF(file_lock) moved)
      :  m_file_hnd(file_handle_t(detail::invalid_file()))
   {  this->swap(moved);   }

   //!Moves the ownership of "moved"'s file mapping to *this.
   //!After the call, "moved" does not represent any file mapping.
   //!Does not throw
   file_lock &operator=(BOOST_INTERPROCESS_RV_REF(file_lock) moved)
   {
      file_lock tmp(boost::interprocess::move(moved));
      this->swap(tmp);
      return *this;
   }

   //!Opens a file lock.  The lock is based on a portion of the file beginning
   //!at offset bytes from the start of the file, and extending from there
   //!for size bytes.
   file_lock(const char *name, offset_t offset, size_t size);

   //!Closes a file lock. Does not throw.
   ~file_lock();

   //!Swaps two file_locks.
   //!Does not throw.
   void swap(file_lock &other)
   {
      file_handle_t tmp1 = m_file_hnd;
	  offset_t tmp2 = m_offset;
      size_t tmp3 = m_size;
      m_file_hnd = other.m_file_hnd;
      m_offset = other.m_offset;
      m_size = other.m_size;
      other.m_file_hnd = tmp1;
      other.m_offset = tmp2;
      other.m_size = tmp3;
   }

   //Exclusive locking

   //!Effects: The calling thread tries to obtain exclusive ownership of the mutex,
   //!   and if another thread has exclusive, or sharable ownership of
   //!   the mutex, it waits until it can obtain the ownership.
   //!Throws: interprocess_exception on error.
   void lock();

   //!Effects: The calling thread tries to acquire exclusive ownership of the mutex
   //!   without waiting. If no other thread has exclusive, or sharable
   //!   ownership of the mutex this succeeds.
   //!Returns: If it can acquire exclusive ownership immediately returns true.
   //!   If it has to wait, returns false.
   //!Throws: interprocess_exception on error.
   bool try_lock();

   //!Effects: The calling thread tries to acquire exclusive ownership of the mutex
   //!   waiting if necessary until no other thread has has exclusive, or sharable
   //!   ownership of the mutex or abs_time is reached.
   //!Returns: If acquires exclusive ownership, returns true. Otherwise returns false.
   //!Throws: interprocess_exception on error.
   bool timed_lock(const boost::posix_time::ptime &abs_time);

   //!Precondition: The thread must have exclusive ownership of the mutex.
   //!Effects: The calling thread releases the exclusive ownership of the mutex.
   //!Throws: An exception derived from interprocess_exception on error.
   void unlock();

   //Sharable locking

   //!Effects: The calling thread tries to obtain sharable ownership of the mutex,
   //!   and if another thread has exclusive ownership of the mutex, waits until
   //!   it can obtain the ownership.
   //!Throws: interprocess_exception on error.
   void lock_sharable();

   //!Effects: The calling thread tries to acquire sharable ownership of the mutex
   //!   without waiting. If no other thread has has exclusive ownership of the
   //!   mutex this succeeds.
   //!Returns: If it can acquire sharable ownership immediately returns true. If it
   //!   has to wait, returns false.
   //!Throws: interprocess_exception on error.
   bool try_lock_sharable();

   //!Effects: The calling thread tries to acquire sharable ownership of the mutex
   //!   waiting if necessary until no other thread has has exclusive ownership of
   //!   the mutex or abs_time is reached.
   //!Returns: If acquires sharable ownership, returns true. Otherwise returns false.
   //!Throws: interprocess_exception on error.
   bool timed_lock_sharable(const boost::posix_time::ptime &abs_time);

   //!Precondition: The thread must have sharable ownership of the mutex.
   //!Effects: The calling thread releases the sharable ownership of the mutex.
   //!Throws: An exception derived from interprocess_exception on error.
   void unlock_sharable();
   /// @cond
   private:
   file_handle_t m_file_hnd;
   offset_t m_offset;
   size_t m_size;

   bool timed_acquire_file_lock
      (file_handle_t hnd, bool &acquired, const boost::posix_time::ptime &abs_time)
   {
      //Obtain current count and target time
      boost::posix_time::ptime now = microsec_clock::universal_time();
      using namespace boost::detail;

      if(now >= abs_time) return false;

      do{
         if(!detail::try_acquire_file_lock(hnd, m_offset, m_size, acquired))
            return false;

         if(acquired)
            return true;
         else{
            now = microsec_clock::universal_time();

            if(now >= abs_time){
               acquired = false;
               return true;
            }
            // relinquish current time slice
            detail::thread_yield();
         }
      }while (true);
   }

   bool timed_acquire_file_lock_sharable
      (file_handle_t hnd, bool &acquired, const boost::posix_time::ptime &abs_time)
   {
      //Obtain current count and target time
      boost::posix_time::ptime now = microsec_clock::universal_time();
      using namespace boost::detail;

      if(now >= abs_time) return false;

      do{
         if(!detail::try_acquire_file_lock_sharable(hnd, m_offset, m_size, acquired))
            return false;

         if(acquired)
            return true;
         else{
            now = microsec_clock::universal_time();

            if(now >= abs_time){
               acquired = false;
               return true;
            }
            // relinquish current time slice
            detail::thread_yield();
         }
      }while (true);
   }
   /// @endcond
};

inline file_lock::file_lock(const char *name)
    : m_offset(0)
    , m_size(0)
{
   m_file_hnd = detail::open_existing_file(name, read_write);

   if(m_file_hnd == detail::invalid_file()){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

inline file_lock::file_lock(const char *name, offset_t offset, size_t size)
	: m_offset(offset)
	, m_size(size)
{
   m_file_hnd = detail::open_existing_file(name, read_write);

   if(m_file_hnd == detail::invalid_file()){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

inline file_lock::~file_lock()
{
   if(m_file_hnd != detail::invalid_file()){
      detail::close_file(m_file_hnd);
      m_file_hnd = detail::invalid_file();
   }
}

inline void file_lock::lock()
{
   if(!detail::acquire_file_lock(m_file_hnd, m_offset, m_size)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

inline bool file_lock::try_lock()
{
   bool result;
   if(!detail::try_acquire_file_lock(m_file_hnd, m_offset, m_size, result)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
   return result;
}

inline bool file_lock::timed_lock(const boost::posix_time::ptime &abs_time)
{
   if(abs_time == boost::posix_time::pos_infin){
      this->lock();
      return true;
   }
   bool result;
   if(!this->timed_acquire_file_lock(m_file_hnd, result, abs_time)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
   return result;
}

inline void file_lock::unlock()
{
   if(!detail::release_file_lock(m_file_hnd, m_offset, m_size)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

inline void file_lock::lock_sharable()
{
   if(!detail::acquire_file_lock_sharable(m_file_hnd, m_offset, m_size)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

inline bool file_lock::try_lock_sharable()
{
   bool result;
   if(!detail::try_acquire_file_lock_sharable(m_file_hnd, m_offset, m_size, result)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
   return result;
}

inline bool file_lock::timed_lock_sharable(const boost::posix_time::ptime &abs_time)
{
   if(abs_time == boost::posix_time::pos_infin){
      this->lock_sharable();
      return true;
   }
   bool result;
   if(!this->timed_acquire_file_lock_sharable(m_file_hnd, result, abs_time)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
   return result;
}

inline void file_lock::unlock_sharable()
{
   if(!detail::release_file_lock_sharable(m_file_hnd, m_offset, m_size)){
      error_info err(system_error_code());
      throw interprocess_exception(err);
   }
}

}  //namespace ext {
}  //namespace interprocess {
}  //namespace boost {

#include <boost/interprocess/detail/config_end.hpp>

#endif   //BOOST_INTERPROCESS_FILE_LOCK_HPP
