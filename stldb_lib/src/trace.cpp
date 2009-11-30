/*
 * trace.cpp
 *
 *  Created on: Jun 18, 2009
 *      Author: bobw
 */
#define BOOST_STLDB_SOURCE

#include <stldb/stldb.hpp>
#include <stldb/trace.h>

namespace stldb {

// Used to set the tracer that handles trace messages.
tracer* tracing::get_tracer() {
	return s_tracer;
}
void tracing::set_tracer( tracer *t ) {
	s_tracer = t;
}

// Get and set the current tracing level
trace_level_t tracing::get_trace_level() {
	return s_trace_level;
}
void tracing::set_trace_level(trace_level_t lvl) {
	s_trace_level = lvl;
}

// assigned values are defaults.
tracer* tracing::s_tracer = new cerr_tracer();
trace_level_t tracing::s_trace_level = info_e;


// Static data of cerr_tracer
const char* cerr_tracer::_level_names[8] = {
    "",
    "SEVERE:  ",
    "ERROR:   ",
    "WARNING: ",
    "INFO:    ",
    "FINE:    ",
    "FINER:   ",
    "FINEST:  "
};

} // namespace


