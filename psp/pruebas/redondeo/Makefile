# Rounding-mode probe: main.cpp plus dream-recomp's fenv.cpp from the patched copy in $(RT).
#   make -f /repo/psp/pruebas/redondeo/Makefile RT=/rt
RT    ?= ../src
HERE  := $(dir $(lastword $(MAKEFILE_LIST)))

TARGET = redondeo
OBJS   = obj/main.o obj/fenv.o

EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = ikaruga-psp redondeo

INCDIR   = $(RT)/runtime/include
CFLAGS   = -O2 -G0 -ffp-contract=off -frounding-math
CXXFLAGS = -std=gnu++20
LIBS     = -lstdc++

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

obj/main.o: $(HERE)main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/fenv.o: $(RT)/runtime/src/fenv.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
