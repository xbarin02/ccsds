CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal
LDLIBS=

.PHONY: all clean

all: compress

compress: compress.o frame.o dwt.o

compress.o: compress.c utils.h config.h

frame.o: frame.c frame.h utils.h

dwt.o: dwt.c frame.h common.h utils.h config.h
