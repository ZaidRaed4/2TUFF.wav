# 2TUFF.wav - PSP homebrew music player
# Canonical PSPSDK Makefile (requires `make` + the pspdev toolchain on PATH).
# If you don't have `make`, use ./build.sh instead - it does the same steps.

TARGET = 2TUFFwav
OBJS = $(patsubst %.c,%.o,$(wildcard src/*.c))

INCDIR =
CFLAGS  = -G0 -O2 -Wall -Wno-format-truncation -fno-strict-aliasing -Isrc
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  = $(CFLAGS)

LIBDIR =
LDFLAGS =
# App + portlib (libjpeg/libpng/zlib) libraries; build.mak appends the base SDK libs last.
LIBS = -lpspgu -lpspmp3 -lpspaudio -lpsputility -lpsppower -ljpeg -lpng16 -lz -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = 2TUFF.wav
PSP_EBOOT_ICON  = assets/ICON0.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
