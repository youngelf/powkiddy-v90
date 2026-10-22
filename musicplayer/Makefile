# Makefile for PowKiddy V90 Directory Music Player (MP3 / OGG / FLAC)
# Targets:
#   make all      - Build both PowKiddy V90 (ARM) and Linux Desktop (x86_64 X11) binaries + run verification
#   make v90      - Build only the PowKiddy V90 ARM binary (musicplayer)
#   make linux    - Build only the Linux Desktop X11 binary (musicplayer_linux)
#   make test     - Verify libmpg123, libvorbisfile, and libFLAC decoding, seeking, and state persistence
#   make package  - Build and populate sdcard_package/ ready to copy to V90 SD card

ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    MIYOO_BASE := /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    MIYOO_BASE := /tmp/miyoo_sdk_extract/opt/miyoo
else
    MIYOO_BASE := /opt/miyoo
endif

ARM_CC      := LD_LIBRARY_PATH=$(MIYOO_BASE)/lib $(MIYOO_BASE)/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc
ARM_STRIP   := LD_LIBRARY_PATH=$(MIYOO_BASE)/lib $(MIYOO_BASE)/usr/bin/arm-miyoo-linux-uclibcgnueabi-strip
SYSROOT     := $(MIYOO_BASE)/arm-miyoo-linux-uclibcgnueabi/sysroot

ARM_CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -march=armv5te -mtune=arm926ej-s \
               -I$(SYSROOT)/usr/include/SDL -I$(SYSROOT)/usr/include \
               -D_GNU_SOURCE=1 -D_REENTRANT -D_FILE_OFFSET_BITS=64
ARM_LDFLAGS := -L$(SYSROOT)/usr/lib -Wl,-rpath,/mnt/apps/musicplayer/libs \
               -Wl,--allow-shlib-undefined \
               -lmpg123 -lvorbisfile -lvorbis -lFLAC -logg -lSDL -lpthread -lm

HOST_CC      := gcc
HOST_CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=2 -D_GNU_SOURCE=1 \
                $(shell pkg-config --cflags libmpg123 vorbisfile flac ogg x11 2>/dev/null)
HOST_LDFLAGS := $(shell pkg-config --libs libmpg123 vorbisfile vorbis flac ogg x11 2>/dev/null || echo "-lmpg123 -lvorbisfile -lvorbis -lFLAC -logg -lX11") \
                -lm -lpthread -ldl

.PHONY: all v90 linux test package clean

all: v90 linux test

v90: musicplayer

linux: musicplayer_linux

musicplayer: musicplayer.c musicplayer_font.h
	$(ARM_CC) $(ARM_CFLAGS) -o $@ musicplayer.c $(ARM_LDFLAGS)
	$(ARM_STRIP) $@

musicplayer_linux: musicplayer.c musicplayer_font.h
	$(HOST_CC) $(HOST_CFLAGS) -o $@ musicplayer.c $(HOST_LDFLAGS)

test: musicplayer_linux
	./musicplayer_linux --verify ./sample_media/01_Blues_Guitar_Sessions

package: musicplayer
	mkdir -p sdcard_package/apps/musicplayer/libs
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp musicplayer sdcard_package/apps/musicplayer/
	cp icon.png sdcard_package/apps/musicplayer/
	cp musicplayer.lnk sdcard_package/gmenu2x/sections/applications/
	cp $(SYSROOT)/usr/lib/libmpg123.so.0.44.8 sdcard_package/apps/musicplayer/libs/libmpg123.so.0
	cp $(SYSROOT)/usr/lib/libFLAC.so.8.3.0 sdcard_package/apps/musicplayer/libs/libFLAC.so.8
	cp $(SYSROOT)/usr/lib/libvorbisfile.so.3.3.7 sdcard_package/apps/musicplayer/libs/libvorbisfile.so.3
	cp $(SYSROOT)/usr/lib/libvorbis.so.0.4.8 sdcard_package/apps/musicplayer/libs/libvorbis.so.0
	cp $(SYSROOT)/usr/lib/libogg.so.0.8.3 sdcard_package/apps/musicplayer/libs/libogg.so.0
	$(ARM_STRIP) sdcard_package/apps/musicplayer/libs/*.so*

clean:
	rm -f musicplayer musicplayer_linux
	rm -rf sdcard_package
