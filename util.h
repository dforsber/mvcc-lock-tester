int sharedLock(const char *actor, int fd, struct flock *lck);
int sharedUnlock(const char *actor, int fd, struct flock *lck);
int exclusiveLock(const char *actor, int fd, struct flock *lck);
int exclusiveUnlock(const char *actor, int fd, struct flock *lck);

void readHeaders(int fd, struct headers *hdr);
void writeHeaders(int fd, struct headers *hdr);

void upgradeVersion(int fd, struct headers *hdr);
