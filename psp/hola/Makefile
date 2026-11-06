TARGET = hola
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra -Werror
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspdebug -lpspdisplay

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = ikaruga-psp hola
# Writes MEMSIZE=1 into PARAM.SFO (64 MB user memory on PSP-2000 and later).
PSP_LARGE_MEMORY = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
