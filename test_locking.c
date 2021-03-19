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

#define NUM_WRITERS                     1
#define NUM_READERS                     100
#define ITERATIONS_PER_THREAD_RUN       3000000 // forever..

#define READER_PAUSE_MAX_USEC           1000000 // 1s
#define READER_READ_TIME_MAX_USEC       3000000 // 3s
#define WRITER_PAUSE_MAX_USEC           1000000 // 1s
#define WRITER_WRITE_TIME_MAX_USEC      3000000 // 3s

char h1[] = "h1";
char h2[] = "h2";

void *reader(void *arg) {
  long tid = (long) arg;
  struct headers hdr;
  char buf[256];
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    int fd = open(FILENAME_H1, O_RDONLY, 0666); // OPEN 1
    int fd2 = -1;
    struct timeval st, et;
    gettimeofday(&st, NULL);
    int tries = sharedLock(__func__, fd, &lck); // LOCK 1
    readHeaders(fd, &hdr);
    char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
    int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
    if (currVersion == hdr.h2_version) {
      // -- switch lock from h1 to h2
      sharedUnlock(__func__, fd, &lck); // UNLOCK 1
      fd2 = open(FILENAME_H2, O_RDONLY, 0666); // OPEN 2
      sharedLock(__func__, fd2, &lck); // LOCK 2
      // --- read data from current version ---
      gettimeofday(&et, NULL);
      int latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      snprintf(buf, 255, "<-- [%2d] reader: %7d, %7d - %s v%-7d %d usec (%d)\n",
        tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries);
      usleep(rand() % READER_READ_TIME_MAX_USEC);
      readHeaders(fd, &hdr);
      char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
      int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
      printf("%s    [%2d] reader: %7d, %7d - %s v%-7d\n",
        buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion);
      sharedUnlock(__func__, fd2, &lck); // UNLOCK 2
      close(fd2); // CLOSE 2
    } else {
      // --- read data from current version ---
      gettimeofday(&et, NULL);
      int latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      snprintf(buf, 255, "<-- [%2d] reader: %7d, %7d - %s v%-7d %d usec (%d)\n",
        tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries);
      usleep(rand() % READER_READ_TIME_MAX_USEC);
      readHeaders(fd, &hdr);
      char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
      int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
      printf("%s    [%2d] reader: %7d, %7d - %s v%-7d\n",
        buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion);
      sharedUnlock(__func__, fd, &lck); // UNLOCK 1
    }
    close(fd); // CLOSE 1
    usleep(rand() % READER_PAUSE_MAX_USEC);
  }
  pthread_exit(NULL);
}

void *writer(void *arg) {
  long tid = (long) arg;
  char buf[256];
  struct headers hdr;
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  struct flock lck2 = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    int fd = open(FILENAME_H1, O_RDWR, 0666);
    int fd2 = -1, latency = -1, tries = 0;
    struct timeval st, et;
    readHeaders(fd, &hdr);
    int walVersion = readWal();
    char *current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
    int currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
    // -- single writer: read headers and get lock on old version --
    snprintf(buf, 255, "==> [%d] writer: %7d, %7d - %s v%-7d\n",
      tid, hdr.h1_version, hdr.h2_version, current, currVersion);
    if (currVersion == hdr.h2_version) {
      gettimeofday(&st, NULL);
      tries = exclusiveLock(__func__, fd, &lck);
      gettimeofday(&et, NULL);
      latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    } else {
      fd2 = open(FILENAME_H2, O_RDWR, 0666);
      gettimeofday(&st, NULL);
      tries = exclusiveLock(__func__, fd2, &lck2);
      gettimeofday(&et, NULL);
      latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    }
    // -- then update old version or add to WAL (if could not get lock)
    usleep(rand() % WRITER_WRITE_TIME_MAX_USEC);
    if (tries > 0) {
      // -- then get, exclusive on the current version too
      if (currVersion != hdr.h2_version) {
        gettimeofday(&st, NULL);
        tries = exclusiveLock(__func__, fd, &lck);
        gettimeofday(&et, NULL);
        latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      } else {
        fd2 = open(FILENAME_H2, O_RDWR, 0666);
        gettimeofday(&st, NULL);
        tries = exclusiveLock(__func__, fd2, &lck2);
        gettimeofday(&et, NULL);
        latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      }
      if (tries > 0) {
        // -- .. and truncate WAL and update version
        truncateWal();
        upgradeVersion(fd, &hdr);
        current = (hdr.h1_version > hdr.h2_version) ? h1 : h2;
        currVersion = (hdr.h1_version > hdr.h2_version) ? hdr.h1_version : hdr.h2_version;
        printf("%s    [%d] writer: %7d, %7d - %s v%-7d %d usec (%d)%s\n",
          buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, tries, "");
      } else {
        // -- or write to WAL if failed to acquire "full lock"
        int walVersion = upgradeWal();
        printf("%s    [%d] writer: %7d, %7d - %s v%-7d %d usec -- WAL (%d)\n",
          buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, walVersion);
      }
    } else {
      // -- or write to WAL if failed to acquire old version lock
      int walVersion = upgradeWal();
      printf("%s    [%d] writer: %7d, %7d - %s v%-7d %d usec -- WAL (%d)\n",
        buf, tid, hdr.h1_version, hdr.h2_version, current, currVersion, latency, walVersion);
    }
    fsync(fd);
    exclusiveUnlock(__func__, fd, &lck);
    exclusiveUnlock(__func__, fd2, &lck2);
    close(fd2);
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
