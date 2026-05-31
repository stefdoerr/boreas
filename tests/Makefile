CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I../plugins/Boreas/dsp

SRCS := $(wildcard test_*.cpp)
BINS := $(SRCS:.cpp=)

test: $(BINS)
	@fail=0; for b in $(BINS); do echo "== $$b =="; ./$$b || fail=1; done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"

%: %.cpp test_util.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: test clean
