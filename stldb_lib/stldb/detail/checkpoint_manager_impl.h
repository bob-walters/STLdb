/*
 *  checkpoint_manager_impl.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 12/23/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */
#ifndef CHECKPOINT_MANAGER_IMPL_
#define CHECKPOINT_MANAGER_IMPL_

#include <signal.h>

#include <boost/thread.hpp>
#include <boost/intrusive/list.hpp>

#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/node_allocator.hpp>

#include <stldb/sync/bounded_interprocess_mutex.h>
#include <stldb/detail/mapped_checkpoint_file.h>
#include <stldb/trace.h>
#include <stldb/timing/timer.h>
#include <stldb/sync/lockfree/atomic_int.hpp>


namespace stldb {
	namespace detail {
		
	namespace ip = boost::interprocess;
	namespace intrusive = boost::intrusive;
	namespace lockfree = boost::lockfree;
	
	typedef uint64_t pageno_t;
	static const pageno_t NULL_PAGE = (pageno_t)-1;
	
	// This structure represents the changes which have occurred within a region since the start of
	// some checkpoint.  Between checkpoints, only one of these is needed.  When a checkpoint is in
	// progress, 2 are needed - one for all changes which occurred up to the moment the checkpoint
	// began, and one for all changes that have occurred since that.
	template<class void_allocator_t, class mutex_t>
	struct region_change_info {
		
		static const int MUTEX_COUNT = 111;

		//! This region needs a mutex to protect its contents from concurrent modification.
		mutex_t mutex[MUTEX_COUNT];
		
		//! vector that keeps track of pages modified since last checkpoint.
		ip::vector<bool, typename void_allocator_t::template rebind<bool>::other > modified_bits;
		
		// modified pages since last checkpoint, in order of modification time
		ip::vector<pageno_t, typename void_allocator_t::template rebind<pageno_t>::other > modified_pages;
		
		//! number of pages modified since last checkpoint
		lockfree::atomic_int<size_t> modified_pages_count;
		
		//! Statistics
		size_t ckpt_written_pages_count;
		lockfree::atomic_int<size_t> demand_written_pages_count;

		// construct an instance
		region_change_info(const void_allocator_t &alloc, size_t region_page_count)
			: mutex()
			, modified_bits(alloc)
			, modified_pages(alloc)
			, modified_pages_count(0)
			, ckpt_written_pages_count(0)
			, demand_written_pages_count(0)
		{
			this->grow(region_page_count);
		}
		
		// grow the size of an instance (for use when a region is grown)
		void grow( size_t new_region_page_count ) {
			modified_bits.resize(new_region_page_count, false);
			modified_pages.resize(new_region_page_count, NULL_PAGE);
			STLDB_TRACE(finest_e, "modified_bits at " << (void*)this << ", length: " << modified_bits.size());
		}
		
		// compute a reasonable maximum size for the data associated with this type, based on
		// the # of pages in a region.
		static inline size_t size_estimate( size_t region_page_count ) {
			return size_t(sizeof(region_change_info<void_allocator_t, mutex_t>)
						  + (region_page_count / 8 )  // modified_bits
						  + (region_page_count * sizeof(pageno_t)) // modified_pages
						  );
		}
		
	};
	
	// Info about the current contents of a checkpoint file.  i.e. Which pages in the checkpoint file
	// are free, and the mapping of pages in the managed region to pages in the checkpoint file.
	template<class void_allocator_t, class mutex_t>
	struct checkpoint_info {
		
		//! This region needs a mutex to protect its contents from concurrent modification.
		mutex_t mutex;
		
		//! lock for coordinating regular vs exclusive transactions
		bounded_interprocess_upgradable_mutex  transaction_lock;

		// The location of the region's pages in the checkpoint file.  
		// i.e. page_locs[managed_file_pageno] = ckpt_pagno  | NULL_PAGE
		ip::vector<pageno_t, typename void_allocator_t::template rebind<pageno_t>::other > page_locs;
		
		// The contents of the checkpoint file.
		// i.e. ckpt_contents[ckpt_pageno] == managed_file_pageno | NULL_PAGE
		ip::vector<pageno_t, typename void_allocator_t::template rebind<pageno_t>::other > ckpt_content;
		
		typedef ip::node_allocator<pageno_t, typename void_allocator_t::segment_manager> set_node_allocator_t;
		typedef ip::set<pageno_t, std::less<pageno_t>, set_node_allocator_t> set_of_pages_t;
		
		// The set of free pages.  This is a set rather than a list in order to permit best placement
		// of new page with the intention of optimize loading time, when a checkpoint file is sequentially
		// read to reconstruct 
		set_of_pages_t free_pages;
		
