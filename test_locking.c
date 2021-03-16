#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "test_locking.h"
#include "util.h"

#define FILENAME_H1                     "test.mydb"
#define FILENAME_H2                     "test.mydb.h2"

#define NUM_WRITERS                     1
#define NUM_READERS                     10
#define ITERATIONS_PER_THREAD_RUN       3000000

#define READER_PAUSE_MAX_USEC           1000
#define READER_READ_TIME_MAX_USEC       200000 // 200ms
#define WRITER_PAUSE_MAX_USEC           1000
#define WRITER_WRITE_TIME_MAX_USEC      300000 // 300ms

char h1[] = "h1";
char h2[] = "h2";

void *reader(void *arg) {
  long tid = (long) arg;
  struct headers hdr;
  char buf[256];
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    int fd = open(FILENAME_H1, O_RDONLY, 0666);
    int fd2 = -1;
    struct timeval st, et;
    gettimeofday(&st, NULL);
    int tries = sharedLock(__func__, fd, &lck);
    readHeaders(fd, &hdr);
    char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
    int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
    if (currVersion == hdr.h2_version) {
      sharedUnlock(__func__, fd, &lck);
      fd2 = open(FILENAME_H2, O_RDONLY, 0666);
      sharedLock(__func__, fd2, &lck);
      gettimeofday(&et, NULL);
      int latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      // --- read data from current version ---
      snprintf(buf, 255, "<-- [%2d] reader: %7d, %7d - %s v%-7d %d usec (%d)\n", tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries);
      usleep(rand() % READER_READ_TIME_MAX_USEC);
      readHeaders(fd, &hdr);
      char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
      int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
      printf("%s    [%2d] reader: %7d, %7d - %s v%-7d\n", buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion);
      sharedUnlock(__func__, fd2, &lck);
      close(fd2);
    } else {
      gettimeofday(&et, NULL);
      int latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      // --- read data from current version ---
      snprintf(buf, 255, "<-- [%2d] reader: %7d, %7d - %s v%-7d %d usec (%d)\n", tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries);
      usleep(rand() % READER_READ_TIME_MAX_USEC);
      readHeaders(fd, &hdr);
      char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
      int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
      printf("%s    [%2d] reader: %7d, %7d - %s v%-7d\n", buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion);
      sharedUnlock(__func__, fd, &lck);
    }
    close(fd);
		usleep(rand() % READER_PAUSE_MAX_USEC);
	}
  pthread_exit(NULL);
}

void *writer(void *arg) {
  long tid = (long) arg;
  char buf[256];
  struct headers hdr;
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    int fd = open(FILENAME_H1, O_RDWR, 0666);
    int fd2 = -1, latency = -1, tries = 0;
    struct timeval st, et;
    readHeaders(fd, &hdr);
    char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
    int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
    // -- single writer: write data to old version, while readers read the current --
    usleep(rand() % WRITER_WRITE_TIME_MAX_USEC);
    snprintf(buf, 255, "==> [%d] writer: %7d, %7d - %s v%-7d\n", tid, hdr.h1_version, hdr.h2_version, current, currVersion);
    if (currVersion == hdr.h2_version) {
      gettimeofday(&st, NULL);
      tries = exclusiveLock(__func__, fd, &lck);
      gettimeofday(&et, NULL);
      latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    } else {
      fd2 = open(FILENAME_H2, O_RDWR, 0666);
      gettimeofday(&st, NULL);
      tries = exclusiveLock(__func__, fd2, &lck);
      gettimeofday(&et, NULL);
      latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    }
    // -- then get, exclusive on the current version too, and truncate WAL and update version
    upgradeVersion(fd, &hdr);
    current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
    currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
    printf("%s    [%d] writer: %7d, %7d - %s v%-7d %d usec (%d)\n", buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries);
    if (fd2 > 0) {
      exclusiveUnlock(__func__, fd, &lck) && close(fd2);
    } else {
      exclusiveUnlock(__func__, fd, &lck);
    }
    fsync(fd);
    close(fd);
    usleep(rand() % WRITER_PAUSE_MAX_USEC);
	}
  pthread_exit(NULL);
}


int main (int argc, char **argv) {
  if (argc < 2 || (strcmp(argv[1], "reader") != 0 && strcmp(argv[1], "writer") != 0)) {
    printf("USAGE: %s <reader|writer>\n", argv[0]);
    exit(1);
  }
  int isWriter = strcmp(argv[1], "writer") == 0;
  long i = 0;
  time_t t;
  struct headers hdr = {0,0};
  srand((unsigned) time(&t));
  pthread_t threads[NUM_READERS + NUM_WRITERS];
  if (isWriter) for (i = 0; i < NUM_WRITERS; i++) pthread_create (&threads[i], NULL, writer, (void *)i);
  if (!isWriter) for (i = NUM_WRITERS; i < NUM_WRITERS + NUM_READERS; i++) pthread_create (&threads[i], NULL, reader, (void *)i);
  pthread_exit(NULL);
  return 0;
}
