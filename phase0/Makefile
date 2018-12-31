CFLAGS=-std=c89 -pedantic -Wall -Wextra -Wconversion -ftrapv -Wfloat-equal
LDLIBS=

.PHONY: all clean

all: compress

compress: compress.o frame.o

compress.o: compress.c utils.h

frame.o: frame.c frame.h utils.h
