/*
 *  mapped_checkpoint_file.h
 *  stldb_lib
 *
 *  Created by Bob Walters on 12/13/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */

#ifndef MAPPED_CHECKPOINT_FILE_H_
#define MAPPED_CHECKPOINT_FILE_H_ 1

#include <boost/intrusive/list.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/permissions.hpp>

#include <boost/filesystem.hpp>

namespace stldb {
	namespace detail {
		
	namespace ip = boost::interprocess;
	namespace fs = boost::filesystem;
		using std::size_t;
		
	class mapped_checkpoint_file : public ip::mapped_region {
	public:
		
		
		mapped_checkpoint_file(ip::open_or_create_t &ind, const char *filename, size_t filesize)
		: mapped_region()
		, fullname( filename )
		, size( filesize )
		{
			this->map_file();
		}

		mapped_checkpoint_file(ip::create_only_t &ind, const char *filename, size_t filesize)
			: mapped_region()
			, fullname( filename )
			, size( filesize )
		{
			this->map_file();
		}
		
		// construct the checkpoint file, in the indicated checkpoint directory, for the
		// indicated database, of the indicated size.
		mapped_checkpoint_file(const char *filename, size_t filesize)
			: mapped_region()
			, fullname( filename )
			, size( filesize )
		{
			this->map_file();
		}

		// grow the size of the checkpoint region
		static void grow(size_t new_database_size) {
			// TODO
		}
		
	private:
		void map_file() {
			const ip::permissions perm = ip::permissions();
			
			//This loop is very ugly, but brute force is sometimes better
			//than diplomacy. If someone knows how to open or create a
			//file and know if we have really created it or just open it
			//drop me a e-mail!
			ip::detail::file_wrapper dev;
			bool completed = false;
			while(!completed){
				try{
					ip::detail::file_wrapper tmp(ip::create_only, fullname.string().c_str(), 
												 ip::read_write, perm);
					// TODO - Set the file's size!
					tmp.truncate(size);
					dev.swap(tmp);
					completed = true;
				}
				catch(ip::interprocess_exception &ex){
					if(ex.get_error_code() != ip::already_exists_error){
						throw;
					}
					else{
						try{
							ip::detail::file_wrapper tmp(ip::open_only, fullname.string().c_str(), 
														 ip::read_write);
							dev.swap(tmp);
							completed = true;
						}
						catch(ip::interprocess_exception &ex){
							if(ex.get_error_code() != ip::not_found_error){
								throw;
							}
						}
					}
				}
				ip::detail::thread_yield();
			}
			ip::mapped_region temp(dev, ip::read_write, 0, 0);
			this->swap(temp);
		}
		
		fs::path fullname;
		size_t size;
	};


	}; // namespace detail
}; // namespace stldb

#endif
