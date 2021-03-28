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
#ifdef DEBUG
    printf("\t%s: %s failed: %d %s\n", actor, func, errno, strerror(errno));
#endif // DEBUG
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

int reader__openLockFileCurrentVersion(struct headers *hdr, int mainfd) {
 return hdr->h1_is_current ? mainfd : open(FILENAME_H2, O_RDONLY, 0666);
}

int reader__openLockFileOldVersion(struct headers *hdr, int mainfd) {
 return !hdr->h1_is_current ? mainfd : open(FILENAME_H2, O_RDONLY, 0666);
}

int writer__openLockFileCurrentVersion(struct headers *hdr, int mainfd) {
 return hdr->h1_is_current ? mainfd : open(FILENAME_H2, O_RDWR | O_SYNC, 0666);
}

int writer__openLockFileOldVersion(struct headers *hdr, int mainfd) {
 return !hdr->h1_is_current ? mainfd : open(FILENAME_H2, O_RDWR | O_SYNC, 0666);
}

int reader__openMainFile() {
  return open(FILENAME_H1, O_RDONLY, 0666);
}

int writer__openMainFile() {
  return open(FILENAME_H1, O_RDWR | O_SYNC, 0666);
}

void closeFiles(int mainfd, int lockfd) {
  if (lockfd != mainfd) close(lockfd);
  close(mainfd);
}

void readHeaders(int fd, struct headers *hdr) {
  int size = lseek(fd, (size_t)0, SEEK_END);
  if (size < sizeof(struct headers)) {
    printf("SIZE 0 - bootstrapping?\n");  
    return;
  }
  lseek(fd, (size_t)0, SEEK_SET);
  read(fd, hdr, sizeof(struct headers));
}

int readWalVersion() {
  int fd = open(FILENAME_WAL, O_RDONLY, 0644);
  int version = 0;
  int size = lseek(fd, (size_t)0, SEEK_END);
  if (size <= 0) return 0;
  lseek(fd, (size_t)0, SEEK_SET);
  read(fd, &version, sizeof(version));
  close(fd);
  return version;
}

int upgradeWalVersion() {
  int fd = open(FILENAME_WAL, O_RDWR | O_CREAT | O_SYNC, 0644);
  int size = lseek(fd, (size_t)0, SEEK_END);
  lseek(fd, (size_t)0, SEEK_SET);
  int version = 0;
  if (size > 0) read(fd, &version, sizeof(int));
  version++;
  lseek(fd, (size_t)0, SEEK_SET);
  int res = write(fd, &version, sizeof(int));
  close(fd);
  return version;
}

void truncateWal() {
  truncate(FILENAME_WAL, 0);
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
	int ret = write(mainfd, hdr, sizeof(struct headers));
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
void upgradeVersion(int mainfd, struct headers *hdr, int walVersion) {
  int size = lseek(mainfd, (size_t)0, SEEK_END);
  if (size) assert(hdr->h1_version == hdr->h2_version);
  if (size) assert((hdr->h1_is_current ? hdr->h2_wal_version : hdr->h1_wal_version) == walVersion);
  hdr->h1_wal_version = 0;
  hdr->h2_wal_version = 0;
  hdr->h1_version = hdr->h2_version + walVersion;
  hdr->h2_version = hdr->h1_version;
  hdr->h1_is_current = !hdr->h1_is_current;
  lseek(mainfd, (size_t)0, SEEK_SET);
	int ret = write(mainfd, hdr, sizeof(struct headers));
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

void __debugPrintStart(char *buf, int maxLen, const char *role, int tid, struct headers *hdr, long udiff, int tries) {
  snprintf(buf, maxLen, "<-- [%3d] %s     [%s] h1:%d.%-2d   [%s] h2:%d.%-2d - %ld usec (%d)\n",
    tid, role, 
    getH1Status(hdr), hdr->h1_version, hdr->h1_wal_version, 
    getH2Status(hdr), hdr->h2_version, hdr->h2_wal_version,
    udiff, tries);
}

void __debugPrintEnd(char *buf, const char *role, int tid, struct headers *hdr, int walVersion) {
  printf("%s    [%3d] %s     [%s] h1:%d.%-2d   [%s] h2:%d.%-2d",
    buf, tid, role,
    getH1Status(hdr), hdr->h1_version, hdr->h1_wal_version, 
    getH2Status(hdr), hdr->h2_version, hdr->h2_wal_version);
  if (walVersion >= 0) printf("   wal(%d)", walVersion);
  printf("\n");
}

void reader__waitWorkloadTime() {
  usleep(rand() % READER_READ_TIME_MAX_USEC);
}

void reader__waitPauseTime() {
  usleep(rand() % READER_PAUSE_MAX_USEC);
}

void writer__waitWorkloadTime() {
  usleep(rand() % WRITER_WRITE_TIME_MAX_USEC);
}

void writer__waitWalUpdateTime() {
  usleep(rand() % WRITER_WRITE_TIME_MAX_USEC / 10);
}

void writer__waitPauseTime() {
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