		// The set of pending free pages - pages freed up during a checkpoint  This can't actually
		// become part of free_pages until the checkpoint is completed.
		set_of_pages_t pending_free_pages;
		
		typedef region_change_info<void_allocator_t,mutex_t>  region_change_info_t;
		typedef typename void_allocator_t::template rebind<region_change_info_t>::other::pointer region_change_info_ptr;

		// modified page info - indicates pages modified since the last checkpoint began.
		region_change_info_ptr change_info;
		
		// the previous set of modified pages, up to the moment when an inprogress checkpoint began.
		region_change_info_ptr change_info_inprog;
		
		// construct an instance
		template <class managed_region_t>
		checkpoint_info(const void_allocator_t &alloc, size_t ckpt_page_count,
						managed_region_t *support_region)
			: mutex()
			, transaction_lock()
			, page_locs(alloc)
			, ckpt_content(alloc)
			, free_pages(std::less<pageno_t>(), set_node_allocator_t(alloc.get_segment_manager()))
			, pending_free_pages(std::less<pageno_t>(), set_node_allocator_t(alloc.get_segment_manager()))
			, change_info(NULL), change_info_inprog(NULL)
		{ 
			page_locs.resize(ckpt_page_count, NULL_PAGE);
			ckpt_content.resize(ckpt_page_count, NULL_PAGE);
			for (pageno_t i=0; i<ckpt_page_count; i++) {
				free_pages.insert( free_pages.end(), i);
			}
			// the ckpt_page_count is double the size of the region.  Thus the /2 below.
			change_info = support_region->template find_or_construct<region_change_info_t>
				("change_info")(alloc, ckpt_page_count/2);
			change_info_inprog = support_region->template find_or_construct<region_change_info_t>
				("change_info_inprog")(alloc, ckpt_page_count/2);
		}
		
		// return an estimate of the size of this object, including all dynamically allocated
		// content.
		static inline size_t size_estimate(size_t ckpt_page_count) {
			return size_t( sizeof(checkpoint_info<void_allocator_t,mutex_t>) 
						  + sizeof(pageno_t) * ckpt_page_count // page_locs
						  + sizeof(pageno_t) * ckpt_page_count // ckpt_content
						  + (sizeof(pageno_t) + sizeof(typename void_allocator_t::pointer)*2) * ckpt_page_count // free_pages
						  + region_change_info_t::size_estimate(ckpt_page_count/2) * 2
						  );
		}
		
		// grow the size of an instance (for use when a region is grown)
		void grow( size_t new_ckpt_page_count ) {
			size_t current_ckpt_page_count = page_locs.size();
			page_locs.resize(new_ckpt_page_count, NULL_PAGE);
			ckpt_content.resize(new_ckpt_page_count, NULL_PAGE);
			for (pageno_t i=current_ckpt_page_count; i<new_ckpt_page_count; i++) {
				free_pages.insert( free_pages.end(), i);
			}
			change_info->grow(new_ckpt_page_count/2);
			change_info_inprog->grow(new_ckpt_page_count/2);
		}
		
		// allocate a new page for a modified age of the managed region.  Return
		// the checkpoint page which has been allocated.
		pageno_t allocate(pageno_t region_pageno) {
			//ip::scoped_lock<mutex_t> lock(mutex);
			
			pageno_t current = page_locs[region_pageno];
			if (current != NULL_PAGE) {
				pending_free_pages.insert( current );
				ckpt_content[current] = NULL_PAGE;
			}
			// allocate the free page in the checkpoint closest to region_pageno
			typename set_of_pages_t::iterator i = free_pages.lower_bound(region_pageno);
			if (i != free_pages.end()) {
				pageno_t diff = *i - region_pageno;
				if (diff > 0 && i != free_pages.begin()) {
					i--;
					if (region_pageno - *i > diff)
						i++;
				}
			}
			else {
				i--;
			}
			// i now points to the entry in free_pages which we want to return.
			pageno_t newloc = *i;
			ckpt_content[newloc] = region_pageno;
			page_locs[region_pageno] = newloc;
			free_pages.erase(i);
			return newloc;
		}
		
		// complete a checkpoint - pending_free_pages can now be merged into free_pages.
		void complete_checkpoint() {
			ip::scoped_lock<mutex_t> lock(mutex);
			
			for (typename set_of_pages_t::iterator i = pending_free_pages.begin(); 
				 i != pending_free_pages.end(); i++)
			{
				free_pages.insert(*i);
			}	
			pending_free_pages.clear();
		}
	};
	
	
	// This is a managed mapped file, along with a checkpoint peer file which is opened
	// with NO_CACHE, and madvise() for nearly sequential access.
	class checkpoint_manager_impl {
	public:
		checkpoint_manager_impl( void *region_addr, size_t region_size, const char *ckpt_fname );
		
