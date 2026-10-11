# Makefile for pom1 - PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host Pomodoro Timer
#
# Usage:
#   make               - Build ARM binary (pom1) for PowKiddy V90 (MiyooCFW 1.3.3)
#   make linux         - Build x86_64 Linux X11 binary (pom1_linux) for host debugging
#   make run-linux     - Build and launch pom1_linux on the Linux host
#   make package       - Create sdcard_package/ ready to copy to V90 SD card
#   make clean         - Remove compiled binaries and package folder

TARGET       := pom1
LINUX_TARGET := pom1_linux
SRC          := pom1.c

# Host Linux compiler (uses native X11, already installed on host)
HOST_CC      ?= gcc
HOST_SCALE   ?= 2
HOST_CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE)
HOST_LDFLAGS := -lX11 -lm

# Auto-detect MiyooCFW 1.3.3 uClibc toolchain in /opt/miyoo or /tmp/miyoo_sdk_extract/opt/miyoo
ifneq ($(wildcard /opt/miyoo/bin/arm-linux-gcc),)
    CHAINPREFIX ?= /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/bin/arm-linux-gcc),)
    CHAINPREFIX ?= /tmp/miyoo_sdk_extract/opt/miyoo
else
    CHAINPREFIX ?= /opt/miyoo
endif

CROSS_COMPILE ?= $(CHAINPREFIX)/bin/arm-linux-
CC            := LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)gcc
STRIP         := LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)strip

SYSROOT       := $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot
SDL_CFLAGS    := -I$(SYSROOT)/usr/include/SDL -I$(SYSROOT)/usr/include -D_GNU_SOURCE=1 -D_REENTRANT
SDL_LIBS      := -L$(SYSROOT)/usr/lib -lSDL -lpthread

CFLAGS  := -O2 -Wall -Wextra -std=gnu11 $(SDL_CFLAGS)
LDFLAGS := $(SDL_LIBS) -lm

.PHONY: all v90 linux host run-linux package clean

all: $(TARGET)

v90: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	$(STRIP) $(TARGET)

linux: $(LINUX_TARGET)

host: $(LINUX_TARGET)

$(LINUX_TARGET): $(SRC)
	$(HOST_CC) $(HOST_CFLAGS) $(SRC) -o $(LINUX_TARGET) $(HOST_LDFLAGS)

run-linux: $(LINUX_TARGET)
	./$(LINUX_TARGET)

package: $(TARGET)
	@mkdir -p sdcard_package/apps/pom1
	@mkdir -p sdcard_package/gmenu2x/sections/applications
	@cp $(TARGET) sdcard_package/apps/pom1/$(TARGET)
	@cp pom1.png sdcard_package/apps/pom1/pom1.png
	@cp pom1.lnk sdcard_package/gmenu2x/sections/applications/pom1
	@echo "Package created in ./sdcard_package/ (copy contents to the V90 'main' SD partition)"

clean:
	rm -f $(TARGET) $(LINUX_TARGET)
	rm -rf sdcard_package
