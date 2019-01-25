CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal -g -march=native -O3
LDFLAGS=-g -rdynamic
LDLIBS=

.PHONY: all clean

all: compress perftest

compress: compress.o frame.o dwt.o

compress.o: compress.c utils.h config.h

frame.o: frame.c frame.h utils.h

dwt.o: dwt.c frame.h common.h utils.h config.h

perftest: perftest.o frame.o dwt.o

clean:
	$(RM) -- *.o compress perftest
