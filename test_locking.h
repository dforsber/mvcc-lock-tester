#ifndef __TEST_LOCKING_H__
#define __TEST_LOCKING_H__
#include <stdbool.h>

#define DEBUG 1

#define FILENAME_H1                     "test.mydb"
#define FILENAME_H2                     "test.mydb.h2"
#define FILENAME_WAL                    "test.mydb.WAL"

#define MAX_WAL_VERSION                 10
#define NUM_WRITERS                     1
#define NUM_READERS                     20
#define ITERATIONS_PER_THREAD_RUN       3000000 // forever..

#define READER_PAUSE_MAX_USEC           3000000 // 3s
#define READER_READ_TIME_MAX_USEC       3000000 // 3s
#define WRITER_PAUSE_MAX_USEC           100000  // 100ms
#define WRITER_WRITE_TIME_MAX_USEC      300000  // 300ms

struct headers {
  bool h1_is_current;

  int h1_version;
  int h1_wal_version;

  int h2_version;
  int h2_wal_version;
};

#endif // __TEST_LOCKING_H__