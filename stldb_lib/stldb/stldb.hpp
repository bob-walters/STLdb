/*
 * config.hpp
 *
 *  Created on: Sep 23, 2009
 *      Author: RWalter3
 */

#ifndef STLDB_CONFIG_HPP_
#define STLDB_CONFIG_HPP_

#include <boost/config.hpp>

#ifdef BOOST_HAS_DECLSPEC // defined in config system

// we need to import/export our code only if the user has specifically
// asked for it by defining either BOOST_ALL_DYN_LINK if they want all boost
// libraries to be dynamically linked, or BOOST_STLDB_DYN_LINK
// if they want just this one to be dynamically liked:
#if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_STLDB_DYN_LINK)
// export if this is our own source, otherwise import:
#ifdef BOOST_STLDB_SOURCE
# define BOOST_STLDB_DECL __declspec(dllexport)
#else
# define BOOST_STLDB_DECL __declspec(dllimport)
#endif  // BOOST_STLDB_SOURCE
#endif  // DYN_LINK
#endif  // BOOST_HAS_DECLSPEC

// if BOOST_STLDB_DECL isn't defined yet define it now:
#ifndef BOOST_STLDB_DECL
#define BOOST_STLDB_DECL
#endif


#endif /* STLDB_CONFIG_HPP_ */
