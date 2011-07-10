/*
 *  checkpoint_manager.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 10/30/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */
#ifndef CHECKPOINT_MANAGER_
#define CHECKPOINT_MANAGER_

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
#include <stldb/detail/checkpoint_manager_impl.h>

namespace stldb {
	
	// This is a manaed mapped file, along with a checkpoint peer file which is opened
	// with NO_CACHE, and madvise() for nearly sequential access.
	template <class managed_region_t>
	class checkpoint_manager {
	public:
		// construct a checkpoint manager over the indicated region.  This must be
		// constructed over a region
		checkpoint_manager( managed_region_t &region, const char *ckpt_fname )
		: impl(region.get_address(), region.get_size(), ckpt_fname)
		{ }
		
		// TODO - code for rvalue ref move semantics.
		//checkpoint_manager(checkpoint_manager &);
		//checkpoint_manager& operator=(checkpoint_manager &);

		// Swap two regions
		void swap(checkpoint_manager &rarg) {
			impl.swap(rarg);
		}
		
		// Resets the tracking of page modifications (mprotect) for a new checkpoint cycle
		// and copies all modified pages from the current region into the checkpoint region.
		// The mutex passed is locked briefly at the start of this process so that the checkpoint
		// thread has a short window of time when no transaction is in progress on the region,
		// not during the whole of the operation.
		void checkpoint() {
			impl.checkpoint();
		}
		
		boost::interprocess::interprocess_upgradable_mutex&  transaction_lock() {
			return impl.transaction_lock();
		}

		// grow a region
		static void grow(const char *ckpt_fname, std::size_t newsize) {
			detail::checkpoint_manager_impl::grow(ckpt_fname, newsize);
		}

		// shrink a region to fit its current contents.
		static void shrink_to_fit(const char *region_fname, const char *ckpt_fname) {
			detail::checkpoint_manager_impl::shrink_to_fit(region_fname, ckpt_fname);
		}
		
	private:
		detail::checkpoint_manager_impl impl;
	};
	
} // namespace stldb

#endif
