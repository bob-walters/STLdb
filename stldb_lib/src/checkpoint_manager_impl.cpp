/*
 *  checkpoint_manager_impl.cpp
 *  stldb_lib
 *
 *  Created by Bob Walters on 10/30/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */
#include <boost/interprocess/managed_mapped_file.hpp>
#include <stldb/detail/checkpoint_manager_impl.h>
#include <signal.h>
#include <stldb/trace.h>
#include <boost/interprocess/sync/sharable_lock.hpp>

namespace stldb {
	namespace detail {
		
	namespace ip = boost::interprocess;
	namespace fs = boost::filesystem;
	
	size_t checkpoint_manager_impl::pagesize = ::getpagesize();

	// saved signal handler (action) detected during install_signal_handler
	struct ::sigaction checkpoint_manager_impl::oldaction_bus;
	struct ::sigaction checkpoint_manager_impl::oldaction_segv;

	// installs the SIGSEGV/SIGBUS signal handler to detect writes on read-only pages
	void checkpoint_manager_impl::install_signal_handler() {
		struct ::sigaction newactions;
		newactions.sa_sigaction = page_write_handler;
		newactions.sa_flags = SA_SIGINFO;
		sigemptyset(&newactions.sa_mask);
		
		if (::sigaction(SIGBUS, &newactions, &oldaction_bus) == -1)
			perror("Could not install new signal handler");
		if (::sigaction(SIGSEGV, &newactions, &oldaction_segv) == -1)
			perror("Could not install new signal handler");
	};
	
	// remove signal handler installed by install_signal_handler
	void checkpoint_manager_impl::remove_sighandler() {
		if (::sigaction(SIGBUS, &oldaction_bus, NULL) == -1)
			perror("Could not revert to original signal handler");
		if (::sigaction(SIGSEGV, &oldaction_segv, NULL) == -1)
			perror("Could not revert to original signal handler");
	};

	// flag to track call to install_signal_handler
	boost::once_flag checkpoint_manager_impl::called_flag;
	checkpoint_manager_impl::upgradable_mutex_t checkpoint_manager_impl::regions_mutex;
	
	// regions is keyed off the last byte address of the region
	// this must exist within process heap
	std::map<char*, checkpoint_manager_impl*> checkpoint_manager_impl::regions;
	
