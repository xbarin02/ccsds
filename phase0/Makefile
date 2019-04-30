CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal -g -march=native -O3 -DNDEBUG $(EXTRA_CFLAGS)
LDFLAGS=-g -rdynamic $(EXTRA_LDFLAGS)
LDLIBS=$(EXTRA_LDLIBS)
TARGETS=compress perftest

-include Makefile.local

.PHONY: all clean distclean

all: $(TARGETS)

compress: compress.o frame.o dwt.o dwtfloat.o dwtint.o common.o bio.o bpe.o

compress.o: compress.c common.h config.h

frame.o: frame.c frame.h common.h

dwt.o: dwt.c frame.h common.h config.h

dwtfloat.o: dwtfloat.c dwtfloat.h frame.h common.h config.h

dwtint.o: dwtint.c dwtint.h frame.h common.h config.h

perftest: perftest.o frame.o dwt.o dwtfloat.o dwtint.o common.o

perftest.o: perftest.c common.h config.h frame.h dwt.h

common.o: common.c common.h

bio.o: bio.c bio.h common.h

bpe.o: bpe.c bpe.h frame.h common.h bio.h

clean:
	$(RM) -- *.o $(TARGETS)

distclean: clean
	$(RM) -- *.gcda
