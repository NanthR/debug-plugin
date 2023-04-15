CXX = g++
# CXX = aarch64-linux-gnu-g++
CXXFLAGS = -std=c++20 -Wall -fno-rtti -O2
CXXFLAGS += -Wno-literal-suffix

PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
CXXFLAGS += -I$(PLUGINDIR)/include

all: build/print.so check

build/print.so: build/print.o
	$(CXX) $(LDFLAGS) -g -std=c++20 -shared -o $@ $<

build/print.o : print.cpp
	$(CXX) $(CXXFLAGS) -g -fPIC -c -o $@ $<

clean:
	rm -f build/print.o build/print.so build/pragma

check: testing/pragma.c
	gcc -g -fplugin=./build/print.so -fdump-tree-original -O3 $< -o build/pragma

.PHONY: all clean check
