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
TESTS = test00 test01 test02 test03 test04 test05 test06 test07 test08 test09 test10 \
        test11 test12 test13 test14 test15 test16 test17 test18 test19 test20 test21 \
		test22 test23 test24 test25 test38 test39 test40 test45


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
