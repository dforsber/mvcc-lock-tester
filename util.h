#include <stdbool.h>

#define FILENAME_H1                     "test.mydb"
#define FILENAME_H2                     "test.mydb.h2"
#define FILENAME_WAL                    "test.mydb.WAL"

#define READER_PAUSE_MAX_USEC           1000000 // 1s
#define READER_READ_TIME_MAX_USEC       3000000 // 3s
#define WRITER_PAUSE_MAX_USEC           1000000 // 1s
#define WRITER_WRITE_TIME_MAX_USEC      3000000 // 3s

int sharedLock(const char *actor, int fd, struct flock *lck);
int sharedUnlock(const char *actor, int fd, struct flock *lck);
int exclusiveLock(const char *actor, int fd, struct flock *lck);
int exclusiveLockOneTry(const char *actor, int fd, struct flock *lck);
int exclusiveUnlock(const char *actor, int fd, struct flock *lck);

int openLockFile(struct headers *hdr, int mainfd);
void closeFiles(int lockfd, int mainfd);
int openMainFile(void);

void readHeaders(int fd, struct headers *hdr);
void upgradeVersion(int fd, struct headers *hdr);
void upgradeVersionWal(int fd, struct headers *hdr, int walVersion);
int upgradeWal();
int readWal();
void truncateWal();

int getCurrentVersion(struct headers *hdr);
char *getCurrentVersionStr(struct headers *hdr);
long getUsecDiff(struct timeval *st, struct timeval *et);

void __debugPrintStart(char *buf, int maxLen, char *role, int tid, struct headers *hdr, long int udiff, int tries);
void __debugPrintEnd(char *buf, char *role, int tid, struct headers *hdr);

void waitReadTime(void);
void waitReaderPauseTime(void);

bool ensureCorrectVersionLocked(int mainfd, int lockfd, struct headers *hdr);
void forcedVersionUpgradeCheck(int mainfd, struct headers *hdr);