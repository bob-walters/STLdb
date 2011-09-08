/*
 * trace.h
 *
 *  Created on: Jun 17, 2009
 *      Author: bobw
 */

#ifndef TRACE_H_
#define TRACE_H_

#include <iostream>
#include <sstream>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <stldb/stldb.hpp>

/// The code has scattered STLDB_TRACE macros, which can be disabled.
#ifdef STLDB_TRACING

#define STLDB_TRACE(lvl,msg) \
	if(::stldb::tracing::get_trace_level() >= lvl) { \
		std::ostringstream smsg; \
		smsg << msg ; \
		std::string msgs(smsg.str()); \
		::stldb::tracing::get_tracer()->log(lvl, __FILE__ , __LINE__ , msgs.c_str()); \
	}

#define STLDB_TRACEDB(lvl,dbname,msg) \
	if(::stldb::tracing::get_trace_level() >= lvl) { \
		std::ostringstream smsg; \
		smsg << "[" << dbname << "] " << msg ; \
		std::string msgs(smsg.str()); \
		::stldb::tracing::get_tracer()->log(lvl, __FILE__ , __LINE__ , msgs.c_str()); \
	}

#else

#define STLDB_TRACE(lvl,msg)
#define STLDB_TRACEDB(lvl,dbname,msg)

#endif

// Special versions of these macros which are completely optimized out
// of release builds
#if defined(DEBUG) && defined(STLDB_TRACING)

#define STLDB_TRACE_DEBUG(lvl,msg) \
	if(::stldb::tracing::get_trace_level() >= lvl) { \
		std::ostringstream smsg; \
		smsg << msg ; \
		std::string msgs(smsg.str()); \
		::stldb::tracing::get_tracer()->log(lvl, __FILE__ , __LINE__ , msgs.c_str()); \
	}

#define STLDB_TRACEDB_DEBUG(lvl,dbname,msg) \
	if(::stldb::tracing::get_trace_level() >= lvl) { \
		std::ostringstream smsg; \
		smsg << "[" << dbname << "] " << msg ; \
		std::string msgs(smsg.str()); \
		::stldb::tracing::get_tracer()->log(lvl, __FILE__ , __LINE__ , msgs.c_str()); \
	}

#else

#define STLDB_TRACE_DEBUG(lvl,msg)
#define STLDB_TRACEDB_DEBUG(lvl,dbname,msg)

#endif

namespace stldb {

// Tracing levels, using as first arg of above macro.
enum trace_level_t {
	none_e = 0,
	severe_e,
	error_e,
	warning_e,
	info_e,
	fine_e,
	finer_e,
	finest_e
};

// Interface of a type which handles tracing calls.
class BOOST_STLDB_DECL tracer {
public:
	virtual ~tracer() { }
	virtual void log(trace_level_t, const char *filename, int loc, const char *msg) = 0;
};

// class of static methods for setting trace levels.
class BOOST_STLDB_DECL tracing {
public:
	// Used to set the tracer that handles trace messages.
	static tracer* get_tracer();
	static void set_tracer( tracer *t );

	// Get and set the current tracing level
	static trace_level_t get_trace_level();
	static void set_trace_level(trace_level_t lvl);

private:
	static tracer* s_tracer;
	static trace_level_t s_trace_level;
};


// This class is an example tracing implementation.  You can override this by
// calling tracer::set_instance() and passing some instance with an overloaded
// log() method.
class BOOST_STLDB_DECL cerr_tracer : public tracer {
public:
	cerr_tracer()
		: _mutex()
		, _file_loc_included(false)
		{ }

	// Log the passed message to cerr;
	virtual void log(trace_level_t lvl, const char *filename, int loc, const char *msg) {
		boost::lock_guard<boost::mutex> guard(_mutex);

		std::cerr << _level_names[(int)lvl];
		if (_file_loc_included) {
			std::cerr << filename << ":" << loc << ": ";
		}
		std::cerr << msg << std::endl;
		std::cerr.flush();
	}

	// Get and set whether the file name and line of code should be included
	// in the trace messages from STLDB_TRACE macro calls.
	bool file_loc_included() const {
		return _file_loc_included;
	}
	void set_file_loc_included(bool b) {
		_file_loc_included = b;
	}

private:
	boost::mutex  _mutex;
	bool _file_loc_included;

	// The text-names of the logging levels.
	static const char* _level_names[8];
};

} // namespace

#endif /* TRACE_H_ */
