
#ifndef STLDB_DYNAMIC_MANAGED_MAPPED_FILE_HPP
#define STLDB_DYNAMIC__MANAGED_MAPPED_FILE_HPP

#if (defined _MSC_VER) && (_MSC_VER >= 1200)
#  pragma once
#endif

#include <vector>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/detail/config_begin.hpp>
#include <boost/interprocess/detail/move.hpp>
#include <boost/interprocess/file_mapping.hpp>

using boost::interprocess::basic_managed_mapped_file;

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

   typedef basic_managed_mapped_file<CharType, AllocationAlgorithm, IndexType> base_t;
   typedef typename base_t::device_type device_type;

   private:

   dynamic_managed_mapped_file *get_this_pointer()
   {  return this;   }

   private:
   typedef typename base_t::char_ptr_holder_t   char_ptr_holder_t;

   //TODO:
   BOOST_INTERPROCESS_MOVABLE_BUT_NOT_COPYABLE(dynamic_managed_mapped_file)

   /// @endcond

   public: //functions

   //!Creates mapped file and creates and places the segment manager. 
   //!This can throw.
   dynamic_managed_mapped_file()
   {}

   //!Creates mapped file and creates and places the segment manager. 
   //!This can throw.
   dynamic_managed_mapped_file(boost::interprocess::create_only_t create_only,const char *name,
                             std::size_t size, const void *addr = 0)
      : basic_managed_mapped_file(create_only, name, size, addr)
   {  register_region(this); }

   //!Creates mapped file and creates and places the segment manager if
   //!segment was not created. If segment was created it connects to the
   //!segment.
   //!This can throw.
   dynamic_managed_mapped_file (boost::interprocess::open_or_create_t open_or_create,
                              const char *name, std::size_t size, 
                              const void *addr = 0)
	   : basic_managed_mapped_file(open_or_create, name, size, addr)
   {  register_region(this); }

   //!Connects to a created mapped file and its segment manager.
   //!This can throw.
   dynamic_managed_mapped_file (boost::interprocess::open_only_t open_only, const char* name,
                              const void *addr = 0)
	   : basic_managed_mapped_file(open_only, name, addr)
   {  register_region(this); }

   //!Connects to a created mapped file and its segment manager
   //!in copy_on_write mode.
   //!This can throw.
   dynamic_managed_mapped_file (boost::interprocess::open_copy_on_write_t open_copy_on_write, const char* name,
                              const void *addr = 0)
	   : basic_managed_mapped_file(open_copy_on_write, name, addr)
   {  register_region(this); }

   //!Connects to a created mapped file and its segment manager
   //!in read-only mode.
   //!This can throw.
   dynamic_managed_mapped_file (boost::interprocess::open_read_only_t open_read_only, const char* name,
                              const void *addr = 0)
	   : basic_managed_mapped_file(open_read_only, name, addr)
   {  register_region(this); }

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
	   unregister_region(this);
   }

   //!Swaps the ownership of the managed mapped memories managed by *this and other.
   //!Never throws.
   void swap(dynamic_managed_mapped_file &other)
   {
	  swap_registrations(this, &other);
      base_t::swap(other);
   }

   std::size_t get_extension_size() const {
	   return m_extension_size;
   }

   void set_extension_size(std::size_t s) {
	   m_extension_size = s;
   }
   //!Tries to resize the mapped file so that we have room
   //!for more objects.  Performs this growth in-place, on
   //!an opened memory mapped file.  Can only be safely called
   //!when a single process is using the region, unless you
   //!devise some way to advice all other processes to all call
   //!the method in a reasonable timeframe.  If other
   //!processes are connected, and don't also grow their mapping
   //!, they could end up traversing
   //!a pointer to the extended portion of the region, which
   //!will cause a segmentation violation for those processes.
   //!the method is synchronized.
   bool grow_in_place(std::size_t current_size)
   {
	   bool result;
	   boost::bind f( &dynamic_managed_mapped_file::grow_in_place_impl, this, current_size, &result);
	   this->get_segment_manager()->atomic_func(f);
	   return result;
   }

   //!Return the dynamic_managed_mapped_file& for the object in the current
   //!process which has the indicated segment_manager.
   static dynamic_managed_mapped_file* region_at(segment_manager *addr) {
	   std::map<get_segment_manager*,dynamic_managed_mapped_file *>::iterator
		   result = s_registered_mappings.find(addr);
	   return (result == s.registered_mappings.end() ?
			   NULL : result->second);
   }

   /// @cond
   private:
	   std::vector<boost::interprocess::mapped_region> m_additional_mappings;
	   std::size_t m_extension_size;

	   void grow_in_place_impl(std::size_t expected_size, bool *result)
	   {
	       *result = false;

	       // current mappings
		   void *base = this->get_address();
		   std::size_t current_size = this->get_size();
		   void *mapping_addr = reinterpret_cast<CharType*>(base) + current_size;

		   // Check against MT race conditons in calls to this from allocators.
		   if (current_size > expected_size) {
			   // some other thread also got bad_alloc exceptions, and beat us
			   // in the race to get in here and resize.
			   *result = true;
			   return;
		   }

		   // extend the memory mapped file on disk, but only if some other
		   // process has not already done so.  If for some reason, we find
		   // the file to be larger than what we have already mapped, then
		   // just extend our mapping to include that file.
	       device_type f(boost::interprocess::open_or_create, filename,
	    		   boost::interprocess::read_write);
	       std::size_t old_size;
	       if(!f.get_size(old_size))
	          return false;

	       std::size_t extension_size = (old_size - current_size);
	       if (old_size == current_size) {
	    	   f.truncate(old_size + m_extension_size);
	    	   extension_size += m_extension_size;
	       }

	       // map in the last extra_bytes portion of f at
	       // mapping_addr.
	       boost::interprocess::mapped_region region_extension(f, read_write, current_size, extension_size, mapping_addr);
	       m_addtional_mappings.resize( m_addtional_mappings.size() + 1 );
	       m_additional_mappings[ m_additional_mappings.size() - 1 ].swap(region_extension);

	       // add the additional memory to the segment manager.  grow is not thread-safe,
	       // so we need to do this via atomic_func.
	       base_t::atomic_func( base_t::grow(extra_bytes) );
	       *result = true;
	   }

	   static void register_region(dynamic_managed_mapped_file *file) {
		   const segment_manager *mgr = file->get_segment_manager();
		   s_registered_mappings.insert(mgr, file);
	   }

	   static void unregister_region(dynamic_managed_mapped_file *file) {
		   const segment_manager *mgr = file->get_segment_manager();
		   std::map<segment_manager*,dynamic_managed_mapped_file *>::iterator
			   result = s_registered_mappings.find(mgr);
		   if (result != s_registered_mappings.end()) {
			   s_registered_mappings.erase(result);
		   }
	   }

	   static void swap_registrations(dynamic_managed_mapped_file *file1, dynamic_managed_mapped_file *file2) {
		   const segment_manager *mgr1 = file1->get_segment_manager();
		   const segment_manager *mgr2 = file1->get_segment_manager();
		   std::map<segment_manager*,dynamic_managed_mapped_file *>::iterator
			   i = s_registered_mappings.find(mgr1);
		   assert(i->second = file1);
		   i->second = file2;
		   i = s_registered_mappings.find(mgr2);
		   assert(i->second = file2);
		   i->second = file1;
	   }

	   static std::map<get_segment_manager*,dynamic_managed_mapped_file*> s_registered_mappings;
};

}  //namespace stldb

#include <boost/interprocess/detail/config_end.hpp>

#endif   //STLDB_DYNAMIC__MANAGED_MAPPED_FILE_HPP
