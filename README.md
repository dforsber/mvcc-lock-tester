# MVCC lock tester

This code uses Linux [Open File Descriptor Locks](https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html) on two separate files to implement [Multiversion Concurrency Control (MVCC)](https://en.wikipedia.org/wiki/Multiversion_concurrency_control).

The **main goal** of this repository is to play with the MVCC locking scheme (educational), like concurrent readers with a single writer, a model adopted in [DuckDB](https://github.com/cwida/duckdb), and eventually see DuckDB support *concurrent* Readers and a Single Writer in an efficient and OLAP friendly way. Maybe even find a solution for multiple Writers too.

**Secondary goal** is to write a tester that can be run over network filesystems, like [Amazon AWS EFS](https://aws.amazon.com/efs/) that supports 512 locks per file (as of writing this), to see how performant the solution would be and whether they have any issues with this particular locking scheme - and if issues like eventual consistency arise, we can tune the solution towards the case where it is working regardless. This is to bring the knowledge of current network filesystems up to date and address the decade old phrase of "do not run databases over network filesystems as the locking does not work".

## Design (DRAFT, WIP)

Please see the [PDF of slides sketching the solution](doc/Switcher%20role%20for%20concurrent%20Readers%20and%20a%20Single%20Writer%20MVCC.pdf). Comments, collaboration, and any input is most welcome! This work is in progress and currently being progressed in my free time.

## Files

The main "db" file is `test.mybdb` and the second file used for the another "version" is `test.mydb.h2`. The main file contains two headers with counters that are incremented every time the version is upgraded. A WAL file is used (`test.mybdb.WAL`) if writer does not get the lock with a number of tries. The WAL file only contains an increasing WAL version counter.

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
