/*
 * region_util.h
 *
 *  Created on: Aug 27, 2009
 *      Author: rwalter3
 */

#ifndef REGION_UTIL_H_
#define REGION_UTIL_H_

#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/managed_heap_memory.hpp>
#include <boost/interprocess/managed_external_buffer.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/filesystem.hpp>

#if (defined BOOST_INTERPROCESS_WINDOWS)
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>
#endif


namespace stldb
{


// The different boost::interprocess::managed_XXX_ region types don't have exactly
// the same interfaces for operations like grow, remove, naming, etc. so this set of
// templates and their specializations createa uniform interface for classes like Database
// to work with.

/**
 * Helper class which correlates ManagedRegion type to
 * specific clean-up (remove) operations.
 */
template<class ManagedRegionType>
struct RegionRemover
{
	static void remove(const char *directory, const char *name) {
		// TODO - Not all region types have 'remove' functions, presently
	}
};

template<class C, class M, template<class IndexConfig> class I>
struct RegionRemover<boost::interprocess::basic_managed_shared_memory<C, M, I> >
{
	static void remove(const char *directory, const char *name)
	{
		try {
			STLDB_TRACE(info_e, "removing shared_memory_object " << name);
			boost::interprocess::shared_memory_object::remove(name);
		}
		catch (...) {
			// do nothing on errors. i.e. idempotent.
		}
	}
};

template<class C, class M, template<class IndexConfig> class I>
struct RegionRemover<boost::interprocess::basic_managed_mapped_file<C, M, I> >
{
	static void remove(const char *directory, const char *name)
	{
		try {
			boost::filesystem::path fullname( directory );
			fullname /= name;
			STLDB_TRACE(info_e, "removing managed_mapped_file " << fullname.string());
			boost::interprocess::file_mapping::remove(fullname.string().c_str());
		}
		catch (...) {
			// do nothing on errors. i.e. idempotent.
		}
	}
};

/**
 * Helper class used to add the database_path to the name of any memory mapped file
 * regions so that their full name is correct.  For other types of shared
 * memory regions, the database name, verbatim, is used.
 */
template<class ManagedRegionType>
struct ManagedRegionNamer {
	static std::string getFullName( const char *database_directory,
								    const char *database_name ) {
		return database_name;
	}
};

template<class C, class M, template<class IndexConfig> class I>
struct ManagedRegionNamer<boost::interprocess::basic_managed_mapped_file<C, M, I> > {
	static std::string getFullName( const char *database_directory,
								    const char *database_name ) {
		boost::filesystem::path fullpath( database_directory );
		fullpath /= database_name;
		return fullpath.string();
	}
};


/**
 * The region flush is used with memory mapped file regions immediately 
 * after process registration to ensure that the file on disk contains
 * a record of the processes presence.  Managed files have the unique
 * ability to be retained on disk but have stale information after
 * power-based failures, this being distinct from the other types which
 * are simply completely lost.  So the goal here is to ensure proper
 * recreation in all cases.
 */	 
template<class ManagedRegionType>
struct RegionSync
{
	static void flush(ManagedRegionType *region) {
		// Most region types don't require any flush operation.
	}
};

template<class C, class M, template<class IndexConfig> class I>
struct RegionSync<boost::interprocess::basic_managed_mapped_file<C, M, I> >
{
	static void flush(boost::interprocess::basic_managed_mapped_file<C, M, I> *region) {
		region->flush();
	}
};
	

/**
 * Through support of static methods like grow and shrink_to_fit, regions can be
 * resized.
 */
template<class ManagedRegionType>
struct RegionResizer {
	static ManagedRegionType* resize( ManagedRegionType *region, const char *name, std::size_t desired_size, void *fixed_mapping_addr ) {
		try {
			std::size_t region_size = region->get_size();
			if (desired_size > region_size) {
				delete region; region = NULL;
				ManagedRegionType::grow(name, desired_size - region_size);
			}
			else if (desired_size < region_size) {
				delete region; region = NULL;
				ManagedRegionType::shrink_to_fit(name);
				region = new ManagedRegionType(boost::interprocess::open_or_create,
						name, desired_size, fixed_mapping_addr);
				region_size = region->get_size();
				if (desired_size > region_size) {
					delete region; region = NULL;
					ManagedRegionType::grow(name, desired_size - region_size);
				}
			}
		}
		catch (boost::interprocess::interprocess_exception &ex) {
			STLDB_TRACE(warning_e, "Resize of region threw (non-fatal) exception " << ex.what() << ". Region size unchanged" );
		}

		// Reopen the region
		if (!region)
			region = new ManagedRegionType(boost::interprocess::open_or_create,
					name, desired_size, fixed_mapping_addr);
		return region;
	}
};

/**
 * Specialization for boost::interprocess::basic_managed_windows_shared_memory
 * which can't be resized.
 */
#if (defined BOOST_INTERPROCESS_WINDOWS)
template<class C, class M, template<class IndexConfig> class I>
struct RegionResizer<boost::interprocess::basic_managed_windows_shared_memory<C, M, I> > {
	static boost::interprocess::basic_managed_windows_shared_memory<C, M, I> *
	resize(boost::interprocess::basic_managed_windows_shared_memory<C, M, I> *region, const char *name, std::size_t desired_size, void *fixed_mapping_addr ) {
		// This region type can't be resized, so don't even try
		int64_t difference = desired_size - region->get_size();
		if (difference != 0) {
			STLDB_TRACE(warning_e, "Regions of type managed_windows_shared_memory cannot be resized. Region size unchanged" );
		}
		return region;
	}
};
#endif

} // namespace

#endif /* REGION_UTIL_H_ */
