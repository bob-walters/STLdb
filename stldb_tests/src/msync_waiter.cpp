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
#ifdef LINUX
#include <sys/capability.h>
#endif

static const int pages = 64;
static const size_t pagesize = getpagesize();
static char *addr = NULL;

using namespace std;

// Make sure, on Linux, that the process has the capability to lock memory.
// See man capability(7)
void print_mlock_capability() {
#ifdef LINUX
	cap_t caps = cap_get_proc();
	if (caps == NULL) {
		perror("cap_get_proc()");
		exit(0);
	}

	cap_flag_value_t value;
	cap_get_flag(caps, CAP_IPC_LOCK, CAP_PERMITTED, &value);
	cout << "IPC_LOCK Permitted: " << (value == CAP_SET ? "set" : "clear") << endl;
	cap_get_flag(caps, CAP_IPC_LOCK, CAP_EFFECTIVE, &value);
	cout << "IPC_LOCK Effective: " << (value == CAP_SET ? "set" : "clear") << endl;
#endif
}

int main(const int argc, const char* argv[]) {
	print_mlock_capability();

	int fd = open("mincore_testmap", O_RDWR | O_CREAT, 0777);
	assert(fd != -1);

	off_t length = pagesize*pages;
	char byte=0;
	ssize_t written = pwrite(fd, &byte, sizeof(char), length);
	assert(written != -1);

	int prot = PROT_READ|PROT_WRITE;
	//int flags = MAP_PRIVATE | MAP_NOCACHE;
	//int flags = MAP_SHARED;
	int flags = MAP_SHARED | MAP_NORESERVE;
	addr = (char*)mmap(0, length, prot, flags, fd, 0);
	assert(addr != 0);

	cout << "Mapping at: " << (void*)addr << endl;
	memset(addr,0,pagesize*pages);

	msync(addr,length,MS_SYNC);

	if (argc>1) {
		cout << "Locking page 0 in memory" << endl;
		// lock page 0 into memory
		int rc = mlock(addr, pagesize);
		if (rc != 0) {
			perror("mlock");
			exit(0);
		}
	}

	// modify page 0 and 1
	memset(addr, 127, pagesize*2);

	// now wait 
	cout << "Page 0 and 1 modified, but not synced.  Waiting...." << endl;
	sleep( 300 );
	cout << "Asleep for 5, sleeping for another 5..." << endl;
	sleep( 300 );
}

