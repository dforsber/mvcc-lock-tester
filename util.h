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
int reader__openWalFile(void);
int reader__openH2File(void);

int writer__openMainFile(void);
int writer__openWalFile(void);
int writer__openH2File(void);

void closeFiles(int lockfd, int mainfd, int walfd);
void closeLockFileOnly(int mainfd, int lockfd);

void readHeaders(int fd, struct headers *hdr);
void upgradeVersion(int fd, struct headers *hdr);
void upgradeHeaderWalVersion(int fd, struct headers *hdr, int walVersion);
int upgradeWalVersion(int walfd);
int readWalVersion(int walfd);
void truncateWal(int walVersion, int headerWalVersion);

int getCurrentVersion(struct headers *hdr);
char *getCurrentVersionStr(struct headers *hdr);
long getUsecDiff(struct timeval *st, struct timeval *et);

void __gettimeofday(struct timeval *et);
void __debugPrintStart(char *buf, int maxLen, const char *role, int tid, struct headers *hdr, long int udiff, int tries);
void __debugPrintEnd(char *buf, const char *role, int tid, struct headers *hdr, int walVersion);

void __reader__waitWorkloadTime(void);
void __reader__waitPauseTime(void);

void __writer__waitWorkloadTime(void);
void __writer__waitPauseTime(void);
void __writer__waitWalUpdateTime(void);

bool ensureCorrectVersionLocked(int mainfd, int lockfd, struct headers *hdr);
bool isHeaderWalEven(struct headers *hdr, int walVersion);
bool isHeaderWalAboveThreshold(struct headers *hdr, int maxVersion);

void dumpHeaders(struct headers *hdr);

#endif // __UTIL_H__