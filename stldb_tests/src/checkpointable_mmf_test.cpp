/*
 *  checkpointable_mmf_test.cpp
 *  stldb_lib
 *
 *  Created by Bob Walters on 11/13/10.
 *  Copyright 2010 bobw. All rights reserved.
 *
 */

#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>

#include <stldb/checkpoint_manager.h>
#include <stldb/timing/timer.h>
#include <stldb/trace.h>

#include "properties.h"

using stldb::checkpointable_managed_mapped_file;
namespace ip = boost::interprocess;

// globals used out of laziness.  Values shown here are overriden by properties at startup
ip::managed_mapped_file *g_region;
checkpoint_manager *g_manager;

ip::interprocess_upgradable_mutex trans_mutex;

char *region_base;

size_t g_map_node_size = 48;
size_t g_avg_key_length = 64;
size_t g_avg_val_length = 512;

int g_ops_per_txn = 10;
long g_region_size = 256*1024*1024;
long g_map_region_size;
long g_key_region_size; 
long g_val_region_size;


// generate a random node <address,size>
std::pair<void *,size_t> random_map_node() {
	return std::make_pair(region_base + static_cast<int64_t>(
			(((::rand()/(double)RAND_MAX) * g_map_region_size) / g_map_node_size ) * g_map_node_size)
			, g_map_node_size);
}

// generate a random key <address,size>
std::pair<void *,size_t> random_map_key() {
	size_t keysize = static_cast<size_t>(::rand()/ (double)RAND_MAX) * (((double)g_avg_key_length) ) + (g_avg_key_length/2);
	return std::make_pair(region_base + g_map_region_size + static_cast<int64_t>((::rand()/(double)RAND_MAX) * (g_key_region_size-keysize)),
						  keysize);
}

// generate a random entry value <address,size>
std::pair<void *,size_t> random_map_value() {
	size_t valsize = static_cast<size_t>(::rand() * (((double)g_avg_val_length) / (double)RAND_MAX)) + (g_avg_val_length/2);
	return std::make_pair(region_base + g_map_region_size + g_key_region_size + static_cast<int64_t>((::rand()/(double)RAND_MAX) * (g_val_region_size - valsize)) ,
						  valsize);		  
}


// Operation - perform an operation of some sort against the managed region.
// The operation performed is one test loop iteration
class operation
{
public:
	virtual void operator()()=0;
	virtual ~operation() { }
};

// One of the possible transactional operations which is invoked.
// Simulates the page locking associated with updating ops_per_txn entries
class SimulatedUpdateTransaction : public operation
{
	std::pair<void*,size_t> *locks;
	
public:
	SimulatedUpdateTransaction()
		: locks( new std::pair<void*,size_t>[g_ops_per_txn*2] )
	{ }

	virtual ~SimulatedUpdateTransaction() {
		delete [] locks;
	}

	virtual void operator()() {
		stldb::timer t("SimulatedUpdateTransaction");

		ip::sharable_lock<ip::interprocess_upgradable_mutex> lock(trans_mutex);
		
		// sequence of update calls.  lock the node, allocate the value
		for (int i=0; i<g_ops_per_txn; i++) {
			locks[i*2] = random_map_node();
			locks[i*2+1] = random_map_value();
			// perform changes to target nodes
			int peek = * reinterpret_cast<char*>(locks[i*2].first);
			* reinterpret_cast<char*>(locks[i*2].first) = 1;
			memset(locks[i*2].first, 1, locks[i*2].second);
			// modify the value
			peek = * reinterpret_cast<char*>(locks[i*2+1].first);
			memset(locks[i*2+1].first, 1, locks[i*2+1].second);
		}
		// simulate the commit
		for (int i=0; i<g_ops_per_txn; i++) {
			// during commit, the old value is deallocated during replace.
			std::pair<void*,size_t> oldvalue = random_map_value();
			int peek = * reinterpret_cast<char*>(oldvalue.first);
			memset(oldvalue.first, 1, oldvalue.second);
			// simulate some final updates to commit changes to the modified node
			peek = * reinterpret_cast<char*>(locks[i*2].first);
			memset(locks[i*2].first, 2, 4);
		}
	}
};

class checkpoint_operation  : public operation
{
public:
	checkpoint_operation(int inter)
	: interval( inter )
	{ }
	
