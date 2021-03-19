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

#include "test_locking.h"
#include "util.h"

#define SHARED_LOCK_SET                 F_OFD_SETLKW
#define EXCLUSIVE_LOCK_SET              F_OFD_SETLK
#define EXCLUSIVE_LOCK_SET_WAIT         F_OFD_SETLKW

#define LOOP_SLEEP_USEC                 1000
#define LOOP_MAX_TRIES                  100 // 100 * 1000usec = 100ms
// #define DEBUG

int lockAct(const char *actor, const char *func, int fd, struct flock *lck, short type, int fcntlType) {
  lck->l_type = type;
  int counter = 0;
  // Looping only makes sense with non-waiting lock (F_OFD_SETLK)
  while (counter++ < LOOP_MAX_TRIES) {
    if (fcntl(fd, fcntlType, lck) >= 0) return counter;
#ifdef DEBUG
    printf("\t%s: %s failed: %d %s\n", actor, func, errno, strerror(errno));
#endif // DEBUG
    usleep(LOOP_SLEEP_USEC);
  }
  return -1;
}

int sharedLock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_RDLCK, SHARED_LOCK_SET);
}

int sharedUnlock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, SHARED_LOCK_SET);
}

int exclusiveLock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_WRLCK, EXCLUSIVE_LOCK_SET);
}

int exclusiveUnlock(const char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, EXCLUSIVE_LOCK_SET);
}

void readHeaders(int fd, struct headers *hdr) {
  read(fd, hdr, sizeof(struct headers));
}

int readWal() {
  int fd = open(FILENAME_WAL, O_RDONLY, 0644);
  int version = 0;
  read(fd, &version, sizeof(version));
  close(fd);
  return version;
}

int upgradeWal() {
  int fd = open(FILENAME_WAL, O_RDWR | O_CREAT, 0644);
  int size = lseek(fd, (size_t)0, SEEK_END);
  lseek(fd, (size_t)0, SEEK_SET);
  int version = 0;
  if (size > 0) read(fd, &version, sizeof(int));
  version++;
  lseek(fd, (size_t)0, SEEK_SET);
  int res = write(fd, &version, sizeof(int));
  sync();
  close(fd);
  return version;
}

void truncateWal() {
  truncate(FILENAME_WAL, 0);
}

void upgradeVersion(int fd, struct headers *hdr) {
  if (hdr->h1_version > hdr->h2_version) {
    hdr->h2_version = hdr->h1_version + 1;
  } else {
    hdr->h1_version = hdr->h2_version + 1;
  }
  lseek(fd, (size_t)0, SEEK_SET);
	int ret = write(fd, hdr, sizeof(struct headers));
	fsync(fd);
}
