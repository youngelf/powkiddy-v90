# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host Calculator
#
# Usage:
#   make           - Build both PowKiddy V90 ARM binary (calc) and Linux x86_64 binary (calc_linux)
#   make v90       - Build ARM binary (calc) for PowKiddy V90 (MiyooCFW 1.3.3)
#   make linux     - Build x86_64 Linux X11 binary (calc_linux) for desktop testing
#   make run-linux - Build and launch calc_linux on the Linux desktop
#   make package   - Create sdcard_package/ ready to copy to V90 'main' SD card partition
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

SDL_CFLAGS = -I$(SYSROOT)/usr/include/SDL -I$(SYSROOT)/usr/include -D_GNU_SOURCE=1 -D_REENTRANT
SDL_LIBS   = -L$(SYSROOT)/usr/lib -lSDL -lpthread -lm

CFLAGS  = -O2 -Wall -Wextra -std=gnu11 -march=armv5te -mtune=arm926ej-s $(SDL_CFLAGS)
LDFLAGS = $(SDL_LIBS)

# --- Host Linux (x86_64 Native X11) Configuration ---
HOST_CC      ?= gcc
HOST_SCALE   ?= 2
HOST_CFLAGS  = -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE) -D_GNU_SOURCE=1
HOST_LDFLAGS = -lX11 -lm

# --- Targets ---
TARGET      = calc
HOST_TARGET = calc_linux
SRCS        = calc.c
HEADERS     = calc_font.h

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
	mkdir -p sdcard_package/apps/calc
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp $(TARGET) sdcard_package/apps/calc/calc
	cp icon.png sdcard_package/apps/calc/icon.png
	cp calc.lnk sdcard_package/gmenu2x/sections/applications/calc

clean:
	rm -f $(TARGET) $(HOST_TARGET)
	rm -rf sdcard_package
