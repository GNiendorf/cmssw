# Chain-tracking offline prototype (plan section 10).
# Plain ROOT only: no rooutil, no Alpaka, no LSTCore link dependency.

EXE = bin/chainproto

SOURCES = $(wildcard *.cc)
OBJECTS = $(SOURCES:.cc=.o)
HEADERS = $(wildcard *.h)

CXX        = g++
ROOTCFLAGS = $(shell root-config --cflags)
ROOTLIBS   = $(shell root-config --libs)
# -std=c++17 must come after ROOTCFLAGS: root-config injects -std=c++20 and the last flag wins.
CXXFLAGS   = $(ROOTCFLAGS) -O2 -g -std=c++17 -Wall
LDFLAGS    = -O2 -g

all: $(EXE)

$(EXE): $(OBJECTS)
	mkdir -p bin
	$(CXX) $(LDFLAGS) $(OBJECTS) $(ROOTLIBS) -o $@

%.o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXE)

.PHONY: all clean
