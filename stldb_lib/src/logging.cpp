/*
 * logging.cpp
 *
 *  Created on: Jun 18, 2009
 *      Author: bobw
 */

#include <ios>
#include <iomanip>
#include <iostream>
#include <sstream>

#define BOOST_STLDB_SOURCE

#include <stldb/logging.h>

namespace stldb
{

// Copied this from Wikipedia...
static int MOD_ADLER = 65521;

/* data: Pointer to the data to be summed; len is in bytes */
BOOST_STLDB_DECL
uint32_t
adler(const uint8_t *data, std::size_t len)
{
    uint32_t a = 1, b = 0;

    while (len > 0)
    {
        size_t tlen = len > 5550 ? 5550 : len;
        len -= tlen;
        do
        {
            a += *data++;
            b += a;
        } while (--tlen);

        a %= MOD_ADLER;
        b %= MOD_ADLER;
    }

    return (b << 16) | a;
};




} //namespace
