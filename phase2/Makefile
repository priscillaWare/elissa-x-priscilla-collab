PREFIX = /root

CC = gcc

# Collect non-test source files in the current directory.
CSRCS = $(wildcard *.c)
# Exclude test files and our special objects.
COBJS = $(filter-out phase2.o phase2_common_testcase_code.o, $(CSRCS:.c=.o))

LIBS = -lusloss4.7 -lphase1

LIB_DIR     = $(PREFIX)/lib
INCLUDE_DIR = $(PREFIX)/include

CFLAGS = -Wall -g -I$(INCLUDE_DIR) -I.
LDFLAGS = -Wl,--start-group -L$(LIB_DIR) -L. $(LIBS) -Wl,--end-group

# List of test executables we want to build.
TESTS = test25 test26 test28 test29 test33 test34 


all: $(TESTS)

# Build each test executable from its corresponding source file in testcases/
%: testcases/%.c phase2_common_testcase_code.o phase2.o $(COBJS) libphase1.a
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Explicit rule for phase2.o (depends on phase2.h too)
phase2.o: phase2.c phase2.h
	$(CC) $(CFLAGS) -c phase2.c -o phase2.o

# Explicit rule for the common testcase object file, from testcases/phase2_common_testcase_code.c
phase2_common_testcase_code.o: testcases/phase2_common_testcase_code.c phase2.h 
	$(CC) $(CFLAGS) -c testcases/phase2_common_testcase_code.c -o phase2_common_testcase_code.o

clean:
	rm -f *.o $(TESTS) term[0-3].out
