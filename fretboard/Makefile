# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux x86_64 Interactive Guitar Fretboard (fretboard)
#
# Targets:
#   make            - Build both PowKiddy V90 ARM binary (fretboard) and Linux x86_64 binary (fretboard_linux)
#   make arm        - Build PowKiddy V90 ARM binary (fretboard)
#   make linux-x64  - Build native x86_64 Linux X11 desktop binary (fretboard_linux)
#   make run-linux  - Build and launch fretboard_linux on the Linux desktop
#   make package    - Populate sdcard_package/ ready to copy to the PowKiddy V90 'main' SD partition
#   make clean      - Remove compiled binaries and sdcard_package/

ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /tmp/miyoo_sdk_extract/opt/miyoo
else
    CHAINPREFIX ?= /opt/miyoo
endif

CROSS_COMPILE := $(CHAINPREFIX)/usr/bin/arm-miyoo-linux-uclibcgnueabi-
SYSROOT       := $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot

ARM_CC        := LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)gcc
ARM_STRIP     := LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)strip

ARM_CFLAGS    := -O2 -Wall -Wextra -std=gnu11 -march=armv5te -mtune=arm926ej-s \
                 -I$(SYSROOT)/usr/include/SDL \
                 -I$(SYSROOT)/usr/include \
                 -D_GNU_SOURCE=1 -D_REENTRANT
ARM_LDFLAGS   := -L$(SYSROOT)/usr/lib -lSDL -lpthread -lm

HOST_CC       ?= gcc
HOST_SCALE    ?= 2
HOST_CFLAGS   := -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE) -D_GNU_SOURCE=1
HOST_LDFLAGS  := -lX11 -lm -lpthread -ldl

TARGET        := fretboard
LINUX_TARGET  := fretboard_linux
SRCS          := fretboard.c
HDRS          := fretboard_font.h

.PHONY: all arm v90 linux linux-x64 host run-linux package clean

all: $(TARGET) $(LINUX_TARGET)

arm v90: $(TARGET)

linux linux-x64 host: $(LINUX_TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(ARM_CC) $(ARM_CFLAGS) -o $(TARGET) $(SRCS) $(ARM_LDFLAGS)
	$(ARM_STRIP) $(TARGET)

$(LINUX_TARGET): $(SRCS) $(HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o $(LINUX_TARGET) $(SRCS) $(HOST_LDFLAGS)

run-linux: $(LINUX_TARGET)
	./$(LINUX_TARGET)

package: $(TARGET)
	mkdir -p sdcard_package/apps/fretboard
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp $(TARGET) sdcard_package/apps/fretboard/fretboard
	cp icon.png sdcard_package/apps/fretboard/icon.png
	cp fretboard.lnk sdcard_package/gmenu2x/sections/applications/fretboard

clean:
	rm -f $(TARGET) $(LINUX_TARGET)
	rm -rf sdcard_package