	virtual ~checkpoint_operation() { }
	
	virtual void operator()() {
		while (true) {
#ifdef BOOST_INTERPROCESS_WINDOWS
			Sleep(interval);
#else
			sleep(interval);
#endif
			{
				stldb::timer t("std_transactional_operation");
				std::cout << "start checkpoint" << std::endl;
				g_region->checkpoint(".", trans_mutex);
				std::cout << "end checkpoint" << std::endl;
			}
		}
	}
private:
	int interval;
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
	void add(operation* op, int frequency ) {
		_max_freq += frequency;
		_ops.insert( std::make_pair<const int,operation*>( _max_freq, op ) );
	}
	
	// Run through loop_size randomly
	void operator()() {
		if (_ops.size() == 1) {
			// fast loop
			operation &op = *(_ops[_max_freq]);
			for (int i=0; i<loop_size; i++) {
				op();
			}
		}
		else {
			// slower loop, randomly pick work to do.
			for (int i=0; i<loop_size; i++) {
				// randomly pick one operation, and execute it.
				int r = static_cast<int>((::rand() * (double)_max_freq) / RAND_MAX);
				(*_ops.lower_bound(r)->second)();
			}
		}
	}
	
private:
	int loop_size;
	std::map<int,operation*> _ops;
	int _max_freq;
};


// run-time configuration in the form of name/value pairs.
properties_t properties;


int main(int argc, const char* argv[])
{
	properties.parse_args(argc, argv);
	
	stldb::timer_configuration config;
	config.enabled_percent = properties.getProperty("timing_percent", 0.0);
	config.report_interval_seconds = properties.getProperty("report_freq", 60);
	config.reset_after_print = properties.getProperty("report_reset", true);
	stldb::timer::configure( config );
	stldb::tracing::set_trace_level(stldb::fine_e);

	const int thread_count = properties.getProperty("threads", 4);
	const int loopsize = properties.getProperty("loopsize", 100);
	g_ops_per_txn = properties.getProperty("ops_per_txn", g_ops_per_txn);
	
	g_map_node_size = properties.getProperty("map_node_size", g_map_node_size);
	g_avg_key_length = properties.getProperty("avg_key_size", g_avg_key_length);
	g_avg_val_length = properties.getProperty("avg_value_size", g_avg_val_length);
	int total_entry_size = g_map_node_size + g_avg_key_length + g_avg_val_length;
	
	g_region_size = properties.getProperty("region_size", g_region_size);
	g_map_region_size = properties.getProperty("map_region_size", g_region_size * g_map_node_size / total_entry_size);
	g_key_region_size = properties.getProperty("key_region_size", g_region_size * g_avg_key_length / total_entry_size);
	g_val_region_size = properties.getProperty("value_region_size", g_region_size - g_map_region_size - g_key_region_size);
	
	boost::posix_time::time_duration g_checkpoint_interval = boost::posix_time::seconds(properties.getProperty("checkpoint_interval", 0));
	
	// open the checkpointable region we will test with
	g_region = new checkpointable_managed_mapped_file(boost::interprocess::open_or_create, 
											  "test_region", "test_checkpoint", g_region_size);
	region_base = reinterpret_cast<char*>(g_region->get_address());

	// The loop that the running threads will execute
	test_loop loop(loopsize);
	SimulatedUpdateTransaction update;
	//TODO: SimulatedInsertTransaction insert(ops_per_txn);
	loop.add( &update, 60 );
	//TODO: loop.add( &insert, 40 );
	
	// Start the threads which are going to run operations:
	boost::thread **workers = new boost::thread *[thread_count];
	for (int i=0; i<thread_count; i++) {
		workers[i] = new boost::thread( loop );
	}
	// start a thread which does periodic checkpointing
	boost::thread *checkpointor = NULL;
	if ( g_checkpoint_interval.seconds() > 0 ) {
		checkpointor = new boost::thread( checkpoint_operation(g_checkpoint_interval.seconds()) );
	}
	
	// now await their completion
	for (int i=0; i<thread_count; i++) {
		workers[i]->join();
		delete workers[i];
	}
	
	// final print timing stats (if requested)
	if (config.enabled_percent > 0.0)
		stldb::time_tracked::print(std::cout, true);
	
	return 0;
}
