CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal -g -march=native -O3 $(EXTRA_CFLAGS)
LDFLAGS=-g -rdynamic $(EXTRA_LDFLAGS)
LDLIBS=$(EXTRA_LDLIBS)

.PHONY: all clean distclean

all: compress perftest

compress: compress.o frame.o dwt.o dwtfloat.o dwtint.o common.o

compress.o: compress.c common.h config.h

frame.o: frame.c frame.h common.h

dwt.o: dwt.c frame.h common.h config.h

dwtfloat.o: dwtfloat.c frame.h common.h config.h

dwtint.o: dwtint.c frame.h common.h config.h

perftest: perftest.o frame.o dwt.o dwtfloat.o dwtint.o common.o

perftest.o: perftest.c common.h config.h frame.h dwt.h

common.o: common.c common.h

clean:
	$(RM) -- *.o compress perftest

distclean: clean
	$(RM) -- *.gcda
