/*
 * ckpt_reader.cpp
 *
 *  Created on: Jan 18, 2010
 *      Author: bobw
 */

#include <stldb/checkpoint.h>
#include <stldb/detail/db_file_util.h>

int main(int argc, const char *argv[])
{
	// write the metafile initially to a temp filename.
	boost::filesystem::path tempfilepath( argv[1] );
	stldb::checkpoint_file_info meta;

	stldb::detail::get_checkpoint_file_info( tempfilepath, meta );

	std::cout << meta << std::endl;
	return 0;
}

