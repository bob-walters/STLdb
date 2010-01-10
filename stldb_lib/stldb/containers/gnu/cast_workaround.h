// <cast.h> -*- C++ -*-


#ifndef _CAST_WORKAROUND_H
#define _CAST_WORKAROUND_H 1

#include <boost/interprocess/offset_ptr.hpp>

namespace __gnu_cxx {

  template<typename _ToType>
    struct _Caster<boost::interprocess::offset_ptr<_ToType> >
    { typedef _ToType*  type; };


// Pre 4.0 version of gcc regard the above two overloaded forms of 
// pointer_cast to be the subject of ambiguity.  i.e  Doesn't take note that
// one uses pointers while the other passed by reference.  So.....
// As a temporary ambiguity buster for older compilers
  template<typename _ToType, typename _FromType>
    inline _ToType
    __static_pointer_cast1(const _FromType& __arg)
    { return _ToType(static_cast<typename _Caster<_ToType>::
		     type>(__arg.get())); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __dynamic_pointer_cast1(const _FromType& __arg)
    { return _ToType(dynamic_cast<typename _Caster<_ToType>::
		     type>(__arg.get())); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __const_pointer_cast1(const _FromType& __arg)
    { return _ToType(const_cast<typename _Caster<_ToType>::
		     type>(__arg.get())); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __reinterpret_pointer_cast1(const _FromType& __arg)
    { return _ToType(reinterpret_cast<typename _Caster<_ToType>::
		     type>(__arg.get())); }

  /**
   * Casting operations for cases where _FromType is a standard pointer.
   * _ToType can be a standard or non-standard pointer.
   */
  template<typename _ToType, typename _FromType>
    inline _ToType
    __static_pointer_cast2(_FromType* __arg)
    { return _ToType(static_cast<typename _Caster<_ToType>::
		     type>(__arg)); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __dynamic_pointer_cast2(_FromType* __arg)
    { return _ToType(dynamic_cast<typename _Caster<_ToType>::
		     type>(__arg)); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __const_pointer_cast2(_FromType* __arg)
    { return _ToType(const_cast<typename _Caster<_ToType>::
		     type>(__arg)); }

  template<typename _ToType, typename _FromType>
    inline _ToType
    __reinterpret_pointer_cast2(_FromType* __arg)
    { return _ToType(reinterpret_cast<typename _Caster<_ToType>::
		     type>(__arg)); }


} // namespace

#endif // _CAST_H
