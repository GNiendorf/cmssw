# P2.3 flag-matched reference build (the p21_ref / p22_ref pattern, extended to the WHOLE
# prototype because the P2.3 gate compares the emitted track candidates, not a sidecar).
#
# It compiles the FROZEN prototype translation units -- standalone/prototype/, never modified,
# never written to: objects land in obj_fast/ here -- with the flag set the LST device library
# uses (standalone/LST/Makefile CXXFLAGS_CPU). Its only purpose is ATTRIBUTION: -march=native
# turns every a*b + c*d into an FMA, -Ofast lets the compiler reassociate and re-associates libm
# calls, so this binary separates "different compiler flags" from "different arithmetic" without
# touching a line of the reference implementation.
#
# The stock -O2 binary stays prototype/bin/chainproto and is what every physics number is quoted
# from; this one is only ever compared against, never used for a scoreboard.

PROTO   = ../prototype
SRCS    = $(wildcard $(PROTO)/*.cc)
OBJDIR  = obj_fast
OBJS    = $(patsubst $(PROTO)/%.cc,$(OBJDIR)/%.o,$(SRCS))
EXE     = bin/chainproto_fast

CXX        = g++
ROOTCFLAGS = $(shell root-config --cflags)
ROOTLIBS   = $(shell root-config --libs)
# -std=c++17 must follow ROOTCFLAGS (root-config injects -std=c++20 and the last one wins).
CXXFLAGS   = $(ROOTCFLAGS) -march=native -mtune=native -Ofast -fno-reciprocal-math -g -std=c++17 -Wall -I$(PROTO)
LDFLAGS    = -Ofast -g

all: $(EXE)

$(EXE): $(OBJS)
	mkdir -p bin
	$(CXX) $(LDFLAGS) $(OBJS) $(ROOTLIBS) -o $@

$(OBJDIR)/%.o: $(PROTO)/%.cc
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(EXE)

.PHONY: all clean
