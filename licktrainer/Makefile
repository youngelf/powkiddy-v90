# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux x86_64 Guitar Speed-Builder & Lick Coach (licktrainer)
#
# Targets:
#   make            - Build both PowKiddy V90 ARM binary (licktrainer) and Linux x86_64 binary (licktrainer_linux)
#   make arm        - Build PowKiddy V90 ARM binary (licktrainer)
#   make linux-x64  - Build native x86_64 Linux X11 desktop binary (licktrainer_linux)
#   make run-linux  - Build and launch licktrainer_linux on the Linux desktop
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

TARGET_ARM    := licktrainer
TARGET_LINUX  := licktrainer_linux
SRCS          := licktrainer.c
HDRS          := licktrainer_font.h

.PHONY: all arm v90 linux linux-x64 host run-linux package clean

all: $(TARGET_ARM) $(TARGET_LINUX)

arm: $(TARGET_ARM)
v90: $(TARGET_ARM)

linux: $(TARGET_LINUX)
linux-x64: $(TARGET_LINUX)
host: $(TARGET_LINUX)

$(TARGET_ARM): $(SRCS) $(HDRS)
	$(ARM_CC) $(ARM_CFLAGS) -o $(TARGET_ARM) $(SRCS) $(ARM_LDFLAGS)
	$(ARM_STRIP) $(TARGET_ARM)

$(TARGET_LINUX): $(SRCS) $(HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o $(TARGET_LINUX) $(SRCS) $(HOST_LDFLAGS)

run-linux: $(TARGET_LINUX)
	./$(TARGET_LINUX)

package: $(TARGET_ARM)
	mkdir -p sdcard_package/apps/licktrainer
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp $(TARGET_ARM) sdcard_package/apps/licktrainer/
	cp icon.png sdcard_package/apps/licktrainer/
	cp licktrainer.lnk sdcard_package/gmenu2x/sections/applications/

clean:
	rm -f $(TARGET_ARM) $(TARGET_LINUX)
	rm -rf sdcard_package
