LIBS=-lpthread

.PHONY: clean run

all: build

clean:
	rm -rf *.o test_locking

run: build test_locking
	./test_locking

build: test_locking.o util.o
	gcc ${LIBS} -o test_locking test_locking.o util.o

test_locking.o: test_locking.c
	gcc -c test_locking.c

util.o: util.c
	gcc -c util.c

loop: build
	while :; do ./test_locking ; sleep 0.1; done