	// handles a write to a read-only page in a checkpointable_managed_mapped_region
	// by recording the page modification.  Additional work may be required if a pending
	// checkpoint is in progress.
	void checkpoint_manager_impl::page_write_handler (int signo, ::siginfo_t *info,
																 void *context) 
	{
		STLDB_TRACE_DEBUG(finest_e, "entering page_write_handler()");

		stldb::timer t("checkpoint_manager_impl::page_write_handler()");

		static ptrdiff_t pagemask = (ptrdiff_t)(-1) ^ (ptrdiff_t)(pagesize-1);

		// begin by determining which instance of checkpoint_manager_impl has been
		// modified.  Probably should offer an optimization to supress this code for
		// applications which only open one instance.
		checkpoint_manager_impl *ckpt = NULL;
		{
			ip::sharable_lock<upgradable_mutex_t> lock(regions_mutex);
			void *fault_addr = info->si_addr;
			STLDB_TRACE_DEBUG(finest_e, "signal " << signo << " caught at: " << fault_addr);

			std::map<char*, checkpoint_manager_impl*>::iterator i 
				= regions.lower_bound((char*)fault_addr);
			if (i == regions.end() || i->second->region_base_addr > fault_addr )
			{
				STLDB_TRACE(warning_e, "signal " << signo << " caught at: " << fault_addr
							<< ", is determined to be outside of any region.  Passing signal along to the previous handler or OS");
				
				// For debugging...
				/*
				const char *val = getenv("SEGV_DEBUG");
				if (val != NULL && strlen(val) != 0) {
					for (int z=0; z<1000000; z++) {
						STLDB_TRACE(info_e, "pausing thread for debuging opportunity");
						sleep(1);
					}
				}
				*/

				// This is a real fault.  Not related to checkpoint protections,
				// but to some other region of memory
				if (signo == SIGSEGV && oldaction_segv.sa_sigaction != NULL) {
					// if there was a previously installed hanlder, let it handle it
					oldaction_segv.sa_sigaction(signo, info, context);
				}
				else if (signo == SIGBUS && oldaction_bus.sa_sigaction != NULL) {
					// if there was a previously installed hanlder, let it handle it
					oldaction_bus.sa_sigaction(signo, info, context);
				}
				else {
					// otherwise remove this handler to allow the signal to occur
					// after we return.  The instruction which causes the memory fault
					// will be re-executed, but that time it will not be sent to this function
					remove_sighandler();
				}
				return;
			}
			ckpt = i->second;
		}
		
		// track the page modification
		void* pageaddr = (void*)((ptrdiff_t)info->si_addr & pagemask);
		pageno_t pageno = ckpt->pageno(pageaddr);

		checkpoint_info_t *ckpt_info = ckpt->ckpt_info;
		
		region_change_info_t *ckpt_inprog = &* ckpt_info->change_info_inprog;
		// unprotected read, will double check this within
		if (ckpt_inprog->modified_bits[pageno] == true)
		{
			ip::scoped_lock<mutex_t> lock(ckpt_inprog->mutex[pageno % region_change_info_t::MUTEX_COUNT]);

			// double checked locking pattern
			if (ckpt_inprog->modified_bits[pageno] == true) {
				STLDB_TRACE_DEBUG(finer_e, "modification to modified page at " << pageaddr << " (pageno " << pageno << " ) triggering on demand checkpoint of page since checkpoint is in progress");

				// copy the pending checkpoint page into ckpt now, before
				// making any further changes to it.
				ip::scoped_lock<mutex_t> ckptlock(ckpt_info->mutex);
				pageno_t ckpt_pageno = ckpt_info->allocate(pageno);
				ckptlock.unlock();

				ckpt->copy_to_checkpoint(pageno, ckpt_pageno);
				ckpt_inprog->modified_bits[pageno] = false;
				ckpt_inprog->demand_written_pages_count ++;
			}
		}

		region_change_info_t *change_info = &* ckpt_info->change_info;
		{
			ip::scoped_lock<mutex_t> lock(change_info->mutex[pageno % region_change_info_t::MUTEX_COUNT]);
			
			// this check is needed to address the race of entering this handler for the same page.
			// in most cases, this will be the case
			if (change_info->modified_bits[pageno] == false) {
				
				STLDB_TRACE_DEBUG(finest_e, "clearing protection on modified page at " << pageaddr << " (pageno " << pageno << " )");

				// mark the page as modified
				change_info->modified_bits[pageno] = true;
				change_info->modified_pages[change_info->modified_pages_count++] = pageno;
			}
		}

		// at this point, we always remove the protection from this page.
		// The reason this is always done is because modified_bits[pageno] might be true
		// because of another process tracking a change, in which case this processes
		// memory protection for that page still needs to be lifted.  It's also possible
		// that the page was marked dirty prior to the last close(), but the entire
		// region protected (read-only) when we connected.

		// This can result in multiple calls to mrpotect for the same page when multiple
		// threads enter this code for the same page simultaneously.  Future optimization
		// can eliminate that by having a secondary local (heap) bit vector tracking.

		int rc = mprotect(pageaddr, pagesize, PROT_READ|PROT_WRITE);
		if (rc != 0) {
			perror("mprotect(read|write)");
			exit(0);
		}
	}
	
	// Given a checkpoint filename, return the name to use for the associated support managed region.
	// which resides in the same directory 
	static std::string ckpt_name_to_metafile_name(const char *ckpt_name) {
		fs::path ckpt_path( ckpt_name );
		std::string file_part( ckpt_path.stem() );
		file_part += ".meta.current";
		return ( ckpt_path.parent_path() / file_part ).string();
	}
	
	
	// construct or open the checkpoint region and associated metafile
	checkpoint_manager_impl::checkpoint_manager_impl(void *region_addr, 
													 std::size_t region_sz,
													 const char *ckpt_name)
		: region_base_addr(reinterpret_cast<char*>(region_addr))
		, region_size(region_sz)
		, ckpt_region(ckpt_name, region_size*2)
		, support_region(ip::open_or_create, ckpt_name_to_metafile_name(ckpt_name).c_str(), support_region_size_est(region_size) )
		, ckpt_info(NULL)
	{
		// find/or create ckpt_info.  The checkpoint region has twice as many pages
		// as the standard region.
		void_alloc_t alloc( support_region.get_segment_manager() );
		ckpt_info = support_region.find_or_construct<checkpoint_info_t>
					(ip::unique_instance)(alloc,(region_size*2/pagesize),&support_region);

		// set up the signal handler if not already done
		boost::call_once(install_signal_handler, called_flag);	

		// register this region with that signal handler.
		ip::scoped_lock<upgradable_mutex_t> lock(regions_mutex);
		regions[region_base_addr + region_size] = this;

		// for performance testing comparisons
		const char *val = getenv("NO_SIGNAL_HANDLING");
		if (val == NULL || strlen(val) == 0) {
			// set the protection of all pages in main region to read-only to commence tracking.
			int rc = mprotect(region_base_addr, region_size, PROT_READ);
			if (rc != 0) {
				perror("mprotect(read_only)");
				exit(0);
			}
		}
	}
	
