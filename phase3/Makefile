PREFIX = /root

CC = gcc

CSRCS = $(wildcard *.c)
COBJS = $(CSRCS:.c=.o)

LIBS = -lusloss4.7 -lphase1 -lphase2

LIB_DIR     = ${PREFIX}/lib
INCLUDE_DIR = ${PREFIX}/include

CFLAGS = -Wall -g -I${INCLUDE_DIR} -I.
LDFLAGS = -Wl,--start-group -L${LIB_DIR} -L. ${LIBS} -Wl,--end-group



VPATH = testcases
TESTS = test07 test20 test26 test27



all: ${TESTS}

${TESTS}: phase3_common_testcase_code.o $(COBJS) libphase1.a libphase2.a

ARCH=$(shell uname | tr '[:upper:]' '[:lower:]')-$(shell uname -p | sed -e "s/aarch/arm/g")

phase3_no_debug_symbols-${ARCH}.o: phase3.c
	gcc -I${INCLUDE_DIR} -I. -c phase3.c -o phase3_no_debug_symbols-${ARCH}.o

phase3_usermode_no_debug_symbols-${ARCH}.o: phase3_usermode.c
	gcc -I${INCLUDE_DIR} -I. -c phase3_usermode.c -o phase3_usermode_no_debug_symbols-${ARCH}.o

libphase3-${ARCH}.a: phase3_no_debug_symbols-${ARCH}.o phase3_usermode_no_debug_symbols-${ARCH}.o
	-rm $@
	ar -r $@ $^

clean:
	-rm *.o ${TESTS} term[0-3].out