		~checkpoint_manager_impl();

		// TODO - code for rvalue ref move semantics.
		//checkpoint_manager_impl(checkpoint_manager_impl &);
		//checkpoint_manager_impl& operator=(checkpoint_manager_impl &);
		
		// Swap two regions
		void swap(checkpoint_manager_impl &) ;
		
		// Resets the tracking of page modifications (mprotect) for a new checkpoint cycle
		// and copies all modified pages from the current region into the checkpoint region.
		// The mutex passed is locked briefly at the start of this process so that the checkpoint
		// thread has a short window of time when no transaction is in progress on the region,
		// not during the whole of the operation.
		bool checkpoint();

		// return the upgradable_mutex in the ckpt_info structure
		ip::interprocess_upgradable_mutex&  transaction_lock() {
			return ckpt_info->transaction_lock;
		}
		
		// grow a region
		static bool grow(const char *, std::size_t);
		
		// shrink a region to fit its current contents.
		static bool shrink_to_fit(const char *region_fname, const char *ckpt_fname);

	private:
		typedef bounded_interprocess_mutex 				mutex_t;
		typedef bounded_interprocess_upgradable_mutex	upgradable_mutex_t;

		typedef ip::allocator<void,ip::managed_mapped_file::segment_manager> void_alloc_t;
		typedef checkpoint_info<void_alloc_t,mutex_t> checkpoint_info_t;
		typedef region_change_info<void_alloc_t,mutex_t> region_change_info_t;
		
		// the region we are checkpointing
		char *region_base_addr;
		size_t region_size;
		
		// the checkpoint region, which must be sized to be twice as many pages as the size
		// of the region, in order to permit the leapfrogging use of free space.  The file can
		// be sparse however, and NO_CACHE based on I/O patterns, so the only real demand is on
		// the virtual address space of the process.
		detail::mapped_checkpoint_file  ckpt_region;
		
		// A support region, which contains the current copy of checkoint_info.  At the conclusion
		// of each checkpoint, this region is persisted.
		ip::managed_mapped_file  support_region;
		
		checkpoint_info_t        *ckpt_info;	// in support region
		
		static size_t pagesize;
		
		// posix signal handler function called when protected pages are modified.
		// permits the tracking of modifications.
		static void page_write_handler (int signo, ::siginfo_t *info, void *context);
		static struct ::sigaction oldaction_segv;
		static struct ::sigaction oldaction_bus;
		static void install_signal_handler();
		static void remove_sighandler();		
		static boost::once_flag called_flag;
		
		// checkpoints is keyed off the last byte address of the region
		static std::map<char*, checkpoint_manager_impl*> regions;
		static upgradable_mutex_t  regions_mutex;  // guards the above

		// return the page number of the address within this region
		inline pageno_t pageno(void* address) {
			size_t offset = (size_t)((char*)address - (char*)region_base_addr);
			BOOST_VERIFY((char*)address >= (char*)region_base_addr);
			BOOST_VERIFY((char*)address < (char*)region_base_addr + region_size);
			return offset / pagesize;
		}
		
		// return the page range of the region from address to address+size bytes.
		inline std::pair<pageno_t,pageno_t> pagerange(void*address, size_t size) {
			return std::make_pair( pageno(address), pageno(((char*)address)+size) );
		}
		
		// copy a page from the managed region to a page in the ckpt_region.
		void copy_to_checkpoint(pageno_t region_pageno, pageno_t ckpt_pageno) {
			char *source_addr = reinterpret_cast<char*>(region_base_addr);
			char *dest_addr = reinterpret_cast<char*>(ckpt_region.get_address());
			source_addr += (pagesize * region_pageno);
			dest_addr += (pagesize * ckpt_pageno);
			memcpy( dest_addr, source_addr, pagesize );
		}
		
		// Given a database size, determine how big the support region should be.
		static size_t support_region_size_est(size_t size) {
			size_t region_page_count = size / pagesize;
			// checkpoint size is twice region size (maximum)
			size_t raw_size_est = checkpoint_info_t::size_estimate(region_page_count*2);
			raw_size_est *= 1.5; // safety buffer
			raw_size_est = ((raw_size_est / pagesize )+1 ) *pagesize; // round to pagesize
			return raw_size_est;
		}
	};
	
	
	} // namespace detail
} // namespace stldb

#endif