	checkpoint_manager_impl::~checkpoint_manager_impl() {
		const char *val = getenv("NO_SIGNAL_HANDLING");
		if (val == NULL || strlen(val) == 0) {
			// set the protection of all pages in main region to read-only to commence tracking.
			int rc = mprotect(region_base_addr, region_size, PROT_READ|PROT_WRITE|PROT_EXEC);
			if (rc != 0) {
				perror("mprotect(read|write|execute)");
				exit(0);
			}
		}

		ip::scoped_lock<upgradable_mutex_t> lock(regions_mutex);
		regions.erase(region_base_addr + region_size);

		// destructor for ckpt_region and support_region will be called, closing them.
		// The signal handler is not currently removed.
	}

	// swap
	void checkpoint_manager_impl::swap(checkpoint_manager_impl &rarg) {
		std::swap(region_base_addr, rarg.region_base_addr);
		std::swap(region_size, rarg.region_size);
		ckpt_region.swap(rarg.ckpt_region);
		support_region.swap(rarg.support_region);
		std::swap(ckpt_info, rarg.ckpt_info);
	}

	// checkpoint a region.
	//template <class Mutex_t>
	bool checkpoint_manager_impl::checkpoint()
	{
		stldb::timer t("checkpoint_manager_impl::checkpoint()");
		STLDB_TRACE(info_e, "Starting checkpoint");

		{   //lock scope for the transactional mutex
			stldb::timer lock_time("checkpoint_manager_impl::checkpoint().get_transaction_lock()");
			ip::scoped_lock<ip::interprocess_upgradable_mutex> lock( this->transaction_lock() );
			lock_time.end();

			BOOST_VERIFY(ckpt_info->change_info_inprog->modified_pages_count==0);

			// Save change_info in change_info_inprog, and reset it to empty by swapping pointers.
			std::swap(ckpt_info->change_info_inprog,ckpt_info->change_info);

			// reset the protection of all pages in main region to read-only to resume tracking.
			stldb::timer protect_time("checkpoint_manager_impl::checkpoint::mprotect(region,READ_ONLY)");
			STLDB_TRACE_DEBUG(finer_e, "Resetting protection on region to READ_ONLY");

			int rc = mprotect(region_base_addr, region_size, PROT_READ);
			if (rc != 0) {
				perror("mprotect(read_only)");
				exit(0);
			}
			STLDB_TRACE_DEBUG(finer_e, "Done resetting protection on region to READ_ONLY");
		} // end of lock scope

		region_change_info_t *change_info_inprog = &*(ckpt_info->change_info_inprog);
		int modified_pages = change_info_inprog->modified_pages_count;
		STLDB_TRACE(info_e, "Starting checkpoint of " << modified_pages << " pages");
		for (int entry_no = 0; entry_no < modified_pages; entry_no++)
		{
			pageno_t pageno = change_info_inprog->modified_pages[entry_no];
			ip::scoped_lock<mutex_t> lock(change_info_inprog->mutex[pageno % region_change_info_t::MUTEX_COUNT]);
			if (change_info_inprog->modified_bits[pageno]) {
				ip::scoped_lock<mutex_t> ckptlock(ckpt_info->mutex);
				pageno_t ckpt_pageno = ckpt_info->allocate(pageno);
				ckptlock.unlock();
				this->copy_to_checkpoint(pageno, ckpt_pageno);
				change_info_inprog->modified_bits[pageno] = false;
				change_info_inprog->ckpt_written_pages_count ++;
			}
		}

		STLDB_TRACE(fine_e, "modified_pages_count of inprog region == " << change_info_inprog->modified_pages_count);
		STLDB_TRACE(fine_e, "ckpt_written_pages_count == " << change_info_inprog->ckpt_written_pages_count);
		STLDB_TRACE(fine_e, "demand_written_pages_count == " << change_info_inprog->demand_written_pages_count);
		BOOST_VERIFY(change_info_inprog->modified_pages_count ==
						change_info_inprog->ckpt_written_pages_count +
						change_info_inprog->demand_written_pages_count);

		// Once all pages have been copied out to free portions of the ckpt_region,
		// msync the checkpoint file,
		ckpt_region.flush();

		// pending free pages deallocated during this now become usable.
		ckpt_info->complete_checkpoint();

		change_info_inprog->modified_pages_count = 0;
		change_info_inprog->ckpt_written_pages_count = 0;
		change_info_inprog->demand_written_pages_count = 0;

		// TODO - then write out the new metafile as a dump of the ckpt_info (but not the
		// contained change_info regions, which would be useless during a restore).
		STLDB_TRACE(info_e, "Checkpoint Complete");
		return true;
	}

	}; // namespace detail
}; // namespace stldb

