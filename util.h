#define FILENAME_H1                     "test.mydb"
#define FILENAME_H2                     "test.mydb.h2"
#define FILENAME_WAL                    "test.mydb.WAL"

int sharedLock(const char *actor, int fd, struct flock *lck);
int sharedUnlock(const char *actor, int fd, struct flock *lck);
int exclusiveLock(const char *actor, int fd, struct flock *lck);
int exclusiveUnlock(const char *actor, int fd, struct flock *lck);

void readHeaders(int fd, struct headers *hdr);
void upgradeVersion(int fd, struct headers *hdr);
int upgradeWal();
int readWal();
void truncateWal();
