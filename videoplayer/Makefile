# Makefile for PowKiddy V90 Video Player (MiyooCFW 1.3.3 ARMv5TE & Linux x86_64 Host)

ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
MIYOO_DIR ?= /opt/miyoo
else
MIYOO_DIR ?= /tmp/miyoo_sdk_extract/opt/miyoo
endif

SYSROOT   := $(MIYOO_DIR)/arm-miyoo-linux-uclibcgnueabi/sysroot
CROSS_ENV := LD_LIBRARY_PATH=$(MIYOO_DIR)/lib
ARM_CC    := $(CROSS_ENV) $(MIYOO_DIR)/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc
ARM_STRIP := $(CROSS_ENV) $(MIYOO_DIR)/usr/bin/arm-miyoo-linux-uclibcgnueabi-strip

ARM_CFLAGS  := -O2 -Wall -Wextra -Wno-deprecated-declarations -std=gnu11 -march=armv5te -mtune=arm926ej-s \
               -I$(SYSROOT)/usr/include/SDL \
               -I$(SYSROOT)/usr/include \
               -D_GNU_SOURCE=1 -D_REENTRANT
ARM_LDFLAGS := -L$(SYSROOT)/usr/lib -Wl,-rpath,/mnt/apps/videoplayer/libs \
               -Wl,--allow-shlib-undefined \
               -lavformat -lavcodec -lswscale -lswresample -lavutil -lasound -lSDL -lz -lpthread -lm

HOST_CC      := gcc
HOST_CFLAGS  := -O2 -g -Wall -Wextra -Wno-deprecated-declarations -std=gnu11 -DHOST_X11 -DHOST_SCALE=2 -D_GNU_SOURCE=1 \
                $(shell pkg-config --cflags libavformat libavcodec libswscale libswresample libavutil alsa x11)
HOST_LDFLAGS := $(shell pkg-config --libs libavformat libavcodec libswscale libswresample libavutil alsa x11) -lm

.PHONY: all v90 linux run-linux package clean

all: v90 linux

v90: videoplayer

linux: videoplayer_linux

videoplayer: videoplayer.c videoplayer_font.h
	$(ARM_CC) $(ARM_CFLAGS) -o $@ videoplayer.c $(ARM_LDFLAGS)
	$(ARM_STRIP) $@

videoplayer_linux: videoplayer.c videoplayer_font.h
	$(HOST_CC) $(HOST_CFLAGS) -o $@ videoplayer.c $(HOST_LDFLAGS)

run-linux: videoplayer_linux
	./videoplayer_linux ./media

package: videoplayer
	mkdir -p sdcard_package/apps/videoplayer/libs
	mkdir -p sdcard_package/gmenu2x/sections/applications
	mkdir -p sdcard_package/media
	cp videoplayer sdcard_package/apps/videoplayer/videoplayer
	cp run.sh sdcard_package/apps/videoplayer/run.sh
	chmod +x sdcard_package/apps/videoplayer/run.sh
	cp icon.png sdcard_package/apps/videoplayer/icon.png
	cp videoplayer.lnk sdcard_package/gmenu2x/sections/applications/mplayer
	cp $(SYSROOT)/usr/lib/libavformat.so.57 sdcard_package/apps/videoplayer/libs/
	cp $(SYSROOT)/usr/lib/libavcodec.so.57 sdcard_package/apps/videoplayer/libs/
	cp $(SYSROOT)/usr/lib/libswscale.so.4 sdcard_package/apps/videoplayer/libs/
	cp $(SYSROOT)/usr/lib/libswresample.so.2 sdcard_package/apps/videoplayer/libs/
	cp $(SYSROOT)/usr/lib/libavutil.so.55 sdcard_package/apps/videoplayer/libs/
	$(ARM_STRIP) sdcard_package/apps/videoplayer/libs/*.so*
	cp -f media/*.mp4 sdcard_package/media/ 2>/dev/null || true

clean:
	rm -f videoplayer videoplayer_linux
	rm -rf sdcard_package
