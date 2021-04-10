#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>

#include "test_locking.h"
#include "util.h"

#define SHARED_LOCK_SET                 F_OFD_SETLKW
#define EXCLUSIVE_LOCK_SET              F_OFD_SETLK
#define EXCLUSIVE_LOCK_SET_WAIT         F_OFD_SETLKW

#define LOOP_SLEEP_USEC                 1000
#define LOOP_MAX_TRIES                  100 // 100 * 1000usec = 100ms
// #define DEBUG

int lockAct(const char *actor, const char *func, int fd, struct flock *lck, short type, int fcntlType, int tries) {
  lck->l_type = type;
  int counter = 1;
  // Looping only makes sense with non-waiting lock (F_OFD_SETLK)
  while (1) {
    if (fcntl(fd, fcntlType, lck) >= 0) return counter;
    if (counter++ < tries && tries > 0) usleep(LOOP_SLEEP_USEC);
    else break;
  }
  return -1;
}

int sharedLock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_RDLCK, SHARED_LOCK_SET, LOOP_MAX_TRIES);
}

int sharedUnlock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, SHARED_LOCK_SET, LOOP_MAX_TRIES);
}

int exclusiveLock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_WRLCK, EXCLUSIVE_LOCK_SET, LOOP_MAX_TRIES);
}

int exclusiveLockWait(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_WRLCK, EXCLUSIVE_LOCK_SET_WAIT, 1);
}

int exclusiveLockOneTry(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_WRLCK, EXCLUSIVE_LOCK_SET, 1);
}

int exclusiveUnlock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, EXCLUSIVE_LOCK_SET, 1);
}

int reader__openMainFile() {
  return writer__openMainFile();
}

int writer__openMainFile() {
  return open(FILENAME_H1, O_RDWR | O_SYNC, 0664);
}

int reader__openWalFile() {
  return open(FILENAME_WAL, O_RDONLY | O_SYNC, 0664);
}

int writer__openWalFile() {
  return open(FILENAME_WAL, O_RDWR | O_SYNC, 0664);
}

int reader__openH2File() {
  return open(FILENAME_H2, O_RDONLY | O_SYNC, 0664);
}

int writer__openH2File() {
  return open(FILENAME_H2, O_RDWR | O_SYNC, 0664);
}

void closeFiles(int mainfd, int lockfd, int walfd) {
  if (lockfd != mainfd) close(lockfd);
  if (mainfd > 0) close(mainfd);
  if (walfd > 0) close(walfd);
}

void closeLockFileOnly(int mainfd, int lockfd) {
  if (lockfd != mainfd) close(lockfd);
}

void readHeaders(int fd, struct headers *hdr) {
  lseek(fd, (size_t)0, SEEK_SET);
  read(fd, hdr, sizeof(struct headers));
}

int readWalVersion(int walfd) {
  int version = 0;
  lseek(walfd, (size_t)0, SEEK_SET);
  read(walfd, &version, sizeof(version));
  return version;
}

int upgradeWalVersion(int walfd) {
  int version = readWalVersion(walfd);
  version++;
  lseek(walfd, (size_t)0, SEEK_SET);
  write(walfd, &version, sizeof(version));
  return version;
}

void truncateWal(int walVersion, int headerWalVersion) {
  int offset = 0;
  if (walVersion == headerWalVersion) {
    truncate(FILENAME_WAL, offset);
    return;
  }
  // NOTE: we don't have any data on WAL, so we just truncate always
  truncate(FILENAME_WAL, offset);
  // Implementation with data on WAL:
  //  1. Write all WAL records after headerWalVersion into FILENAME_WAL_TMP
  //  2. Rename/overwrite (gcc) FILENAME_WAL_TMP to FILENAME_WAL (or when needed delete FILENAME_WAL first)
}

void upgradeHeaderWalVersion(int mainfd, struct headers *hdr, int walVersion) {
  // we update only the old version wal version
  if (hdr->h1_is_current) {
    // h2 is old version
    hdr->h2_wal_version = walVersion;
    hdr->h1_wal_version = 0;
  } else {
    // h1 is old version
    hdr->h1_wal_version = walVersion;
    hdr->h2_wal_version = 0;
  }
  lseek(mainfd, (size_t)0, SEEK_SET);
	write(mainfd, hdr, sizeof(struct headers));
}

