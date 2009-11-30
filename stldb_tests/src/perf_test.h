//============================================================================
// Name        : stldb-tests.cpp
// Author      : Bob Walters
// Version     :
// Copyright   : Copyright 2009 by Bob Walters
// Description :
//============================================================================

#ifndef STLDB_TEST_PERF_FRAMEWORK_H_
#define STLDB_TEST_PERF_FRAMEWORK_H_

/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */
#include <cstdlib> // rand()

#include "test_database.h"

#include <stldb/timing/timer.h>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/thread.hpp>

using stldb::Transaction;
using stldb::timer;
using boost::interprocess::scoped_lock;
using boost::interprocess::sharable_lock;

// Operation - perform on transaction of some sort against the map.
// The operation performed is one test loop iteration
class trans_operation
{
public:
	virtual void operator()()=0;
	virtual ~trans_operation() { }
	virtual void print_totals() { }
};


// A callable (per boost::thread concept) which has the executed thread
// perform a series of loop_size operations from among the set provided via
// add functions.  operations to be executed are chosen randomly from among
// the set passed.
class test_loop {
public:
	test_loop(int loop_size)
		: loop_size(loop_size)
		, _ops(), _max_freq(0)
		{ }

	// Adds a trans_operation to the set of ops being performed, which will
	// be executed with the specified frequency.
	void add(trans_operation* op, int frequency ) {
		_max_freq += frequency;
		_ops.insert( std::make_pair<const int,trans_operation*>( _max_freq, op ) );
	}

	// Run through loop_size randomly
	void operator()() {
		if (_ops.size() == 1) {
			// fast loop
			trans_operation &op = *(_ops[_max_freq]);
			for (int i=0; i<loop_size; i++) {
				op();
			}
		}
		else {
			// slower loop, randomly pick work to do.
			for (int i=0; i<loop_size; i++) {
				// randomly pick one operation, and execute it.
				int r = static_cast<int>(::rand() * (((double)_max_freq) / (double)RAND_MAX));
				(*_ops.lower_bound(r)->second)();
			}
		}
	}

	void print_totals() {
		for (std::map<int,trans_operation*>::iterator i = _ops.begin();
			 i != _ops.end(); i++)
		{
			i->second->print_totals();
		}
	}
private:
	int loop_size;
	std::map<int,trans_operation*> _ops;
	int _max_freq;
};


static void performance_test(int thread_cnt, test_loop &callable)
{
	boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

	// Start the threads which are going to run operations:
	boost::thread **workers = new boost::thread *[thread_cnt];
	for (int i=0; i<thread_cnt; i++) {
		workers[i] = new boost::thread( callable );
	}

	// now await their completion
	for (int i=0; i<thread_cnt; i++) {
		workers[i]->join();
		delete workers[i];
	}
	boost::posix_time::ptime end = boost::posix_time::microsec_clock::local_time();

	boost::posix_time::time_duration duration = end - start;
	cout << "performance_test duration: " << duration << std::endl;

	callable.print_totals();
}


#endif
