
#ifndef STLDB_DYNAMIC_MANAGED_MAPPED_FILE_HPP
#define STLDB_DYNAMIC__MANAGED_MAPPED_FILE_HPP

#if (defined _MSC_VER) && (_MSC_VER >= 1200)
#  pragma once
#endif

#include <boost/interprocess/managed_mapped_file.hpp>

namespace stldb {

template < class CharType
         , class AllocationAlgorithm
         , template<class IndexConfig> class IndexType
         >
class dynamic_managed_mapped_file
   : public basic_managed_mapped_file
      <CharType, AllocationAlgorithm, IndexType>
{
   /// @cond
   public:
   typedef detail::basic_managed_mapped_file
      <CharType, AllocationAlgorithm, IndexType,
      detail::managed_open_or_create_impl<detail::file_wrapper>::ManagedOpenOrCreateUserOffset>   base_t;

   typedef typename base_t::device_type device_type;

   typedef typename MemoryAlgorithm::mutex_family::mutex_type   mutex_type;

   private:

   dynamic_managed_mapped_file *get_this_pointer()
   {  return this;   }

   private:
   typedef typename base_t::char_ptr_holder_t   char_ptr_holder_t;
   BOOST_INTERPROCESS_MOVABLE_BUT_NOT_COPYABLE(dynamic_managed_mapped_file)
   /// @endcond

   public: //functions

   //!Creates mapped file and creates and places the segment manager. 
   //!This can throw.
   dynamic_managed_mapped_file()
   {}

   //!Creates mapped file and creates and places the segment manager. 
   //!This can throw.
   dynamic_managed_mapped_file(create_only_t create_only, const char *name,
                             std::size_t size, const void *addr = 0)
      : m_mfile(create_only, name, size, read_write, addr, 
                create_open_func_t(get_this_pointer(), detail::DoCreate))
   {}

   //!Creates mapped file and creates and places the segment manager if
   //!segment was not created. If segment was created it connects to the
   //!segment.
   //!This can throw.
   dynamic_managed_mapped_file (open_or_create_t open_or_create,
                              const char *name, std::size_t size, 
                              const void *addr = 0)
      : dynamic_managed_mapped_file(open_or_create, name, size, read_write, addr,
                create_open_func_t(get_this_pointer(), 
                detail::DoOpenOrCreate))
   {}

   //!Connects to a created mapped file and its segment manager.
   //!This can throw.
   dynamic_managed_mapped_file (open_only_t open_only, const char* name,
                              const void *addr = 0)
      : dynamic_managed_mapped_file(open_only, name, read_write, addr,
                create_open_func_t(get_this_pointer(), 
                detail::DoOpen))
   {}

   //!Connects to a created mapped file and its segment manager
   //!in copy_on_write mode.
   //!This can throw.
   dynamic_managed_mapped_file (open_copy_on_write_t, const char* name,
                              const void *addr = 0)
      : dynamic_managed_mapped_file(open_only, name, copy_on_write, addr,
                create_open_func_t(get_this_pointer(), 
                detail::DoOpen))
   {}

   //!Connects to a created mapped file and its segment manager
   //!in read-only mode.
   //!This can throw.
   dynamic_managed_mapped_file (open_read_only_t, const char* name,
                              const void *addr = 0)
      : dynamic_managed_mapped_file(open_only, name, read_only, addr,
                create_open_func_t(get_this_pointer(), 
                detail::DoOpen))
   {}

   //!Moves the ownership of "moved"'s managed memory to *this.
   //!Does not throw
   dynamic_managed_mapped_file(BOOST_INTERPROCESS_RV_REF(dynamic_managed_mapped_file) moved)
   {
      this->swap(moved);
   }

   //!Moves the ownership of "moved"'s managed memory to *this.
   //!Does not throw
   dynamic_managed_mapped_file &operator=(BOOST_INTERPROCESS_RV_REF(dynamic_managed_mapped_file) moved)
   {
	  dynamic_managed_mapped_file tmp(boost::interprocess::move(moved));
      this->swap(tmp);
      return *this;
   }

   //!Destroys *this and indicates that the calling process is finished using
   //!the resource. The destructor function will deallocate
   //!any system resources allocated by the system for use by this process for
   //!this resource. The resource can still be opened again calling
   //!the open constructor overload. To erase the resource from the system
   //!use remove().
   ~dynamic_managed_mapped_file()
   {
	   // TODO - may need to do something with m_additional_mappings below.
   }

   //!Swaps the ownership of the managed mapped memories managed by *this and other.
   //!Never throws.
   void swap(dynamic_managed_mapped_file &other)
   {
      base_t::swap(other);
   }

   //!Tries to resize the mapped file so that we have room
   //!for more objects.  Performs this growth in-place, on
   //!an opened memory mapped file.  Can only be called
   //!when a single process is using the region.  If other
   //!processes are connected, they could end up traversing
   //!a pointer to the extended portion of the region, which
   //!will cause a segmentation violation for those processes.
   //!the method is not synchronized, and should only be called
   //!by one thread within a given process.
   bool grow_in_place(std::size_t extra_bytes)
   {
	   // current mappings
	   void *base = this->get_address();
	   std::size_t current_size = this->get_size();
	   void *mapping_addr = reinterpret_cast<CharType*>(base) + current_size;

	   // extend the memory mapped file on disk
       device_type f(open_or_create, filename, read_write);
       f.truncate(current_size + extra_bytes);

       // map in the last extra_bytes portion of f at
       // mapping_addr.
       mapped_region region_extension(f, read_write, current_size, extra_bytes, mapping_addr);
       m_addtional_mappings.resize( m_addtional_mappings.size() + 1 );
       m_additional_mappings[ m_additional_mappings.size() - 1 ].swap(region_extension);

       // add the additional memory to the segment manager.  grow is not thread-safe,
       // so we need to do this via atomic_func.
       base_t::atomic_func( base_t::grow(extra_bytes) );
   }

   /// @cond
   private:
	   std::vactor<mapped_region> m_additional_mappings;
};

}  //namespace stldb

#endif   //STLDB_DYNAMIC__MANAGED_MAPPED_FILE_HPP
