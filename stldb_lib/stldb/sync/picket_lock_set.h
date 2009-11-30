/*
 * picket_lock_set.h
 *
 *  Created on: Jun 20, 2009
 *      Author: bobw
 */

#ifndef PICKET_LOCK_SET_H_
#define PICKET_LOCK_SET_H_

namespace stldb
{

// A picket lock set, for locking objects of type T.  (i.e. all pointers to T differ
// by sizeof(T).  The lock set has NumLocks locks in it.
//
// Apps acquire a lock in the picket lock by using the mutex(T*) method, which returns
// the mutex to lock for concurrency control of the entry at the address passed.
template <class T, class mutex_type, int NumLocks=31 >
class picket_lock_set
{
public:
	picket_lock_set() { }

	// returns the mutex that applies for object.  caller can then lock the mutex
	// directly, pass it to a scoped lock, etc.
	mutex_type& mutex(const void* object) {
		const long idx = ( reinterpret_cast<const long>(object) / sizeof(T) ) % NumLocks;
		_count[idx]++;
		return _mutex[idx];
	}

	// To avoid deadlock possibilities, a method is needed which can acquire the
	// locks on a set of objects.  This is needed specifically to avoid deadlocks
	// among multiple commit() processing threads.  Attempting to get the locks in
	// order of the original operations and holding them through the final update
	// would otherwise result in deadlocks.  Not holding them until all changes
	// were done would violate atomicity.  This allows the set of locks needed on
	// an iteration of elements to be determined up front, and then locks on those
	// mutexes can be acquired in a sorted order.  Two threads locking each vector
	// in result from begin() to end() will not deadlock no matter what set of T*
	// are passed in by way of FwdIterator.
	template<typename FwdIterator>
	void mutexes( FwdIterator start, FwdIterator end, std::vector<mutex_type*> &result) {
		std::vector<int> mutex_nums(end-start, 0);
		int items = 0;
		for (FwdIterator i=start; i!=end; i++) {
			mutex_nums[items++] = ( reinterpret_cast<long>(*i) / sizeof(T) ) % NumLocks;
		}
		std::sort( mutex_nums.begin(), mutex_nums.end() );
		std::vector<int>::iterator last = std::unique( mutex_nums.begin(), mutex_nums.end() );
		items = last - mutex_nums.begin();
		result.resize( items );
		for (int i=0; i<items; i++) {
			_count[mutex_nums[i]] ++;
			result[i] = &_mutex[ mutex_nums[i] ];
		}
	}

	// print stat info.
	void print_stats() {
		std::cout << "Mutex distribution: [";
		for (int i=0; i<=NumLocks; i++) {
			std::cout << _count[i] << ",";
		}
		std::cout << "]" << std::endl;
	}

private:
	mutex_type _mutex[NumLocks];
	int _count[NumLocks];
};

}

#endif /* PICKET_LOCK_SET_H_ */
