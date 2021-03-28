#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdbool.h>
#include "test_locking.h"

int sharedLock(const char *actor, int fd, struct flock *lck);
int sharedUnlock(const char *actor, int fd, struct flock *lck);
int exclusiveLock(const char *actor, int fd, struct flock *lck);
int exclusiveLockWait(const char *actor, int fd, struct flock *lck);
int exclusiveLockOneTry(const char *actor, int fd, struct flock *lck);
int exclusiveUnlock(const char *actor, int fd, struct flock *lck);

int reader__openMainFile(void);
int reader__openLockFileCurrentVersion(struct headers *hdr, int mainfd);
int reader__openLockFileOldVersion(struct headers *hdr, int mainfd);
int writer__openMainFile(void);
int writer__openLockFileCurrentVersion(struct headers *hdr, int mainfd);
int writer__openLockFileOldVersion(struct headers *hdr, int mainfd);
void closeFiles(int lockfd, int mainfd);

void readHeaders(int fd, struct headers *hdr);
void upgradeVersion(int fd, struct headers *hdr, int walVersion);
void upgradeHeaderWalVersion(int fd, struct headers *hdr, int walVersion);
int upgradeWalVersion(void);
int readWalVersion(void);
void truncateWal(void);

int getCurrentVersion(struct headers *hdr);
char *getCurrentVersionStr(struct headers *hdr);
long getUsecDiff(struct timeval *st, struct timeval *et);

void __debugPrintStart(char *buf, int maxLen, const char *role, int tid, struct headers *hdr, long int udiff, int tries);
void __debugPrintEnd(char *buf, const char *role, int tid, struct headers *hdr, int walVersion);

void reader__waitWorkloadTime(void);
void reader__waitPauseTime(void);

void writer__waitWorkloadTime(void);
void writer__waitPauseTime(void);
void writer__waitWalUpdateTime(void);

bool ensureCorrectVersionLocked(int mainfd, int lockfd, struct headers *hdr);
bool isHeaderWalEven(struct headers *hdr, int walVersion);
bool isHeaderWalAboveThreshold(struct headers *hdr, int maxVersion);

void dumpHeaders(struct headers *hdr);

#endif // __UTIL_H__