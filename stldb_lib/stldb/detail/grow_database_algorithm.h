/*
 * grow_database_algorithm.h
 *
 *  Created on: Feb 20, 2010
 *      Author: bobw
 */

#ifndef GROW_DATABASE_ALGORITHM_H_
#define GROW_DATABASE_ALGORITHM_H_

namespace stldb {
namespace detail {


template <class ManagedRegionType>
class grow_database_algorithm {
public:
	bool grow_in_place(ManagedRegionType &region, std::size_t extra_bytes) {
		only_implemented_with_dynamic_managed_mapped_file();
	}
};

template <>
class grow_database_algorithm<dynamic_managed_mapped_file> {
	bool grow_in_place(dynamic_managed_mapped_file &region, std::size_t extra_bytes) {
		region.grow_in_place(extra_bytes);
	}
};


} // namespace detail
} // namespace stldb


#endif /* GROW_DATABASE_ALGORITHM_H_ */
