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

void snapshot(int tid, int mainfd, int currentfd, struct headers *hdr, struct flock *lck, const char *actor) {
  int walVersion = -1;
  walVersion = readWalVersion();
  printf("-------- [%d, %s] SNAPSHOTTING --> %d --------\n", tid, actor, hdr->h1_version + walVersion);
  //sleep(100000); // hold the lock forever
  // -- truncate WAL and update version
  truncateWal();
  upgradeVersion(mainfd, hdr, walVersion);
  walVersion = 0;
  upgradeHeaderWalVersion(mainfd, hdr, walVersion);
  // -- unlock and close all
  exclusiveUnlock(actor, currentfd, lck);
  closeFiles(mainfd, currentfd);
}

void forcedUpgrade(int tid, const char *actor) {
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  int currentfd = -1, lockfd = -1, walVersion = -1, mainfd = -1;
  struct headers hdr;
  struct headers checkHdr;
  mainfd = writer__openMainFile();
  readHeaders(mainfd, &hdr);
  currentfd = writer__openLockFileCurrentVersion(&hdr, mainfd);
  exclusiveLockWait(actor, currentfd, &lck);
  // -- we have exclusive..
  readHeaders(mainfd, &checkHdr);
  // dumpHeaders(&hdr);
  // dumpHeaders(&checkHdr);
  if (hdr.h1_is_current != checkHdr.h1_is_current || hdr.h1_version != checkHdr.h1_version) {
    exclusiveUnlock(actor, currentfd, &lck);
    closeFiles(mainfd, currentfd);
    return;
  }
  snapshot(tid, mainfd, currentfd, &hdr, &lck, actor);
}

bool checkForcedVersionUpgrade(int mainfd, struct headers *hdr, int walVersion) {
  return isHeaderWalEven(hdr, walVersion) && isHeaderWalAboveThreshold(hdr, MAX_WAL_VERSION);
}

void *reader(void *arg) {
  long tid = (long) arg;
  struct headers hdr;
  char buf[256];
  struct flock lck = { .l_whence = SEEK_SET, .l_start = 0, .l_len = 1 };
  for (int i = 0; i < ITERATIONS_PER_THREAD_RUN; i++) {
    struct timeval st, et;
    char *current = NULL;
    int tries = 0, lockfd = -1, mainfd = -1, latency = 0, currVersion = 0, walVersion = 0;
    // -- Open current version, handle locking and forced version upgrade
    gettimeofday(&st, NULL);
    walVersion = readWalVersion();
    mainfd = reader__openMainFile();
    readHeaders(mainfd, &hdr);
    lockfd = reader__openLockFileCurrentVersion(&hdr, mainfd);
    sharedLock(__func__, lockfd, &lck);
    readHeaders(mainfd, &hdr); // fresh read needed after lock acquired
    if (!ensureCorrectVersionLocked(mainfd, lockfd, &hdr)) {
      sharedUnlock(__func__, lockfd, &lck);
      closeFiles(mainfd, lockfd);
      continue;
    }
    if (checkForcedVersionUpgrade(mainfd, &hdr, walVersion)) {
      sharedUnlock(__func__, lockfd, &lck);
      closeFiles(mainfd, lockfd);
      forcedUpgrade(tid, __func__);
      continue;
    }
    gettimeofday(&et, NULL);
    __debugPrintStart(buf, 255, __func__, tid, &hdr, getUsecDiff(&st, &et), tries);
    // -- Run read workload on current version
    reader__waitWorkloadTime();
    __debugPrintEnd(buf, __func__, tid, &hdr, walVersion);
    // -- Finish & breath
    sharedUnlock(__func__, lockfd, &lck);
    closeFiles(mainfd, lockfd);
    reader__waitPauseTime();
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
    int fd2 = -1, latency = -1, tries = 0, mainfd = -1, walVersion = -1, lockfd = -1;
    // -- single writer: read headers and get lock on old version --
    gettimeofday(&st, NULL);
    mainfd = writer__openMainFile();
    readHeaders(mainfd, &hdr);
    walVersion = readWalVersion();
    lockfd = writer__openLockFileOldVersion(&hdr, mainfd);
    tries = exclusiveLockOneTry(__func__, lockfd, &lck);
    gettimeofday(&et, NULL);
    __debugPrintStart(buf, 255, __func__, tid, &hdr, getUsecDiff(&st, &et), walVersion);
    if (checkForcedVersionUpgrade(mainfd, &hdr, walVersion)) {
      if (tries) exclusiveUnlock(__func__, lockfd, &lck);
      forcedUpgrade(tid, __func__);
      continue; // -- restarting writer..
    }

    // -- then update WAL
    writer__waitWalUpdateTime();
    walVersion = upgradeWalVersion();
    upgradeHeaderWalVersion(mainfd, &hdr, walVersion);
    if (tries > 0) {
      // -- then update old version
      writer__waitWorkloadTime();
      // -- then release old version lock and single try get exclusive on the current version
      gettimeofday(&st, NULL);
      // we unlock so "version upgrade" lock waiters are unlocked, even if we would re-acquire the same lock again
      exclusiveUnlock(__func__, lockfd, &lck);
      lockfd = writer__openLockFileCurrentVersion(&hdr, mainfd);
      tries = exclusiveLockOneTry(__func__, lockfd, &lck);
      gettimeofday(&et, NULL);
      if (tries > 0) {
        // -- and truncate WAL and update version
        truncateWal();
        // printf("%d.%d %d.%d %d\n", hdr.h1_version, hdr.h1_wal_version, hdr.h2_version, hdr.h2_wal_version, walVersion);
        upgradeVersion(mainfd, &hdr, walVersion);
        walVersion = 0;
        upgradeHeaderWalVersion(mainfd, &hdr, walVersion);
      }
    }
    __debugPrintEnd(buf, __func__, tid, &hdr, walVersion);
    // -- Finish & breath
    fsync(mainfd);
    exclusiveUnlock(__func__, lockfd, &lck);
    closeFiles(mainfd, lockfd);
    writer__waitPauseTime();
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
