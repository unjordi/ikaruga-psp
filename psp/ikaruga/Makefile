# Links ikaruga.elf from dream-recomp's core (patched copy in $(RT)), the translated units in
# $(GEN), our host stubs and main.cpp. Driven by scripts/build-psp-ikaruga.sh, run from the
# variant's output directory:  make -f /repo/psp/ikaruga/Makefile RT=... GEN=... OPT=-O2
RT    ?= ../src
GEN   ?= /gen
HERE  := $(dir $(lastword $(MAKEFILE_LIST)))
STUBS := $(HERE)../stubs
OPT   ?= -O2

RT_SRCS := \
  runtime/src/fenv.cpp runtime/src/sh4/abi_bare.cpp runtime/src/sh4/fsca.cpp \
  runtime/src/mem/dc_memory.cpp runtime/src/sched/scheduler.cpp runtime/src/sh4/intc.cpp \
  runtime/src/sh4/tmu.cpp runtime/src/holly/intc.cpp runtime/src/holly/sysblock.cpp \
  runtime/src/holly/g2dma.cpp runtime/src/sh4/dmac.cpp runtime/src/pvr/spg.cpp \
  runtime/src/pvr/core.cpp runtime/src/system.cpp runtime/src/gdrom/disc.cpp \
  runtime/src/hle/flash.cpp runtime/src/hle/bios.cpp runtime/src/maple/maple.cpp \
  runtime/src/aica/arm7.cpp runtime/src/aica/aica.cpp runtime/src/aica/mixer.cpp \
  render/src/display_list.cpp render/src/texture.cpp render/src/framebuffer.cpp \
  render/src/background.cpp render/src/fog.cpp
GEN_SRCS  := $(sort $(notdir $(wildcard $(GEN)/*.cpp)))
HOST_SRCS := video.cpp input.cpp audio.cpp clock.cpp file.cpp process.cpp

RT_OBJS   := $(addprefix obj/rt/,$(RT_SRCS:.cpp=.o))
GEN_OBJS  := $(addprefix obj/gen/,$(GEN_SRCS:.cpp=.o))
HOST_OBJS := $(addprefix obj/host/,$(HOST_SRCS:.cpp=.o)) obj/host/main.o

TARGET = ikaruga
OBJS   = $(RT_OBJS) $(GEN_OBJS) $(HOST_OBJS)

INCDIR = $(RT)/runtime/include $(RT)/render/include $(STUBS) $(GEN)
# -ffp-contract=off -frounding-math: dream-recomp's ADR 16 (cmake/DreamTargetDefaults.cmake).
# Exceptions stay on: rte() unwinds guest frames with a thrown NonLocalReturn.
CFLAGS   = $(OPT) -G0 -ffp-contract=off -frounding-math -DDREAM_NO_CHD
CXXFLAGS = -std=gnu++20
LDFLAGS  = -Wl,-Map,ikaruga.map
LIBS     = -lstdc++

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

obj/rt/%.o: $(RT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -mno-explicit-relocs: with %hi/%lo pairs psp-as is superlinear on a 45 MB unit (800k lines of
# ikaruga.cpp's assembly: 7.9 s; the whole 8.1M lines: >26 min unfinished). With assembler macros
# instead the whole unit assembles in 3 s.
GEN_FLAGS := -mno-explicit-relocs

obj/gen/%.o: $(GEN)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GEN_FLAGS) -c $< -o $@

obj/host/main.o: $(HERE)main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/host/%.o: $(STUBS)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
