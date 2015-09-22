CFLAGS=-ggdb3 -O3 -Wall --std=gnu99
CC=gcc
AR=ar

.PHONY: all
all: static shared

.PHONY: static
static:
	$(CC) $(CFLAGS) -c uslab.c -o uslab.o
	ar rcs libuslab.a uslab.o
	rm uslab.o

.PHONY: shared
shared:
	$(CC) $(CFLAGS) -fPIC -c uslab.c -o uslab.o
	$(CC) -shared -o libuslab.so uslab.o
	rm uslab.o

.PHONY: clean
clean:
	rm -f libuslab.a libuslab.so uslab.o uslab_bench uslab_test

uslab_bench: static
	$(CC) $(CFLAGS) uslab_bench.c -o uslab_bench -Ijemalloc/include -Ljemalloc/lib -L. -luslab -ljemalloc -lpthread -static

uslab_test: static
	$(CC) $(CFLAGS) uslab_test.c tap.c -o uslab_test -L. -luslab -lpthread -static
