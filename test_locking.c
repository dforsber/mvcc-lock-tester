#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "test_locking.h"
#include "util.h"

void snapshot(int tid, int mainfd, int currentfd, int walfd, struct headers *hdr, struct flock *lck, const char *actor) {
  int walVersion = readWalVersion(walfd);
  printf("-------- [%d, %s] SNAPSHOTTING --> %d (%d) --------\n", tid, actor, hdr->h1_version + hdr->h1_wal_version, walVersion);
  truncateWal(walVersion, hdr->h1_is_current ? hdr->h2_wal_version : hdr->h1_wal_version);
  upgradeVersion(mainfd, hdr);
  walVersion = walVersion - hdr->h1_is_current ? hdr->h2_wal_version : hdr->h1_wal_version; // remaining updates
  upgradeHeaderWalVersion(mainfd, hdr, walVersion);
  exclusiveUnlock(actor, currentfd, lck);
}

void forcedUpgrade(int tid, int mainfd, int walfd, int h2fd, const char *actor) {
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  int currentfd = -1, lockfd = -1;
  struct headers hdr = {0}, checkHdr = {0};
  readHeaders(mainfd, &hdr);
  currentfd = hdr.h1_is_current ? mainfd : h2fd;
  exclusiveLockWait(actor, currentfd, &lck);
  // -- we have exclusive..
  readHeaders(mainfd, &checkHdr);
  if (hdr.h1_is_current != checkHdr.h1_is_current || hdr.h1_version != checkHdr.h1_version) {
    // .. snapshot already done
    exclusiveUnlock(actor, currentfd, &lck);
    return;
  }
  snapshot(tid, mainfd, currentfd, walfd, &checkHdr, &lck, actor);
}

bool checkForcedVersionUpgrade(int mainfd, struct headers *hdr) {
  return isHeaderWalAboveThreshold(hdr, MAX_WAL_VERSION);
}

void *reader(void *arg) {
  struct timeval st, et;
  long tid = (long) arg;
  struct headers hdr;
  char buf[256];
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    int tries = 0, lockfd = -1, mainfd = -1, walfd = -1, h2fd = -1, latency = 0, currVersion = 0, walVersion = 0;
    __reader__waitPauseTime(); // breath
    // -- Open current version, handle locking and forced version upgrade
    mainfd = reader__openMainFile();
    walfd = reader__openWalFile();
    h2fd = reader__openH2File();
    __gettimeofday(&st);
    readHeaders(mainfd, &hdr);
    while (true) {
      lockfd = hdr.h1_is_current ? mainfd : h2fd;
      sharedLock(__func__, lockfd, &lck);
      readHeaders(mainfd, &hdr); // fresh read needed after lock acquired
      if (!ensureCorrectVersionLocked(mainfd, lockfd, &hdr)) {
        sharedUnlock(__func__, lockfd, &lck);
        continue;
      }
      if (checkForcedVersionUpgrade(mainfd, &hdr)) {
        sharedUnlock(__func__, lockfd, &lck);
        forcedUpgrade(tid, mainfd, walfd, h2fd, __func__);
        continue;
      }
      break;
    }
    __gettimeofday(&et);
    __debugPrintStart(buf, 255, __func__, tid, &hdr, getUsecDiff(&st, &et), tries);
    // -- Run read workload on current version
    walVersion = readWalVersion(walfd);
    __reader__waitWorkloadTime();
    __debugPrintEnd(buf, __func__, tid, &hdr, walVersion);
    // -- close files (locks are freed too)
    closeFiles(mainfd, lockfd, walfd);
  }
  pthread_exit(NULL);
}

void *writer(void *arg) {
  long tid = (long) arg;
  char buf[256];
  struct headers hdr;
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    struct timeval st, et;
    int fd2 = -1, latency = -1, tries = 0, mainfd = -1, walfd = -1, h2fd = -1, walVersion = -1, lockfd = -1;
    // -- breath
    __writer__waitPauseTime();
    // -- single writer: read headers and get lock on old version --
    mainfd = writer__openMainFile();
    walfd = writer__openWalFile();
    h2fd = writer__openH2File();
    __gettimeofday(&st);
    while (true) {
      readHeaders(mainfd, &hdr);
      lockfd = hdr.h1_is_current ? h2fd : mainfd; // old
      if (checkForcedVersionUpgrade(mainfd, &hdr)) {
        forcedUpgrade(tid, mainfd, walfd, h2fd, __func__);
        continue; // -- restarting writer..
      }
      tries = exclusiveLockOneTry(__func__, lockfd, &lck);
      break;
    }
    __gettimeofday(&et);
    __debugPrintStart(buf, 255, __func__, tid, &hdr, getUsecDiff(&st, &et), walVersion);
    // -- then update WAL
    __writer__waitWalUpdateTime();
    walVersion = upgradeWalVersion(walfd);
    if (tries > 0) {
      // -- then update old version
      __writer__waitWorkloadTime();
      upgradeHeaderWalVersion(mainfd, &hdr, walVersion);
      // -- then release old version lock and single try get exclusive on the current version
      __gettimeofday(&st);
      // we unlock so "version upgrade" lock waiters are unlocked, even if we would re-acquire the same lock again
      exclusiveUnlock(__func__, lockfd, &lck);
      lockfd = hdr.h1_is_current ? mainfd : h2fd; // current
      tries = exclusiveLockOneTry(__func__, lockfd, &lck);
      __gettimeofday(&et);
      if (tries > 0) {
        // -- and truncate WAL and update version
        truncateWal(walVersion, hdr.h1_is_current ? hdr.h2_wal_version : hdr.h1_wal_version);
        upgradeVersion(mainfd, &hdr);
        walVersion = 0;
        upgradeHeaderWalVersion(mainfd, &hdr, walVersion);
      }
    }
    __debugPrintEnd(buf, __func__, tid, &hdr, walVersion);
    // -- Finish
    closeFiles(mainfd, lockfd, walfd);
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
