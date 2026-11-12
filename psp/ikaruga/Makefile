# Links ikaruga.elf from dream-recomp's core (patched copy in $(RT)), the translated units in
# $(GEN), our host stubs and main.cpp. Driven by scripts/build-psp-ikaruga.sh, run from the
# variant's output directory:  make -f /repo/psp/ikaruga/Makefile RT=... GEN=... OPT_RT=-O2 OPT_GEN=-Os
RT    ?= ../src
GEN   ?= /gen
HERE  := $(dir $(lastword $(MAKEFILE_LIST)))
STUBS := $(HERE)../stubs
# OPT_RT: runtime, stubs and main.cpp. OPT_GEN: the translated units (the bulk of the image).
OPT_RT  ?= -O2
OPT_GEN ?= -Os

RT_SRCS := \
  runtime/src/fenv.cpp runtime/src/sh4/abi_bare.cpp runtime/src/sh4/fsca.cpp \
  runtime/src/mem/dc_memory.cpp runtime/src/sched/scheduler.cpp runtime/src/sh4/intc.cpp \
  runtime/src/sh4/tmu.cpp runtime/src/holly/intc.cpp runtime/src/holly/sysblock.cpp \
  runtime/src/holly/g2dma.cpp runtime/src/sh4/dmac.cpp runtime/src/pvr/spg.cpp \
  runtime/src/pvr/core.cpp runtime/src/system.cpp runtime/src/gdrom/disc.cpp \
  runtime/src/hle/flash.cpp runtime/src/hle/bios.cpp runtime/src/maple/maple.cpp \
  runtime/src/aica/arm7.cpp runtime/src/aica/aica.cpp runtime/src/aica/mixer.cpp runtime/src/state.cpp \
  render/src/display_list.cpp render/src/texture.cpp render/src/framebuffer.cpp \
  render/src/background.cpp render/src/fog.cpp
GEN_SRCS  := $(sort $(notdir $(wildcard $(GEN)/*.cpp)))
HOST_SRCS := video.cpp texture_psp.cpp input.cpp audio.cpp clock.cpp file.cpp disc.cpp vmu.cpp process.cpp config.cpp

RT_OBJS   := $(addprefix obj/rt/,$(RT_SRCS:.cpp=.o))
GEN_OBJS  := $(addprefix obj/gen/,$(GEN_SRCS:.cpp=.o))
HOST_OBJS := $(addprefix obj/host/,$(HOST_SRCS:.cpp=.o)) obj/host/main.o

TARGET = ikaruga
OBJS   = $(RT_OBJS) $(GEN_OBJS) $(HOST_OBJS)

EXTRA_TARGETS    = EBOOT.PBP
PSP_EBOOT_TITLE  = Ikaruga (dream-recomp)
# Writes MEMSIZE=1 into PARAM.SFO (64 MB user memory on PSP-2000 and later).
PSP_LARGE_MEMORY = 1

INCDIR = $(RT)/runtime/include $(RT)/render/include $(STUBS) $(GEN)
# -ffp-contract=off -frounding-math: dream-recomp's ADR 16 (cmake/DreamTargetDefaults.cmake).
# Exceptions stay on: rte() unwinds guest frames with a thrown NonLocalReturn.
CFLAGS   = -G0 -ffp-contract=off -frounding-math -DDREAM_NO_CHD -DDREAM_PERF_COUNTERS -DDREAM_NO_BIOS_IMAGE
# DREAM_FPU_DOUBLE=1: FIPR/FTRV/FMAC in upstream fused/double (patch 0009), the build that must
# match the double PC launcher bit for bit.
ifeq ($(DREAM_FPU_DOUBLE),1)
CFLAGS += -DDREAM_FPU_DOUBLE=1
endif
# DREAM_SOFT_DAZ=1 (default: same as DREAM_FPU_DOUBLE): denormal FP registers read as zero, as on
# x86 with MXCSR.DAZ (patch 0010). Without it the double build diverges from PC at frame 1983.
DREAM_SOFT_DAZ ?= $(DREAM_FPU_DOUBLE)
ifeq ($(DREAM_SOFT_DAZ),1)
CFLAGS += -DDREAM_SOFT_DAZ=1
endif
CXXFLAGS = -std=gnu++20
LDFLAGS  = -Wl,-Map,ikaruga.map
LIBS     = -lstdc++ -lpspgu -lpspge -lpsppower -lpspdisplay -lpsprtc

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

obj/rt/%.o: $(RT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT_RT) $(CXXFLAGS) -c $< -o $@

# -mno-explicit-relocs: with %hi/%lo pairs psp-as does not finish on the 45 MB generated unit
# (timings in docs/tamano-psp.md §4).
GEN_FLAGS := -mno-explicit-relocs

obj/gen/%.o: $(GEN)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT_GEN) $(CXXFLAGS) $(GEN_FLAGS) -c $< -o $@

obj/host/main.o: $(HERE)main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT_RT) $(CXXFLAGS) -c $< -o $@

obj/host/%.o: $(STUBS)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(OPT_RT) $(CXXFLAGS) -c $< -o $@
