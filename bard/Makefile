# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host EPUB Reader (Bard Replacement)
#
# Usage:
#   make           - Build both PowKiddy V90 ARM binary (bard) and Linux x86_64 binary (bard_linux)
#   make v90       - Build ARM binary (bard) for PowKiddy V90 (MiyooCFW 1.3.3)
#   make linux     - Build x86_64 Linux X11 binary (bard_linux) for desktop debugging
#   make run-linux - Build and launch bard_linux on the Linux desktop
#   make package   - Create sdcard_package/ ready to overwrite /mnt/apps/bard on V90 SD card
#   make clean     - Remove compiled binaries and sdcard_package/

# --- PowKiddy V90 (ARM Cross-Compiler) Configuration ---
ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /tmp/miyoo_sdk_extract/opt/miyoo
else
    CHAINPREFIX ?= /opt/miyoo
endif

CROSS_COMPILE = $(CHAINPREFIX)/usr/bin/arm-miyoo-linux-uclibcgnueabi-
SYSROOT       = $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot

CC    = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)gcc
STRIP = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)strip

V90_CFLAGS = -I$(SYSROOT)/usr/include/SDL \
             -I$(SYSROOT)/usr/include/libxml2 \
             -I$(SYSROOT)/usr/include \
             -D_GNU_SOURCE=1 -D_REENTRANT
V90_LIBS   = -L$(SYSROOT)/usr/lib -lSDL -lzip -lxml2 -lz -lpthread -lm

CFLAGS  = -O2 -Wall -Wextra -std=gnu11 -march=armv5te -mtune=arm926ej-s $(V90_CFLAGS)
LDFLAGS = $(V90_LIBS)

# --- Host Linux (x86_64 Native X11) Configuration ---
HOST_CC      ?= gcc
HOST_SCALE   ?= 2
HOST_CFLAGS  = -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE) -D_GNU_SOURCE=1 \
               $(shell pkg-config --cflags libzip libxml-2.0)
HOST_LDFLAGS = $(shell pkg-config --libs libzip libxml-2.0) -lX11 -lz -lm

# --- Targets ---
TARGET      = bard
HOST_TARGET = bard_linux
SRCS        = bard.c
HEADERS     = bard_font.h

.PHONY: all v90 linux host run-linux package clean

all: $(TARGET) $(HOST_TARGET)

v90: $(TARGET)

linux: $(HOST_TARGET)

host: $(HOST_TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	$(STRIP) $(TARGET)

$(HOST_TARGET): $(SRCS) $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) -o $(HOST_TARGET) $(SRCS) $(HOST_LDFLAGS)

run-linux: $(HOST_TARGET)
	./$(HOST_TARGET)

package: $(TARGET)
	mkdir -p sdcard_package/apps/bard
	mkdir -p sdcard_package/gmenu2x/sections/applications
	mkdir -p sdcard_package/media
	cp $(TARGET) sdcard_package/apps/bard/bard
	cp icon.png sdcard_package/apps/bard/icon.png
	cp bard.lnk sdcard_package/gmenu2x/sections/applications/bard
	cp -f media/*.epub sdcard_package/media/ 2>/dev/null || true

clean:
	rm -f $(TARGET) $(HOST_TARGET)
	rm -rf sdcard_package
