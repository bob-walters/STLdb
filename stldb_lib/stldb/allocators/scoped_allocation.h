/*
 * scoped_allocation.h
 *
 *  Created on: Feb 2, 2009
 *      Author: bobw
 */

#ifndef SCOPED_ALLOCATION_H_
#define SCOPED_ALLOCATION_H_

#include <boost/thread/tss.hpp>

namespace stldb {

/**
 * scoped_allocation is used to specify that all scope-aware allocators of the specified
 * SegmentManager type, are to use the designated segment manager (region) if any of
 * the scope-aware allocators are constructed without an explicit region, i.e. constructed
 * using their default constructors.
 */
template<class SegmentManager>
class scoped_allocation {
public:
	//!Assign default allocator scope to the segment provided, for
	//!the life of this object, for the current thread
	scoped_allocation(SegmentManager *segment)
		: previous_value( current_value.get() )
	{
		current_value.reset(segment);
	}

	//!Assign default allocator scope to the segment provided, for
	//!the life of this object, for the current thread
	scoped_allocation(SegmentManager &segment)
		: previous_value( current_value.get() )
	{
		current_value.reset(&segment);
	}

	//!Return the default allocator scope to its previous value
	~scoped_allocation() {
		current_value.reset(previous_value);
	}

	static SegmentManager* scoped_manager() {
		return current_value.get();
	}

private:
	//!Default allocation is not supported.
	scoped_allocation();

	SegmentManager *previous_value;

	static boost::thread_specific_ptr<SegmentManager>  current_value;
};


// cleanup function for thread_specific_ptr used in scoped allocation.
// does nothing.  i.e The SegmentManager that we have thread specific pointers to
// is not owned by our thread-specific pointer.
template<class SegmentManager>
void cleanup_function(SegmentManager *segment) { };

template<class SegmentManager>
boost::thread_specific_ptr<SegmentManager>
scoped_allocation<SegmentManager>::current_value(&cleanup_function<SegmentManager>);

} // namespace

#endif /* SCOPED_ALLOCATION_H_ */
