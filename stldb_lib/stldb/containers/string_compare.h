/*
 * string_compare.h
 *
 *  Created on: May 2, 2009
 *      Author: bobw
 */

#ifndef STRING_COMPARE_H_
#define STRING_COMPARE_H_

#include <string>
#include <boost/interprocess/containers/string.hpp>

template<typename CharT, typename Traits, typename Alloc1, typename Alloc2>
bool operator==( const std::basic_string<CharT, Traits, Alloc1> &str1,
				 const boost::interprocess::basic_string<CharT, Traits, Alloc2> &str2 )
{
	return str1.compare(str2.c_str())==0;
}

template<typename CharT, typename Traits, typename Alloc1, typename Alloc2>
bool operator==( const boost::interprocess::basic_string<CharT, Traits, Alloc1> &str2,
				 const std::basic_string<CharT, Traits, Alloc2> &str1 )
{
	return str1.compare(str2.c_str())==0;
}

template<typename CharT, typename Traits, typename Alloc1, typename Alloc2>
bool operator!=( const std::basic_string<CharT, Traits, Alloc1> &str1,
				 const boost::interprocess::basic_string<CharT, Traits, Alloc2> &str2 )
{
	return str1.compare(str2.c_str())!=0;
}

template<typename CharT, typename Traits, typename Alloc1, typename Alloc2>
bool operator!=( const boost::interprocess::basic_string<CharT, Traits, Alloc1> &str2,
				 const std::basic_string<CharT, Traits, Alloc2> &str1 )
{
	return str1.compare(str2.c_str())!=0;
}


#endif /* STRING_COMPARE_H_ */
