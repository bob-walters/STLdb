#pragma warning (disable:4503)

#include <map>
#include <string>
#include "test_database.h"
#include "properties.h"
#include "perf_test.h"

// run-time configuration in the form of name/value pairs.
properties_t properties;
using namespace std;
using stldb::transaction_id_t;
using stldb::log_reader;
using stldb::log_stats;

template <class void_allocator, class mutex_type>
class writelog : public trans_operation
{
public:
	typedef stldb::commit_buffer_t<void_allocator> commit_buffer_t;
	typedef stldb::Logger<void_allocator, mutex_type> logger_t;

	writelog(const void_allocator& alloc, logger_t& log, std::size_t buffer_size)
		: buff( new commit_buffer_t(alloc) ), logger( log )
	{
	    buff->reserve(buffer_size);
	    for (std::size_t k=0; k<buffer_size; k++) {
	      buff->push_back(k);
	    }
	    assert(buff->size() == buffer_size);
	    buff->op_count = 10;
	}

	virtual void operator()() {
		assert(buff->op_count==10);
		assert(buff->size() >0);
		stldb::transaction_id_t commit_id = logger.queue_for_commit( buff );
		logger.log( commit_id );
	}

	virtual ~writelog() {
		delete buff;
	}

	virtual void print_totals() { 
		logger.get_stats().print(std::cout);
	}

private:
    commit_buffer_t *buff;
    logger_t &logger;
};



// Log Tester - tests log throughput
int main(int argc, const char* argv[])
{
  properties.parse_args(argc, argv);

  stldb::timer_configuration config;
  config.enabled_percent = properties.getProperty("timing_percent", 0.0);
  config.report_interval_seconds = properties.getProperty("report_freq", 60);
  config.reset_after_print = properties.getProperty("report_reset", true);
  stldb::timer::configure( config );

  int trace_level = properties.getProperty("tracing", (int)stldb::fine_e);
  stldb::tracing::set_trace_level( (stldb::trace_level_t)trace_level );

  // directory to write the log file into.
  std::string default_dir( "." );
  std::string log_dir = properties.getProperty("log_dir", default_dir);

  // buffer size
  int buffer_size = properties.getProperty("buffer_size", 0);
  // number of buffers to put into the queue before making the same # of log() calls
  // only on of the log calls will write (doing all buffers by aggregation.)
  int thread_count = properties.getProperty("threads", 1);

  // how many buffers to write during the timed test.
  int loopsize = properties.getProperty("loop_size", 1000);

  // maximum size of any one log file.
  int log_max_len = properties.getProperty("max_log_len", 256*1024*1024);

  // should fsync() be done after a write?
  bool sync_write = properties.getProperty("sync", true);

  // should fsync() be done after a write?
  bool verify = properties.getProperty("verify", true);
  
  // device block size for write alignment.
  //int block_size = properties.getProperty("block_size", 512);
  // Set blocksize.
  //stldb::Logger<std::allocator<void>, boost::interprocess::interprocess_mutex>.optimum_write_alignment = block_size;

  std::allocator<void> alloc;
  stldb::SharedLogInfo<std::allocator<void>, boost::interprocess::interprocess_mutex>
  	shared_info(alloc);
  shared_info.log_dir= log_dir.c_str();
  shared_info.log_max_len = log_max_len;
  shared_info.log_sync = sync_write;

  stldb::Logger<std::allocator<void>, boost::interprocess::interprocess_mutex> logger;
  logger.set_shared_info( &shared_info );

  typedef writelog<std::allocator<void>, boost::interprocess::interprocess_mutex> op_t;
  op_t *op = new op_t(alloc, logger, buffer_size);

  test_loop workload(loopsize);
  workload.add( op, 100 );

  performance_test(thread_count, workload);

  if (verify) {
	  cout << "Verifying log file contents" << endl;
	  std::map<transaction_id_t,boost::filesystem::path> files = stldb::detail::get_log_files(log_dir);
	  for (std::map<transaction_id_t,boost::filesystem::path>::const_iterator i = files.begin();
		  i != files.end(); i++ )
	  {
		stldb::timer read_file("load_reader::read_log_file");

		transaction_id_t txn_found = 0;
		std::size_t count = 0;
		log_reader reader(i->second.string().c_str());
		try {
			while ((txn_found = reader.next_transaction()) != stldb::no_transaction) {
				count++;
			}
		}
		catch( stldb::recover_from_log_failed &ex ) {
			cout << "log_reader exception received: " << ex.what() << endl;
			cout << "Occurred at byte " << ex.offset_in_file() << " of file: " << ex.filename() << endl;
			cout << (reader.get_transaction().first);
		}
		cout << "Read " << count << " transactions from " << i->second << ", ending with txn_id: " << txn_found << endl;
	  }
  }

  if (stldb::timer::configuration().enabled_percent > 0.0) {
    stldb::time_tracked::print(std::cout, true);
  }


}


