CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal -g -march=native -O3 -DNDEBUG $(EXTRA_CFLAGS)
LDFLAGS=-g -rdynamic $(EXTRA_LDFLAGS)
LDLIBS=$(EXTRA_LDLIBS)
TARGETS=compress perftest perftest2

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

perftest2: perftest2.o frame.o dwt.o dwtfloat.o dwtint.o common.o bpe.o bio.o

perftest2.o: perftest2.c common.h config.h frame.h dwt.h bpe.h bio.h

common.o: common.c common.h

bio.o: bio.c bio.h common.h

bpe.o: bpe.c bpe.h frame.h common.h bio.h

biotest: biotest.o bio.o common.o

biotest.o: biotest.c common.h bio.h

pgm2h: pgm2h.o common.o frame.o

pgm2h.o: pgm2h.c common.h frame.h

aquas: aquas.o common.o frame.o dwt.o dwtfloat.o dwtint.o bio.o bpe.o

aquas.o: aquas.c config.h common.h frame.h dwt.h bio.h bpe.h Lenna256.h

clean:
	$(RM) -- *.o $(TARGETS)

distclean: clean
	$(RM) -- *.gcda
