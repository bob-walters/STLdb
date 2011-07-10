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
void readpages(int fd, bool visibility_expected_locked, bool visibility_expected_unlocked, bool seek);

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
	memset(addr,0,pagesize*pages);

	// lock page 0 into memory
	int rc = mlock(addr, pagesize);
	if (rc != 0) {
		perror("mlock");
		exit(0);
	}

	// modify page 0 and 1
	memset(addr, 127, pagesize*2);

#ifdef LINUX
	int raw = open("mincore_testmap", O_RDONLY|O_DIRECT);
	assert(fd != -1);
#else
	// open second file handle, in an attempt to see what's on disk
	int raw = open("mincore_testmap", O_RDONLY, 0777);
	assert(fd != -1);

	// attempt to use direct I/O to reread the pages, and observe their content
	cout << "Switching to F_NOCACHE mode for reads" << endl;
	rc = fcntl(raw, F_NOCACHE, 1);
	if (rc != 0) {
		perror("fcntl(F_NOCACHE)");
		exit(0);
	}
#endif
	// now attempt uncached reads of pages, we hope to see old values.
	readpages(raw, false, false, false);
	
#ifdef LINUX
	assert(!close(raw));
	raw = open("mincore_testmap", O_RDONLY);
	assert(fd != -1);
#else
	cout << "Switching to cached mode for reads" << endl;
	rc = fcntl(raw, F_NOCACHE, 0);
	if (rc != 0) {
		perror("fcntl(F_NOCACHE)");
		exit(0);
	}
#endif
	// now attempt uncached reads of pages, we hope to see old values.
	readpages(raw, true, true, true);

	// lets' do an msync
	cout << "Doing msync()" << endl;
	rc = msync(addr, pagesize*2, MS_SYNC);
	if (rc != 0) {
		perror("msync(MS_SYNC)");
		exit(0);
	}

#ifdef LINUX
	assert(!close(raw));
	raw = open("mincore_testmap", O_RDONLY);
	assert(fd != -1);
#else
	cout << "Switching to F_NOCACHE mode for reads" << endl;
	rc = fcntl(fd, F_NOCACHE, 1);
	if (rc != 0) {
		perror("fcntl(F_NOCACHE)");
		exit(0);
	}
#endif
	readpages(raw, true, true, true);
}

void readpages(int fd, bool visibility_expected_locked, bool visibility_expected_unlocked, bool seek) {

	// Seek back to the start of the file.
	if (seek) {
		int rc = lseek(fd, 0, SEEK_SET);
		if (rc != 0) {
			perror("lseek()");
			exit(0);
		}
	}
	
	// now attempt uncached reads of pages
	// buffer needs to be page aligned on Linux
	char buffer2[pagesize*2];
	char *buffer = &buffer2[0];
	buffer = (char*)(((long)(buffer + pagesize -1) / pagesize) * pagesize);
	assert(buffer >= buffer2 && buffer <= (buffer2+pagesize));

	int rc = read(fd, buffer, pagesize);
	if (rc != pagesize) {
		perror("read()");
		exit(0);
	}
	cout << ((visibility_expected_locked == (buffer[0]==127)) ? "Expected: " : "Unexpected: ");
	if (buffer[0] == 127) {
		cout << "read() sees pending modifications in locked page" << endl;
	}
	else {
		cout << "read() with F_NOCACHE doesn't see mods in locked pages" << endl;
	}
	
	rc = read(fd, buffer, pagesize);
	if (rc != pagesize) {
		perror("read()");
		exit(0);
	}
	cout << ((visibility_expected_unlocked == (buffer[0]==127)) ? "Expected: " : "Unexpected: ");
	if (buffer[0] == 127) {
		cout << "read() sees pending modifications in UNlocked page" << endl;
	}
	else {
		cout << "read() doesn't see mods in UNlocked pages" << endl;
	}
	
}
