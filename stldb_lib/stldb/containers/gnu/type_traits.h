// -*- C++ -*-

// Copyright (C) 2005, 2006, 2007, 2009 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 3, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file ext/type_traits.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _STLDB_EXT_TYPE_TRAITS
#define _STLDB_EXT_TYPE_TRAITS 1

namespace stldb {

/*
  // Conditional expression for types. If true, first, if false, second.
  template<bool _Cond, typename _Iftrue, typename _Iffalse>
    struct __conditional_type
    { typedef _Iftrue __type; };

  template<typename _Iftrue, typename _Iffalse>
    struct __conditional_type<false, _Iftrue, _Iffalse>
    { typedef _Iffalse __type; };
*/

  // For use in string and vstring.
  template<typename _Type>
    inline bool
    __is_null_pointer(_Type* __ptr)
    { return __ptr == 0; }

  template<typename _Type>
    inline bool
    __is_null_pointer(_Type)
    { return false; }

 /*
    //
    // For use in std::copy and std::find overloads for streambuf iterators.
    //
    template<typename _Tp>
      struct __is_char
      {
        enum { __value = 0 };
        typedef __false_type __type;
      };

    template<>
      struct __is_char<char>
      {
        enum { __value = 1 };
        typedef __true_type __type;
      };

  #ifdef _GLIBCXX_USE_WCHAR_T
    template<>
      struct __is_char<wchar_t>
      {
        enum { __value = 1 };
        typedef __true_type __type;
      };
  #endif
*/

} // namespace


#endif
