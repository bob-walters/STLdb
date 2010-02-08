
#ifndef STLDB_STRING_SERIALIZE
#define STLDB_STRING_SERIALIZE

// A downside of using boost::interprocess is that its containers aren't
// automatically recognized by boost::serialization as being std containers,
// due to the namespace difference.  For basic_string, I needed to address this.


#include <string>

#include <boost/config.hpp>
#include <boost/serialization/wrapper.hpp>
#include <boost/serialization/level.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/interprocess/containers/string.hpp>


#ifdef __GNUC__
#include <stldb/containers/gnu/basic_string.h>
#endif

#if 0
// Variant1:
// This approach Seemed good, but has the weird side effect that any string whose total size
// (including \0) was >= 16 bytes would fail to serialize correctly.

// This is a partial specialization of certain templates in boost::serialization, based on
// what the BOOST_CLASS_IS_WRAPPER() and BOOST_CLASS_IMPLEMENTATION(...,primitive_type) are
// normally used with std::string.  The intent here is to covery every possible variation
// of std::basic_string with one specialization.

namespace boost {
namespace serialization {


template<typename C, class T, class A>
struct is_wrapper< std::basic_string<C,T,A> > : mpl::true_ {};

template <typename C, class T, class A>
struct implementation_level< std::basic_string<C,T,A> >
{
    typedef mpl::integral_c_tag tag;
    typedef mpl::int_< boost::serialization::primitive_type > type;
    BOOST_STATIC_CONSTANT(
        int,
        value = implementation_level::type::value
    );
};

template<typename C, class T, class A>
struct is_wrapper< boost::interprocess::basic_string<C,T,A> > : mpl::true_ {};

template <typename C, class T, class A>
struct implementation_level< boost::interprocess::basic_string<C,T,A> >
{
    typedef mpl::integral_c_tag tag;
    typedef mpl::int_< boost::serialization::primitive_type > type;
    BOOST_STATIC_CONSTANT(
        int,
        value = implementation_level::type::value
    );
};

} // namespace serialization
} // namespace boost
#endif



