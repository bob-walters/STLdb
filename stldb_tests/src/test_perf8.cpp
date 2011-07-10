
/*
 * DatabaseSample.cpp
 *
 *  Created on: Oct 7, 2008
 *      Author: bobw
 */

#include <stldb/containers/concurrent_trans_map.h>
#include "test_database.h"
#include "perf_test.h"
#include "trans_map_ops.h"
#include "properties.h"
#include <stldb/timing/time_tracked.h>

using stldb::Transaction;
using namespace std;


extern properties_t properties;

// Test structure to use for raw speed tests
template <class mutex_t>
struct test8 {
	int _counter;
	mutex_t _mutex;

	typedef mutex_t mutex_type;

	test8() : _counter(0), _mutex() { }
	test8(const test8 &rarg) : _counter(rarg._counter), _mutex() { }
	test8& operator=(const test8 &rarg) {
		this->_counter = rarg._counter;
		return *this;
	}
	bool operator<(const test8 &rarg) const {
		return (this->_counter < rarg._counter);
	}
	bool operator==(const test8 &rarg) const {
		return (this->_counter == rarg._counter);
	}
private:
	friend class boost::serialization::access;
	friend std::ostream& operator<<(std::ostream &s, const test8 &s8);

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
	    ar & _counter;
	}
};

template <class mutex_t>
std::ostream& operator<<(std::ostream &s, const test8<mutex_t> &s8)
{
	s << s8._counter;
}


// map_type is required to be some instantiation of trans_map<>
template <class database_type, class struct_type, class lock_type>
class increment_operation : public trans_operation
{
public:
	increment_operation(struct_type *s)
		: _struct(s)
		{ }

	virtual void operator()() {
		timer t("increment_operation");

		{ // lock scope
			lock_type lock_holer(_struct->_mutex);
			//scoped_lock<typename struct_type::mutex_type> lock_holder(_struct->_mutex);
			_struct->_counter ++;
		}
	}

private:
	struct_type *_struct;
};

// Used to allow a speed test without mutexes.
struct DummyMutex {
	DummyMutex() { }
	void lock_sharable() { }
	void unlock_sharable() { }
};


template <class MutexType, template <class MutexType> class LockType>
int run_test_8(const char *test_name)
{
	timer t(test_name);

	// Construct the database
	// A transactional map, using std ref counted strings.
	typedef test8<MutexType> MyStruct;
	typedef stldb::concurrent::trans_map<int, MyStruct
			, std::less<int>
			, boost::interprocess::allocator<std::pair<const int, MyStruct>,
				managed_mapped_file::segment_manager>
			, stldb::bounded_mutex_family
			, 1031>  MapType;

	TestDatabase<managed_mapped_file,MapType> db(test_name);
	managed_mapped_file &region = db.getRegion();

	// Allocate our special structure therein
	MyStruct *test_struct = region.find_or_construct<MyStruct>("the_struct")();

	// ensure that default constructors on stldb::gnu::allocator know which region to use by default
	srand( static_cast<unsigned int>(time(NULL)) );

	// TEST 1:
	// Do a single-threaded insert test of 100k entries. Every iterator is to just
	// insert 10 rows.
	int thread_count = properties.getProperty("threads", 1);
	int loop_size = properties.getProperty("loopsize", 1000000);

	test_loop test_ops( loop_size );
	test_ops.add( new increment_operation<TestDatabase<managed_mapped_file,MapType>,MyStruct,
			LockType<MutexType> >(test_struct), 100 );

	// Run the scenario.
	performance_test( thread_count, test_ops );
	stldb::time_tracked::print(std::cout, true);

	return 0;
}

int test_perf8_none()
{
	return run_test_8<DummyMutex,boost::interprocess::sharable_lock>("test_perf8_none");
}

int test_perf8_shared()
{
	return run_test_8<stldb::bounded_interprocess_upgradable_mutex,boost::interprocess::sharable_lock>("test_perf8_shared");
}

int test_perf8_scoped()
{
	return run_test_8<stldb::bounded_interprocess_mutex,boost::interprocess::scoped_lock>("test_perf8_scoped");
}


