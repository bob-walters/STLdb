# STLdb

STLdb is an ACID-compliant, embedded, C++ NoSQL database.  It seeks to expose an API for manipulation of transactional STL containers that are in attached shared memory (mmaps).   As an embedded databse, access is therefore without the overhead of IPC, or even data serialization during reads.  Write operations incur serialization for the purpose of the write-ahead log (and any subsequent recovery based on it), thus this database can be very efficient for OLTP workloads which are primarilly read intensive.

See [https://bob-walters.github.io/STLdb/index.html](https://bob-walters.github.io/STLdb/index.html) for the main project documentation

> This is a work in progress.  This library was being created a potential boost.org library submission, and depends on boost build conventions.  Active work stopped around 2008.  This library needs additional work.  If interested in seeing this library make progress, please email the author.  Currently work has stalled due to lack of interest and other priorities.


## Building

This library builds using boost Jamfiles (and the 'b2' executable)

With `b2` on your path and the `BOOST_ROOT` environment variable set to the root of your boost installation, you can just build with:

```
cd stldb_lib
b2 --install-prefix=/wherever/you/want/it
cd ../stldb_test
b2 --install-prefix=/wherever/you/want/it
```

For more info on the b2 application and it's options, see the [boost.org build documentation](https://www.boost.org/doc/libs/1_79_0/more/getting_started/unix-variants.html).

The `stldb_test` directory contains a stress testing application.


## Documentation

The `stldb_lib/docs` directory contains the boostbook documentation source.  It builds via a Jamfile, and depends on quickbooks and doxygen.  The reference documentation is still a work in progress.

A copy of the built documentation has been pushed to the 'generated_docs' branch and is available at [https://bob-walters.github.io/STLdb/index.html](https://bob-walters.github.io/STLdb/index.html)