namespace boost {
namespace serialization {


// Serialization support for boost::interprocess::basic_string
template<class Archive, class CharT, class CharTraits, class AllocType>
void save(Archive & ar
		  , const boost::interprocess::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	std::size_t len = s.length();
	ar & len;
	ar.save_binary(s.data(), len);
}

template<class Archive, class CharT, class CharTraits, class AllocType>
void load(Archive & ar
		  , boost::interprocess::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	static const std::size_t chunk_size = 4096;
	char buff[chunk_size];

	std::size_t len;
	ar & len;

	s.clear();
	s.reserve(len);
	while (len > chunk_size) {
		ar.load_binary(buff, chunk_size);
		s.append(buff, chunk_size);
		len -= chunk_size;
	}
	if (len>0) {
		ar.load_binary(buff, len);
		s.append(buff, len);
	}
}

// Serialization support for boost::interprocess::basic_string
template<class CharT, class CharTraits, class AllocType>
void save(boost::archive::xml_oarchive & ar
		  , const boost::interprocess::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	std::string value( s.c_str() );
	ar & BOOST_SERIALIZATION_NVP(value);
}

template<class CharT, class CharTraits, class AllocType>
void load(boost::archive::xml_iarchive & ar
		  , boost::interprocess::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	std::string value;
	ar & BOOST_SERIALIZATION_NVP(value);
	s = value;
}

// copied from dfn of BOOST_SERIALIZATION_SPLIT_FREE()
template<class Archive, class CharT, class CharTraits, class AllocType>
inline void serialize(Archive & ar
					  , boost::interprocess::basic_string<CharT, CharTraits, AllocType> & t
					  , const unsigned int file_version )
{
    boost::serialization::split_free(ar, t, file_version);
}

// Serialization support for std::basic_string
template<class Archive, class CharT, class CharTraits, class AllocType>
void save(Archive & ar
		  , const std::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	std::size_t len = s.length();
	ar & len;
	ar.save_binary(s.data(), len);
}

template<class Archive, class CharT, class CharTraits, class AllocType>
void load(Archive & ar
		  , std::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	static const std::size_t chunk_size = 4096;
	char buff[chunk_size];

	std::size_t len;
	ar & len;

	s.clear();
	s.reserve(len);
	while (len > chunk_size) {
		ar.load_binary(buff, chunk_size);
		s.append(buff, chunk_size);
		len -= chunk_size;
	}
	if (len>0) {
		ar.load_binary(buff, len);
		s.append(buff, len);
	}
}

// copied from dfn of BOOST_SERIALIZATION_SPLIT_FREE()
template<class Archive, class CharT, class CharTraits, class AllocType>
inline void serialize(Archive & ar
					  , std::basic_string<CharT, CharTraits, AllocType> & t
					  , const unsigned int file_version )
{
    boost::serialization::split_free(ar, t, file_version);
}

#ifdef __GNUC__
// Serialization support for stldb::basic_string
template<class Archive, class CharT, class CharTraits, class AllocType>
void save(Archive & ar
		  , const stldb::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	std::size_t len = s.length();
	ar & len;
	ar.save_binary(s.data(), len);
}

template<class Archive, class CharT, class CharTraits, class AllocType>
void load(Archive & ar
		  , stldb::basic_string<CharT,CharTraits,AllocType>& s
		  , unsigned int version)
{
	static const std::size_t chunk_size = 4096;
	char buff[chunk_size];

	std::size_t len;
	ar & len;

	s.clear();
	s.reserve(len);
	while (len > chunk_size) {
		ar.load_binary(buff, chunk_size);
		s.append(buff, chunk_size);
		len -= chunk_size;
	}
	if (len>0) {
		ar.load_binary(buff, len);
		s.append(buff, len);
	}
}

// copied from dfn of BOOST_SERIALIZATION_SPLIT_FREE()
template<class Archive, class CharT, class CharTraits, class AllocType>
inline void serialize(Archive & ar
					  , stldb::basic_string<CharT, CharTraits, AllocType> & t
					  , const unsigned int file_version )
{
    boost::serialization::split_free(ar, t, file_version);
}
#endif

} // namespace
} // namespace


#if 0
#include <vector>
#include <string>
#include <boost/interprocess/containers/string.hpp>
#include <boost/serialization/collections_save_imp.hpp>
#include <boost/serialization/collections_load_imp.hpp>
#include <boost/serialization/split_free.hpp>


using boost::interprocess::basic_string;

namespace boost {
namespace serialization {


// basic_string - general case
template<class Archive, class CharT, class Traits, class Allocator>
inline void save(
    Archive & ar,
    const boost::interprocess::basic_string<CharT, Traits, Allocator> &t,
    const unsigned int file_version
){
    boost::serialization::stl::save_collection<
        Archive, boost::interprocess::basic_string<CharT, Traits, Allocator>
    >(ar, t);
}

template<class Archive, class CharT, class Traits, class Allocator>
inline void load(
    Archive & ar,
    boost::interprocess::basic_string<CharT, Traits, Allocator> &t,
    const unsigned int file_version
){
    boost::serialization::stl::load_collection<
        Archive,
        boost::interprocess::basic_string<CharT, Traits, Allocator>,
        boost::serialization::stl::archive_input_seq<
            Archive,
            boost::interprocess::basic_string<CharT, Traits, Allocator>
        >,
        boost::serialization::stl::reserve_imp<
			boost::interprocess::basic_string<CharT, Traits, Allocator>
        >
    >(ar, t);
}

// split non-intrusive serialization function member into separate
// non intrusive save/load member functions
template<class Archive, class U, class Allocator>
inline void serialize(
    Archive & ar,
    boost::interprocess::basic_string<U, std::char_traits<U>, Allocator> & t,
    const unsigned int file_version
){
    boost::serialization::split_free(ar, t, file_version);
}

} // serialization
} // namespace boost

#include <boost/serialization/collection_traits.hpp>

BOOST_SERIALIZATION_COLLECTION_TRAITS(std::vector)

#endif

#endif

