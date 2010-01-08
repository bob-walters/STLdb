#pragma warning (disable:4503)

#include <map>
#include <string>
#include "test_database.h"
#include "properties.h"

// run-time configuration in the form of name/value pairs.
properties_t properties;

// Log Tester - tests log throughput
int main(int argc, const char* argv[])
{
  properties.parse_args(argc, argv);

  stldb::timer::enabled = properties.getProperty("timing", true);

  int trace_level = properties.getProperty("tracing", (int)stldb::fine_e);
  stldb::tracing::set_trace_level( (stldb::trace_level_t)trace_level );

  // buffer size
  int buffer_size = properties.getProperty("buffer_size", 0);
  // number of buffers to put into the queue before making the same # of log() calls
  // only on of the log calls will write (doing all buffers by aggregation.)
  int aggregation = properties.getProperty("aggregation", 1);
  // device block size for write alignment.
  int block_size = properties.getProperty("block_size", 512);

  // how many buffers to write during the timed test.
  int loopsize = properties.getProperty("loop_size", 1000);

  // maximum size of any one log file.
  int log_max_len = properties.getProperty("max_log_len", 256*1024*1024);

  // should fsync() be done after a write?
  int sync_write = properties.getProperty("sync", true);
  
  std::allocator<void> alloc;
  stldb::SharedLogInfo<std::allocator<void>, boost::interprocess::interprocess_mutex>
  	shared_info(alloc);
  	
  stldb::Logger<std::allocator<void>, boost::interprocess::interprocess_mutex> logger;
  logger.set_shared_info( &shared_info );
  
  // to avoid corrupting the results with buffer allocation times, I'm going
  // to keep reusing the same buffers over and over.
  typedef stldb::commit_buffer_t<std::allocator<void> > commit_buffer_t;
  std::vector<commit_buffer_t*> buffers;
  
  for (int j=0; j<aggregation; j++) {
	  buffers.push_back( new commit_buffer_t(alloc) );
	  // TODO - add buffer_size bytes to the buffer
  }
  for (int i=0; i<loopsize; i++) {
  	for (int j=0; j<aggregation; j++) {
  		logger.queue_for_commit( buffers[j] );
  	}
  	for (int j=0; j<aggregation; j++) {
  		logger.log( i*aggregation + j);
  	}
  }

}


