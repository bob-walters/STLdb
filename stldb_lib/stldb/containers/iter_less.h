#ifndef STLDB_ITER_LESS
#define STLDB_ITER_LESS 1

#include <functional>

namespace stldb {

// A alternative to std::less which can compare iterators, even when the
// iterator implementation doesn't include operator<.  This was needed
// for cases where I wanted to use iterators as key values.
template <class iterator_type>
struct iter_less : std::binary_function<iterator_type, iterator_type, bool>
{
	bool operator()(const iterator_type& x, const iterator_type& y) const
	{ return (*x < *y); }
};


} //namespace

#endif

