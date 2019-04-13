CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal -g -march=native -O3 $(EXTRA_CFLAGS)
LDFLAGS=-g -rdynamic $(EXTRA_LDFLAGS)
LDLIBS=$(EXTRA_LDLIBS)

.PHONY: all clean distclean

all: compress perftest

compress: compress.o frame.o dwt.o dwtfloat.o dwtint.o

compress.o: compress.c utils.h config.h

frame.o: frame.c frame.h utils.h

dwt.o: dwt.c frame.h common.h utils.h config.h

dwtfloat.o: dwtfloat.c frame.h common.h utils.h config.h

dwtint.o: dwtint.c frame.h common.h utils.h config.h

perftest: perftest.o frame.o dwt.o dwtfloat.o dwtint.o

clean:
	$(RM) -- *.o compress perftest

distclean: clean
	$(RM) -- *.gcda