// ...
//     old 10.3    current 10.0    wal 3
// --- upgradeVersion ----
// current 13.0        old 13.0    wal 0
// current 13.0        old 13.1    wal 2
// current 13.0        old 13.3    wal 3
// --- upgradeVersion ----
//     old 16.0    current 16.0    wal 0
// ...
void upgradeVersion(int mainfd, struct headers *hdr) {
  int size = lseek(mainfd, (size_t)0, SEEK_END), newVersion = 0;
  if (size) assert(hdr->h1_version == hdr->h2_version);
  newVersion = hdr->h2_version + hdr->h1_is_current ? hdr->h2_wal_version : hdr->h1_wal_version;
  hdr->h1_wal_version = 0;
  hdr->h2_wal_version = 0;
  hdr->h1_version = newVersion;
  hdr->h2_version = newVersion;
  hdr->h1_is_current = !hdr->h1_is_current;
  lseek(mainfd, (size_t)0, SEEK_SET);
	write(mainfd, hdr, sizeof(struct headers));
}

int getCurrentVersion(struct headers *hdr) {
  assert(hdr->h1_version == hdr->h2_version);
  return hdr->h1_version;
}

char *getCurrentVersionStr(struct headers *hdr) {
  return hdr->h1_is_current ? "h1" : "h2";
}

long getUsecDiff(struct timeval *st, struct timeval *et) {
 return ((et->tv_sec - st->tv_sec) * 1000000) + (et->tv_usec - st->tv_usec);
}

char *getH1Status(struct headers *hdr) {
  return hdr->h1_is_current ? "x" : " ";
}

char *getH2Status(struct headers *hdr) {
  return hdr->h1_is_current ? " " : "x";
}

void __gettimeofday(struct timeval *et) {
  gettimeofday(et, NULL);
}

void __debugPrintStart(char *buf, int maxLen, const char *role, int tid, struct headers *hdr, long udiff, int tries) {
  char latencyBuf[256] = {0};
  int i, latency = udiff/1000 > 255 ? 255 : udiff/1000;
  for (i = 0; i<latency; i++) snprintf(latencyBuf+i, 255-i, "*");
  printf("(%5d ms) [%s%s] %s\n", udiff/1000, 
    hdr->h1_is_current ? "x" : " ", 
    hdr->h1_is_current ? " " : "x", 
    latencyBuf);
#ifdef DEBUG
  snprintf(buf, maxLen, "<-- [%3d] %s     [%s] h1:%4d.%-3d   [%s] h2:%4d.%-3d - %ld usec (%d)\n",
    tid, role, 
    getH1Status(hdr), hdr->h1_version, hdr->h1_wal_version, 
    getH2Status(hdr), hdr->h2_version, hdr->h2_wal_version,
    udiff, tries);
#endif // DEBUG
}

void __debugPrintEnd(char *buf, const char *role, int tid, struct headers *hdr, int walVersion) {
#ifdef DEBUG
  printf("%s    [%3d] %s     [%s] h1:%4d.%-3d   [%s] h2:%4d.%-3d",
    buf, tid, role,
    getH1Status(hdr), hdr->h1_version, hdr->h1_wal_version, 
    getH2Status(hdr), hdr->h2_version, hdr->h2_wal_version);
  if (walVersion > 0) printf("   wal(%d)", walVersion);
  printf("\n");
#endif // DEBUG
}

void __reader__waitWorkloadTime() {
  usleep(rand() % READER_READ_TIME_MAX_USEC);
}

void __reader__waitPauseTime() {
  usleep(rand() % READER_PAUSE_MAX_USEC);
}

void __writer__waitWorkloadTime() {
  usleep(rand() % WRITER_WRITE_TIME_MAX_USEC);
}

void __writer__waitWalUpdateTime() {
  usleep(rand() % WRITER_WRITE_TIME_MAX_USEC / 10);
}

void __writer__waitPauseTime() {
  usleep(rand() % WRITER_PAUSE_MAX_USEC);
}

bool ensureCorrectVersionLocked(int mainfd, int lockfd, struct headers *hdr) {
  // current is h1 ==> assert(mainfd == lockfd)
  // current is h2 ==> assert(mainfd != lockfd)
  bool ret = hdr->h1_is_current ? mainfd == lockfd : mainfd != lockfd;
  if (!ret) printf("wrong version...!\n");
  return ret;
}

bool isHeaderWalEven(struct headers *hdr, int walVersion) {
  assert(hdr->h1_version == hdr->h2_version);
  return hdr->h1_is_current 
    ? hdr->h2_wal_version == walVersion
    : hdr->h1_wal_version == walVersion;
}

bool isHeaderWalAboveThreshold(struct headers *hdr, int maxVersion) {
  assert(hdr->h1_version == hdr->h2_version);
  return hdr->h1_is_current
    ? hdr->h2_wal_version >= maxVersion
    : hdr->h1_wal_version >= maxVersion;
}

void dumpHeaders(struct headers *hdr) {
  printf("%d  %d.%d  %d.%d\n",
    hdr->h1_is_current, 
    hdr->h1_version, hdr->h1_wal_version,
    hdr->h2_version, hdr->h2_wal_version);
}
