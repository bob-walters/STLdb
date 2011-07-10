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
	int fd = open("mincore_testmap", O_RDONLY);
	assert(fd != -1);

	off_t length = pagesize*pages;

	int prot = PROT_READ|PROT_WRITE;
	//int flags = MAP_PRIVATE | MAP_NOCACHE;
	int flags = MAP_SHARED;
	addr = (char*)mmap(0, length, prot, flags, fd, 0);
	assert(addr != 0);

	cout << "Mapping at: " << (void*)addr << endl;

	// now attempt uncached reads of pages, we hope to see old values.
	readpages(fd, false, false, false);
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
	cout << ((visibility_expected_locked == (buffer[0]>=120)) ? "Expected: " : "Unexpected: ");
	if (buffer[0] >= 120) {
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
	cout << ((visibility_expected_unlocked == (buffer[0]>=120)) ? "Expected: " : "Unexpected: ");
	if (buffer[0] >= 120) {
		cout << "read() sees pending modifications in UNlocked page" << endl;
	}
	else {
		cout << "read() doesn't see mods in UNlocked pages" << endl;
	}
	
}
