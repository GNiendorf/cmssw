# P2.2 parity reference dumper. Compiles the FROZEN prototype translation units
# (standalone/prototype/, never modified) plus one driver, with the prototype's own flags.
PROTO      = ../prototype

EXE        = bin/p22_ref_dump
PROTO_SRCS = $(PROTO)/NtupleReader.cc $(PROTO)/Stages.cc $(PROTO)/Features.cc $(PROTO)/EdgeInference.cc \
             $(PROTO)/K6Weld.cc $(PROTO)/Trim.cc $(PROTO)/ChainFeatures.cc $(PROTO)/ChainInference.cc \
             $(PROTO)/PixelAttach.cc $(PROTO)/AttachInference.cc
SRCS       = p22_ref_dump.cc

CXX        = g++
ROOTCFLAGS = $(shell root-config --cflags)
ROOTLIBS   = $(shell root-config --libs)
# Same flag set as prototype/Makefile: -std=c++17 must follow ROOTCFLAGS (root-config injects
# -std=c++20 and the last one wins), and -O2 with no fast-math, so the arithmetic here is
# bit-for-bit the reference implementation's.
CXXFLAGS   = $(ROOTCFLAGS) -O2 -g -std=c++17 -Wall -I$(PROTO)
LDFLAGS    = -O2 -g

# Second build of the SAME sources with the flag set the LST device library uses
# (standalone/LST/Makefile CXXFLAGS_CPU). Its only purpose is to attribute a residual difference:
# -march=native turns every a*b + c*d into an FMA and -Ofast lets the compiler reassociate, so this
# binary isolates "different compiler flags" from "different arithmetic" without touching a line of
# the reference implementation.
EXE_FAST      = bin/p22_ref_dump_fast
CXXFLAGS_FAST = $(ROOTCFLAGS) -march=native -mtune=native -Ofast -fno-reciprocal-math -g -std=c++17 -Wall -I$(PROTO)

all: $(EXE) $(EXE_FAST)

$(EXE): $(SRCS) $(PROTO_SRCS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRCS) $(PROTO_SRCS) $(ROOTLIBS) $(LDFLAGS) -o $@

$(EXE_FAST): $(SRCS) $(PROTO_SRCS)
	mkdir -p bin
	$(CXX) $(CXXFLAGS_FAST) $(SRCS) $(PROTO_SRCS) $(ROOTLIBS) $(LDFLAGS) -o $@

clean:
	rm -f $(EXE) $(EXE_FAST)

.PHONY: all clean
