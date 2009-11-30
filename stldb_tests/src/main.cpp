#pragma warning (disable:4503)

#include <map>
#include <string>
#include "test_database.h"
#include "properties.h"


int test1();
int test2();
int test3();
int test_util1();
int test_util2();
int test_util3();
int test_util4();
int test_util5();
int test_perf1load();
int test_perf1();
int test_perf2();
int test_perf3();
int test_perf4();
int test_perf5load();
int test_perf5();
int test_perf5_readonly();
int test_perf6load();
int test_perf6();
int test_perf6_readonly();
int test_perf7load();
int test_perf7();
int test_perf7_readonly();
int test_perf8_shared();
int test_perf8_scoped();
int test_perf8_none();
int test_perf9load();
int test_perf9();
int test_perf9_readonly();
int test_perf10load();
int test_perf10();
int test_perf10_readonly();
int test_perf10bload();
int test_perf10b();
int test_perf10b_readonly();
int test_perf11load();
int test_perf11();
int test_perf11_readonly();
int test_perf11bload();
int test_perf11b();
int test_perf11b_readonly();

// run-time configuration in the form of name/value pairs.
properties_t properties;

int main(int argc, const char* argv[])
{
  properties.parse_args(argc, argv);

  stldb::timer::enabled = properties.getProperty("timing", true);
  stldb::tracing::set_trace_level(stldb::fine_e);
  std::string testname = properties.getProperty("testname", std::string("all"));

  if (testname == "test_util1" || testname == "all")
	  std::cout << "test_util1: " << test_util1() << std::endl;

  if (testname == "test_util2" || testname == "all")
	  std::cout << "test_util2: " << test_util2() << std::endl;

  if (testname == "test_util3" || testname == "all")
	  std::cout << "test_util3: " << test_util3() << std::endl;

  if (testname == "test_util4" || testname == "all")
	  std::cout << "test_util4: " << test_util4() << std::endl;

  if (testname == "test_util5" || testname == "all")
	  std::cout << "test_util5: " << test_util5() << std::endl;

  if (testname == "test1" || testname == "all")
	  std::cout << "test1: " << test1() << std::endl;

  if (testname == "test2" || testname == "all")
	  std::cout << "test2: " << test2() << std::endl;

  if (testname == "test3" || testname == "all")
	  std::cout << "test3: " << test3() << std::endl;

  if (testname == "test_perf1load" || testname == "all")
	  std::cout << test_perf1load() << std::endl;

  if (testname == "test_perf1" || testname == "all")
	  std::cout << test_perf1() << std::endl;

  if (testname == "test_perf2" || testname == "all")
	  std::cout << test_perf2() << std::endl;

  if (testname == "test_perf3" || testname == "all")
	  std::cout << test_perf3() << std::endl;

  if (testname == "test_perf4" || testname == "all")
	  std::cout << test_perf4() << std::endl;

  if (testname == "test_perf5load" || testname == "all")
	  std::cout << test_perf5load() << std::endl;

  if (testname == "test_perf5" || testname == "all")
	  std::cout << test_perf5() << std::endl;

  if (testname == "test_perf5_readonly" || testname == "all")
	  std::cout << test_perf5_readonly() << std::endl;

  if (testname == "test_perf6load" || testname == "all")
	  std::cout << test_perf6load() << std::endl;

  if (testname == "test_perf6_readonly" || testname == "all")
	  std::cout << test_perf6_readonly() << std::endl;

  if (testname == "test_perf6" || testname == "all")
	  std::cout << test_perf6() << std::endl;

  if (testname == "test_perf7load" || testname == "all")
	  std::cout << test_perf7load() << std::endl;

  if (testname == "test_perf7" || testname == "all")
	  std::cout << test_perf7() << std::endl;

  if (testname == "test_perf7_readonly" || testname == "all")
	  std::cout << test_perf7_readonly() << std::endl;

  if (testname == "test_perf8_shared" || testname == "all")
	  std::cout << test_perf8_shared() << std::endl;

  if (testname == "test_perf8_scoped" || testname == "all")
	  std::cout << test_perf8_scoped() << std::endl;

  if (testname == "test_perf8_none" || testname == "all")
	  std::cout << test_perf8_none() << std::endl;

  if (testname == "test_perf9load" || testname == "all")
	  std::cout << test_perf9load() << std::endl;

  if (testname == "test_perf9" || testname == "all")
	  std::cout << test_perf9() << std::endl;

  if (testname == "test_perf9_readonly" || testname == "all")
	  std::cout << test_perf9_readonly() << std::endl;

  if (testname == "test_perf10load" || testname == "all")
	  std::cout << test_perf10load() << std::endl;

  if (testname == "test_perf10" || testname == "all")
	  std::cout << test_perf10() << std::endl;

  if (testname == "test_perf10_readonly" || testname == "all")
	  std::cout << test_perf10_readonly() << std::endl;

  if (testname == "test_perf10bload" || testname == "all")
	  std::cout << test_perf10bload() << std::endl;

  if (testname == "test_perf10b" || testname == "all")
	  std::cout << test_perf10b() << std::endl;

  if (testname == "test_perf10b_readonly" || testname == "all")
	  std::cout << test_perf10b_readonly() << std::endl;

  if (testname == "test_perf11load" || testname == "all")
	  std::cout << test_perf11load() << std::endl;

  if (testname == "test_perf11" || testname == "all")
	  std::cout << test_perf11() << std::endl;

  if (testname == "test_perf11_readonly" || testname == "all")
	  std::cout << test_perf11_readonly() << std::endl;

  if (testname == "test_perf11bload" || testname == "all")
	  std::cout << test_perf11bload() << std::endl;

  if (testname == "test_perf11b" || testname == "all")
	  std::cout << test_perf11b() << std::endl;

  if (testname == "test_perf11b_readonly" || testname == "all")
	  std::cout << test_perf11b_readonly() << std::endl;

  return 0;
}


