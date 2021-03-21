# MVCC lock tester

This code uses Linux [Open File Descriptor Locks](https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html) on two separate files to implement [Multiversion Concurrency Control (MVCC)](https://en.wikipedia.org/wiki/Multiversion_concurrency_control).

The main purpose of this repository is to play with the MVCC locking schemes, like concurrent readers with a single writer.

## Design (DRAFT, WIP)

Please see the [PDF of slides sketching the solution](doc/Switcher%20role%20for%20concurrent%20Readers%20and%20a%20Single%20Writer%20MVCC.pdf). Comments, collaboration, and any input is most welcome!

## Files

The main "db" file is `test.mybdb` and the second file used for the another "version" is `test.mydb.h2`. The main file contains two headers with counters that are incremented every time the version is upgraded. A WAL file is used (`test.mybdb.WAL`) if writer does not get the lock with a number of tries. The WAL file only contains an increasing WAL version counter, which is reset whenever the writer gets "full lock" and does a snapshot (i.e. merges WAL to the main db).

## Running the code

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
