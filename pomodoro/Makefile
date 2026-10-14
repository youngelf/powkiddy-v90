# Makefile for pomodoro - PowKiddy V90 (MiyooCFW 1.3.3), Anbernic RG35XX SP (Knulli), & Linux Host
#
# Usage:
#   make               - Build ARM binary (pomodoro) for PowKiddy V90 (MiyooCFW 1.3.3)
#   make 35xxsp        - Build aarch64 binary (pomodoro_rg35xxsp) for Anbernic RG35XX SP (Knulli)
#   make linux         - Build x86_64 Linux X11 binary (pomodoro_linux) for host debugging
#   make run-linux     - Build and launch pomodoro_linux on the Linux host
#   make opk           - Create pomodoro.opk SquashFS package for GMenu2X (PowKiddy V90)
#   make package       - Create sdcard_package/ ready to copy to V90 SD card
#   make package-35xxsp- Create sdcard_35xxsp/ ready to copy to RG35XX SP (Knulli) SD card
#   make clean         - Remove compiled binaries, .opk, and package folders

TARGET          := pomodoro
RG35XXSP_TARGET := pomodoro_rg35xxsp
LINUX_TARGET    := pomodoro_linux
OPK_TARGET      := $(TARGET).opk
SRC             := pomodoro.c

# 1. Host Linux compiler (uses native X11, already installed on host)
HOST_CC      ?= gcc
HOST_SCALE   ?= 2
HOST_CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE)
HOST_LDFLAGS := -lX11 -lm

# 2. Anbernic RG35XX SP (Allwinner H700 Cortex-A53 aarch64, Knulli / Batocera)
AARCH64_ROOT ?= $(HOME)/.cache/aarch64_toolchain
ifneq ($(wildcard $(AARCH64_ROOT)/usr/bin/aarch64-linux-gnu-gcc-14),)
    RG_PATH    := $(AARCH64_ROOT)/usr/bin:$(PATH)
    RG_LD_LIB  := $(AARCH64_ROOT)/usr/lib/x86_64-linux-gnu:$(AARCH64_ROOT)/usr/lib
    RG_CC      := PATH="$(RG_PATH)" LD_LIBRARY_PATH="$(RG_LD_LIB)" aarch64-linux-gnu-gcc-14
    RG_STRIP   := PATH="$(RG_PATH)" LD_LIBRARY_PATH="$(RG_LD_LIB)" aarch64-linux-gnu-strip
    RG_SYSROOT := --sysroot="$(AARCH64_ROOT)" \
                  -isystem "$(AARCH64_ROOT)/usr/aarch64-linux-gnu/include" \
                  -B"$(AARCH64_ROOT)/usr/aarch64-linux-gnu/lib" \
                  -B"$(AARCH64_ROOT)/usr/lib/gcc-cross/aarch64-linux-gnu/14" \
                  -L"$(AARCH64_ROOT)/usr/aarch64-linux-gnu/lib"
else
    RG_CC      := aarch64-linux-gnu-gcc
    RG_STRIP   := aarch64-linux-gnu-strip
    RG_SYSROOT :=
endif
RG_CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -mcpu=cortex-a53 -DTARGET_RG35XXSP $(RG_SYSROOT)
RG_LDFLAGS := -lm -ldl

# 3. PowKiddy V90 (Allwinner F1C100S ARM926EJ-S, MiyooCFW 1.3.3 uClibc + SDL 1.2)
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
MKSQUASHFS    ?= mksquashfs

SYSROOT       := $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot
SDL_CFLAGS    := -I$(SYSROOT)/usr/include/SDL -I$(SYSROOT)/usr/include -D_GNU_SOURCE=1 -D_REENTRANT
SDL_LIBS      := -L$(SYSROOT)/usr/lib -lSDL -lpthread

CFLAGS  := -O2 -Wall -Wextra -std=gnu11 $(SDL_CFLAGS)
LDFLAGS := $(SDL_LIBS) -lm

.PHONY: all v90 35xxsp rg35xxsp linux host run-linux opk package package-35xxsp clean

all: $(TARGET)

v90: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	$(STRIP) $(TARGET)

35xxsp: $(RG35XXSP_TARGET)

rg35xxsp: $(RG35XXSP_TARGET)

$(RG35XXSP_TARGET): $(SRC)
	$(RG_CC) $(RG_CFLAGS) $(SRC) -o $(RG35XXSP_TARGET) $(RG_LDFLAGS)
	$(RG_STRIP) $(RG35XXSP_TARGET)

linux: $(LINUX_TARGET)

host: $(LINUX_TARGET)

$(LINUX_TARGET): $(SRC)
	$(HOST_CC) $(HOST_CFLAGS) $(SRC) -o $(LINUX_TARGET) $(HOST_LDFLAGS)

run-linux: $(LINUX_TARGET)
	./$(LINUX_TARGET)

opk: $(OPK_TARGET)

$(OPK_TARGET): $(TARGET) $(TARGET).png default.miyoo.desktop
	$(MKSQUASHFS) $(TARGET) $(TARGET).png default.miyoo.desktop $(OPK_TARGET) -all-root -noappend -no-exports -no-xattrs -comp gzip

package: $(TARGET)
	@mkdir -p sdcard_package/apps/$(TARGET)
	@mkdir -p sdcard_package/gmenu2x/sections/applications
	@cp $(TARGET) sdcard_package/apps/$(TARGET)/$(TARGET)
	@cp $(TARGET).png sdcard_package/apps/$(TARGET)/$(TARGET).png
	@cp $(TARGET).lnk sdcard_package/gmenu2x/sections/applications/$(TARGET)
	@echo "Package created in ./sdcard_package/ (copy contents to the V90 'main' SD partition)"

package-35xxsp: $(RG35XXSP_TARGET)
	@mkdir -p sdcard_35xxsp/roms/ports/$(TARGET)
	@cp $(RG35XXSP_TARGET) sdcard_35xxsp/roms/ports/$(TARGET)/$(TARGET)
	@cp $(TARGET).png sdcard_35xxsp/roms/ports/$(TARGET)/$(TARGET).png
	@cp Pomodoro.sh sdcard_35xxsp/roms/ports/Pomodoro.sh
	@chmod +x sdcard_35xxsp/roms/ports/Pomodoro.sh sdcard_35xxsp/roms/ports/$(TARGET)/$(TARGET)
	@echo "Package created in ./sdcard_35xxsp/ (copy roms/ports/* to your Knulli SD card)"

clean:
	rm -f $(TARGET) $(RG35XXSP_TARGET) $(LINUX_TARGET) $(OPK_TARGET)
	rm -rf sdcard_package sdcard_35xxsp
