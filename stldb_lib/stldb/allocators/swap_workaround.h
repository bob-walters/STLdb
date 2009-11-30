/*
 * swaps.h
 *
 *  Created on: Aug 4, 2009
 *      Author: bobw
 */

#ifndef SWAPS_H_
#define SWAPS_H_

#include <algorithm>
#include <boost/interprocess/allocators/cached_adaptive_pool.hpp>
#include <boost/interprocess/allocators/cached_node_allocator.hpp>

// Some of the boost::interprocess allocator's are missing methods to deal with swap().
// I have provided specializations of std::swap() for them which do nothing.
// The reason they do nothing is because in every scenario where a swap might occur

namespace std {

// Specialization needed to get std::swap to use boost::interprocess::swap for swapping.
template <typename T, typename SegmentManager, std::size_t NodesPerBlock>
inline void swap( boost::interprocess::cached_node_allocator<T, SegmentManager, NodesPerBlock> &larg,
	       boost::interprocess::cached_node_allocator<T, SegmentManager, NodesPerBlock> &rarg )
{
	// Do nothing, but assert that it is valid to do nothing.
	assert( larg.get_segment_manager() == rarg.get_segment_manager() );
}

// Specialization needed to get std::swap to use boost::interprocess::swap for swapping.
template <typename T, typename SegmentManager, std::size_t NodesPerBlock, std::size_t MaxFreeBlocks, unsigned char OverheadPercent>
inline void swap( boost::interprocess::cached_adaptive_pool<T, SegmentManager, NodesPerBlock, MaxFreeBlocks, OverheadPercent> &larg,
	boost::interprocess::cached_adaptive_pool<T, SegmentManager, NodesPerBlock, MaxFreeBlocks, OverheadPercent> &rarg )
{
	// Do nothing, but assert that it is valid to do nothing.
	assert( larg.get_segment_manager() == rarg.get_segment_manager() );
}

} // namespace std


#endif /* SWAPS_H_ */
