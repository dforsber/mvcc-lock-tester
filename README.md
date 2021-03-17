# MVCC lock tester

This code uses Linux [Open File Descriptor Locks](https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html) on two separate files to implement [Multiversion Concurrency Control (MVCC)](https://en.wikipedia.org/wiki/Multiversion_concurrency_control). The main file is `test.mybdb` and the second file used for the another "version" is `test.mydb.h2`. The main file contains two headers with counters that are incremented every time the version is upgraded.

The main purpose of this repository is to play with the MVCC locking schemas, like concurrent readers with a single writer.

You can check the outputs from the reader and writer by starting them at different terminals.

```shell
docker-compose run tester /bin/bash
% make
% ./test_locking reader
```

```shell
docker-compose run tester /bin/bash
% make
% ./test_locking writer
```
