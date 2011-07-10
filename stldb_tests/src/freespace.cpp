/*
 *  freespace.cpp
 *  stldb_lib_r12
 *
 *  Created by Bob Walters on 7/25/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#include <stldb/detail/freespace.h>

typedef freelist::fileloc fileloc;
int main(int argc, char *argv[])
{
	freelist fl;

	freelist.allocate(4);
	fileloc result1[] = { {0, 256*1024*1024-4} };
	BOOST_ASSERT( fl.compare( result1 ) );
}


