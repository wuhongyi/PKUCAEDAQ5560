CXX ?= g++
ROOT_CONFIG ?= root-config
CXXFLAGS ?= -O2 -Wall -Wextra
ROOT_CFLAGS = $(shell $(ROOT_CONFIG) --cflags)
ROOT_LIBS = $(shell $(ROOT_CONFIG) --libs)

all: decodeandsort DecodeAndSortAll

DecodeAndSortAll: DecodeAndSortAll.cc R5560Decode.cc R5560Decode.hh Sort.hh WindowSort.hh UserDefine.hh nlohmann/json.hpp
	$(CXX) $(CXXFLAGS) $(ROOT_CFLAGS) -std=c++17 -I. DecodeAndSortAll.cc R5560Decode.cc -o $@ $(ROOT_LIBS)

decodeandsort: DecodeAndSortAll
	cp DecodeAndSortAll decodeandsort

test_decode: tests/test_decode.cc R5560Decode.cc R5560Decode.hh Sort.hh nlohmann/json.hpp
	$(CXX) $(CXXFLAGS) -std=c++17 -I. tests/test_decode.cc R5560Decode.cc -o $@

test_window_sort: tests/test_window_sort.cc R5560Decode.hh Sort.hh WindowSort.hh nlohmann/json.hpp
	$(CXX) $(CXXFLAGS) -std=c++17 -I. tests/test_window_sort.cc -o $@

test: test_decode test_window_sort
	./test_decode
	./test_window_sort

check_root: tests/check_root.cc nlohmann/json.hpp
	$(CXX) $(CXXFLAGS) $(ROOT_CFLAGS) -std=c++17 -I. tests/check_root.cc -o $@ $(ROOT_LIBS)

clean:
	rm -f DecodeAndSortAll decodeandsort test_decode test_window_sort check_root

.PHONY: all test clean
