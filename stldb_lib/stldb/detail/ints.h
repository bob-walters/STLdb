/*
 * ints.h
 *
 *  Created on: May 25, 2009
 *      Author: RWalter3
 */

#ifndef INTS_H_
#define INTS_H_

// For systems which lack stdint.h, cstdint, etc. and don't have uint32_t explicitly
// defined.  (i.e. Windows.)

namespace stldb {

// This is just intended to protect against coding the typedefs for integers
// of various types wrong.  i.e. make the compiler remind me when this code
// needs to change
template <typename t, int bytes = sizeof(t)> struct intN {
	typedef t nothing_special;
};
template <typename t> struct intN<t, 1> {
	typedef t int8_t;
};
template <typename t> struct intN<t, 4> {
	typedef t int32_t;
};
template <typename t> struct intN<t, 8> {
	typedef t int64_t;
};

} // namespace

typedef stldb::intN<char, sizeof(char)>::int8_t int8_t;
typedef stldb::intN<int, sizeof(int)>::int32_t int32_t;
typedef stldb::intN<long long, sizeof(long long)>::int64_t int64_t;

typedef stldb::intN<unsigned char, sizeof(unsigned char)>::int8_t uint8_t;
typedef stldb::intN<unsigned int, sizeof(unsigned int)>::int32_t uint32_t;
typedef stldb::intN<unsigned long long, sizeof(unsigned long long)>::int64_t uint64_t;


#endif /* INTS_H_ */
