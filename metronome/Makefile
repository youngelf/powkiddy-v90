# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host Metronome

# --- PowKiddy V90 (ARM Cross-Compiler) Configuration ---
ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /tmp/miyoo_sdk_extract/opt/miyoo
else
    CHAINPREFIX ?= /opt/miyoo
endif
CROSS_COMPILE = $(CHAINPREFIX)/usr/bin/arm-miyoo-linux-uclibcgnueabi-
SYSROOT = $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot

CC = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)gcc
STRIP = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)strip

SDL_CFLAGS = -I$(SYSROOT)/usr/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
SDL_LIBS = -L$(SYSROOT)/usr/lib -lSDL -lpthread -lm

CFLAGS = -O2 -Wall -Wextra -march=armv5te -mtune=arm926ej-s $(SDL_CFLAGS)
LDFLAGS = $(SDL_LIBS)

# --- Host Linux (x86_64 X11 + PulseAudio dlopen) Configuration ---
HOST_CC ?= gcc
HOST_CFLAGS = -O2 -Wall -Wextra -DHOST_X11 -D_GNU_SOURCE=1
HOST_LDFLAGS = -lX11 -lm -lpthread -ldl

# --- Targets ---
TARGET = metronome
HOST_TARGET = metronome_linux
SRCS = metronome.c

.PHONY: all v90 linux host run-linux clean package

# Default builds the PowKiddy V90 binary
all: $(TARGET)

v90: $(TARGET)

# Build x86_64 Linux binary for local testing/debugging
linux: $(HOST_TARGET)

host: $(HOST_TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	$(STRIP) $(TARGET)

$(HOST_TARGET): $(SRCS)
	$(HOST_CC) $(HOST_CFLAGS) -o $(HOST_TARGET) $(SRCS) $(HOST_LDFLAGS)

run-linux: $(HOST_TARGET)
	./$(HOST_TARGET)

package: $(TARGET)
	mkdir -p sdcard_package/apps/metronome
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp $(TARGET) sdcard_package/apps/metronome/
	cp metronome.png sdcard_package/apps/metronome/
	cp metronome.lnk sdcard_package/gmenu2x/sections/applications/

clean:
	rm -f $(TARGET) $(HOST_TARGET)
	rm -rf sdcard_package
