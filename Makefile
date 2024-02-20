CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Werror -Wextra

.PHONY: all
all: my-shell

my-shell: my-shell.o helper.o

my-shell.o: my-shell.c helper.h

helper.o: helper.c helper.h

.PHONY: clean
clean:
	rm -f *.o my-shell
