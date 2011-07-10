/*
 *  msync_test.cpp
 *  stldb_lib
 *
 *  Created by Bob Walters on 11/18/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>

static const int pages = 64;
static const size_t pagesize = getpagesize();
static char *addr = NULL;

using namespace std;

int main(const int argc, const char* argv[]) {
	int fd = open("mincore_testmap", O_RDWR | O_CREAT, 0777);
	assert(fd != -1);

	off_t length = pagesize*pages;
	char byte=0;
	ssize_t written = pwrite(fd, &byte, sizeof(char), length);
	assert(written != -1);

	int prot = PROT_READ|PROT_WRITE;
	//int flags = MAP_PRIVATE | MAP_NOCACHE;
	int flags = MAP_SHARED;
	addr = (char*)mmap(0, length, prot, flags, fd, 0);
	assert(addr != 0);

	cout << "Mapping at: " << (void*)addr << endl;
	close(fd);

	// modify page 0 and 1
	memset(addr, 120, pagesize*2);

	cout << "memset done, unmapping..." << endl;

	// now unmap.  Because the other process has this page mapped,
	// and locked, page 0 should stay in memory and not go to disk.
	munmap(addr,length);
}

