# MVCC lock tester

This code uses Linux [Open File Descriptor Locks](https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html) on two separate files to implement [Multiversion Concurrency Control (MVCC)](https://en.wikipedia.org/wiki/Multiversion_concurrency_control).

The **main goal** of this repository is to play with the MVCC locking scheme (educational) for distributed systems with shared storage, like concurrent readers with a single writer, a model adopted in [DuckDB](https://github.com/cwida/duckdb), and eventually see DuckDB support *concurrent* Readers and a Single Writer in an efficient and OLAP friendly way. Maybe even find a solution for multiple Writers too.

**Secondary goal** is to write a tester that can be run over network filesystems, like [Amazon AWS EFS](https://aws.amazon.com/efs/) that supports 512 locks per file (as of writing this), to see how performant the solution would be and whether they have any issues with this particular locking scheme - and if issues like eventual consistency arise, we can tune the solution towards the case where it is working regardless. This is to bring the knowledge of current network filesystems up to date and address the decade old phrase of "do not run databases over network filesystems as the locking does not work". Anyway, in cloud (no big timeouts, right..) and e.g. with SANs and with advisory locking in use, this phrase is probably outdated and needs to be challenged.

## Design

Please see the [PDF of slides sketching the solution](doc/Switcher%20role%20for%20concurrent%20Readers%20and%20a%20Single%20Writer%20MVCC.pdf). Comments, collaboration, and any input is most welcome! This work is in progress and currently being progressed in my free time.

### Version history for design

- 2020-03-21 initial version
- 2020-03-21 v2: Added motivation and goals, Switcher role is the back step, not normal
- 2020-03-21 v3: Clarifications, "Improvements and Alternatives" slide (last)
- 2020-03-21 v4: Clarifications and alignment between slides
- 2020-03-25 v5: Pseudo code for Readers, Writer, and "Forced Version Upgrade" Role
- 2020-03-29 v6: Remove unnecessary file close/open calls
- 2020-03-29 v7: Initial non-DRAFT version and matching code

## Files

The main "db" file is `test.mybdb` and the second file used for the another "version" is `test.mydb.h2`. The main file contains two headers with counters that are incremented every time the version is upgraded. A WAL file is used (`test.mybdb.WAL`) as well. The WAL file only contains an increasing WAL version counter. See the docs for deeper level solution description.

## Running the code

You can check the outputs from the reader and writer by starting them at different terminals, but on the same directory so that they use the same database files.

> The settings are configured for high contention, i.e. there are continuous multiple readers reading and a single writer continuously writing. The maximum WAL version is small to get "forced version upgrades".

First compile the code, bootstrap the db files, and start the writer. You can see the writer doing snapshots for every write as it gets the exclusive lock when there are no readers.

```shell
docker-compose run tester /bin/bash
% make
% make bootstrapdb
% ./test_locking writer
```

Then you can start the concurrent readers process that launches N threads of readers that run varying sizes of workloads (keeping the lock).

```shell
docker-compose run tester /bin/bash
% ./test_locking reader
```

If you compile the code by defining `DEBUG`, then you will get more debug output.
