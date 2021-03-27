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

void *reader(void *arg) {
  long tid = (long) arg;
  struct headers hdr;
  char buf[256];
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    struct timeval st, et;
    char *current = NULL;
    int tries = 0, lockfd = -1, mainfd = -1, latency = 0, currVersion = 0;
    // -- Open current version, handle locking and forced version upgrade
    gettimeofday(&st, NULL);
    mainfd = openMainFile();
    readHeaders(mainfd, &hdr);
    lockfd = openLockFile(&hdr, mainfd);
    sharedLock(__func__, lockfd, &lck);
    readHeaders(mainfd, &hdr); // fresh read needed after lock acquired
    if (!ensureCorrectVersionLocked(mainfd, lockfd, &hdr)) continue;
    forcedVersionUpgradeCheck(mainfd, &hdr); // TODO
    gettimeofday(&et, NULL);
    __debugPrintStart(buf, 255, "reader", tid, &hdr, getUsecDiff(&st, &et), tries);
    // -- Run read workload on current version
    waitReadTime();
    __debugPrintEnd(buf, "reader", tid, &hdr);
    // -- Finish & breath
    sharedUnlock(__func__, lockfd, &lck);
    closeFiles(lockfd, mainfd);
    waitReaderPauseTime();
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
    // -- single writer: read headers and get lock on old version --
    snprintf(buf, 255, "==> [%d] writer: %7d + %-3d %7d + %-3d - %s v%-7d (WAL %d)\n",
        tid, hdr.h1_version, hdr.h1_wal_version,
        hdr.h2_version, hdr.h2_wal_version,
        getCurrentVersionStr(&hdr), getCurrentVersion(&hdr), walVersion);
    if (getCurrentVersion(&hdr) == hdr.h2_version) {
      gettimeofday(&st, NULL);
      tries = exclusiveLockOneTry(__func__, fd, &lck);
      gettimeofday(&et, NULL);
      latency = getUsecDiff(&st, &et);
    } else {
      fd2 = open(FILENAME_H2, O_RDWR, 0666);
      gettimeofday(&st, NULL);
      tries = exclusiveLockOneTry(__func__, fd2, &lck2);
      gettimeofday(&et, NULL);
      latency = getUsecDiff(&st, &et);
    }
    if (tries > 0) {
      // -- then update WAL + old version
      int waitTime = rand() % WRITER_WRITE_TIME_MAX_USEC;
      usleep(waitTime);
      int walVersion = upgradeWal();
      usleep(waitTime / 10);
      upgradeVersionWal(fd, &hdr, walVersion);
      // -- then release old version lock and get exclusive on the current version
      if (getCurrentVersion(&hdr) != hdr.h2_version) {
        gettimeofday(&st, NULL);
        tries = exclusiveLockOneTry(__func__, fd, &lck);
        gettimeofday(&et, NULL);
        latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      } else {
        fd2 = open(FILENAME_H2, O_RDWR, 0666);
        gettimeofday(&st, NULL);
        tries = exclusiveLockOneTry(__func__, fd2, &lck2);
        gettimeofday(&et, NULL);
        latency = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
      }
      if (tries > 0) {
        // -- .. and truncate WAL and update version
        int walVersion = readWal();
        truncateWal();
        upgradeVersion(fd, &hdr);
        printf("%s    [%d] writer: %7d + %-3d %7d + %-3d - %s v%-7d %ld usec -- (%d)\n",
          buf, tid,
          hdr.h1_version, hdr.h1_wal_version,
          hdr.h2_version, hdr.h2_wal_version,
          getCurrentVersionStr(&hdr), getCurrentVersion(&hdr), latency, tries);
      } else {
        printf("%s    [%d] writer: %7d + %-3d %7d + %-3d - %s v%-7d %ld usec -- WAL(b) (%d)\n",
          buf, tid,
          hdr.h1_version, hdr.h1_wal_version,
          hdr.h2_version, hdr.h2_wal_version,
          getCurrentVersionStr(&hdr), getCurrentVersion(&hdr), latency, walVersion);
      }
    } else {
      // -- or write to WAL if failed to acquire old version lock
      usleep(rand() % WRITER_WRITE_TIME_MAX_USEC / 10);
      int walVersion = upgradeWal();
      printf("%s    [%d] writer: %7d + %-3d %7d + %-3d - %s v%-7d %d usec -- WAL(a) (%d)\n",
        buf, tid,
        hdr.h1_version, hdr.h1_wal_version,
        hdr.h2_version, hdr.h2_wal_version,
        getCurrentVersionStr(&hdr), getCurrentVersion(&hdr), latency, walVersion);
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
