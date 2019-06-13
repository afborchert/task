CC := $(CXX)
DEBUG := -g # -fprofile-arcs -ftest-coverage
THREADS := -pthread
CXXFLAGS := -Wfatal-errors -Wall -I. -Itpool -std=c++14 $(DEBUG) $(THREADS)
LDFLAGS := $(DEBUG) $(THREADS)
.PHONY:		all clean
all:		test_suite
test_suite.o:	test_suite.cpp task.hpp tpool/thread_pool.hpp

clean:
		rm -f test_suite test_suite.o *.gcov gmon.out *.gcno *.gcda core
