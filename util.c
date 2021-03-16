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

#define LOOP_SLEEP_USEC 10
#define LOOP_MAX_TRIES 100000
// #define DEBUG

int lockAct(char *actor, const char *func, int fd, struct flock *lck, short type, int fcntlType) {
  lck->l_type = type;
  int counter = 0;
  while (counter++ < LOOP_MAX_TRIES) {
    if (fcntl(fd, fcntlType, lck) >= 0) return counter;
#ifdef DEBUG
    printf("\t%s: %s failed: %d %s\n", actor, func, errno, strerror(errno));
#endif // DEBUG
    usleep(LOOP_SLEEP_USEC);
  }
  return -1;
}

int sharedLock(char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_RDLCK, F_OFD_SETLK);
}

int sharedUnlock(char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, F_OFD_SETLK);
}

int exclusiveLock(char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_WRLCK, F_OFD_SETLKW);
}

int exclusiveUnlock(char *actor, int fd, struct flock *lck) {
  return lockAct(actor, __func__, fd, lck, F_UNLCK, F_OFD_SETLKW);
}

void readHeaders(int fd, struct headers *hdr) {
  read(fd, hdr, sizeof(struct headers));
}

void writeHeaders(int fd, struct headers *hdr) {
  write(fd, hdr, sizeof(struct headers));
}

void upgradeVersion(int fd, struct headers *hdr) {
  if (hdr->h1_version > hdr->h2_version) {
    hdr->h2_version = hdr->h1_version + 1;
  } else {
    hdr->h1_version = hdr->h2_version + 1;
  }
  lseek(fd, 0, SEEK_SET);
	int ret = write(fd, hdr, sizeof(struct headers));
	fsync(fd);
}
