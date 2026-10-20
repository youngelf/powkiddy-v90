# Makefile for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host Guitar Ear Trainer (eartrainer)

ifneq ($(wildcard /opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /opt/miyoo
else ifneq ($(wildcard /tmp/miyoo_sdk_extract/opt/miyoo/usr/bin/arm-miyoo-linux-uclibcgnueabi-gcc),)
    CHAINPREFIX ?= /tmp/miyoo_sdk_extract/opt/miyoo
else
    CHAINPREFIX ?= /opt/miyoo
endif

CROSS_COMPILE = $(CHAINPREFIX)/usr/bin/arm-miyoo-linux-uclibcgnueabi-
SYSROOT       = $(CHAINPREFIX)/arm-miyoo-linux-uclibcgnueabi/sysroot

CC            = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)gcc
STRIP         = LD_LIBRARY_PATH=$(CHAINPREFIX)/lib $(CROSS_COMPILE)strip

ARM_CFLAGS    = -O2 -Wall -Wextra -std=gnu11 -march=armv5te -mtune=arm926ej-s \
                -I$(SYSROOT)/usr/include/SDL -I$(SYSROOT)/usr/include \
                -D_GNU_SOURCE=1 -D_REENTRANT
ARM_LIBS      = -L$(SYSROOT)/usr/lib -lSDL -lpthread -lm

HOST_CC      ?= gcc
HOST_SCALE   ?= 2
HOST_CFLAGS   = -O2 -g -Wall -Wextra -std=gnu11 -DHOST_X11 -DHOST_SCALE=$(HOST_SCALE) -D_GNU_SOURCE=1
HOST_LIBS     = -lX11 -lm -lpthread -ldl

TARGET        = eartrainer
LINUX_TARGET  = eartrainer_linux
SRCS          = eartrainer.c

.PHONY: all arm v90 linux-x64 linux host run-linux package clean

all: $(TARGET) $(LINUX_TARGET)

arm: $(TARGET)

v90: $(TARGET)

linux-x64: $(LINUX_TARGET)

linux: $(LINUX_TARGET)

host: $(LINUX_TARGET)

$(TARGET): $(SRCS) eartrainer_font.h
	$(CC) $(ARM_CFLAGS) -o $(TARGET) $(SRCS) $(ARM_LIBS)
	$(STRIP) $(TARGET)

$(LINUX_TARGET): $(SRCS) eartrainer_font.h
	$(HOST_CC) $(HOST_CFLAGS) -o $(LINUX_TARGET) $(SRCS) $(HOST_LIBS)

run-linux: $(LINUX_TARGET)
	./$(LINUX_TARGET)

package: $(TARGET)
	mkdir -p sdcard_package/apps/eartrainer
	mkdir -p sdcard_package/gmenu2x/sections/applications
	cp $(TARGET) sdcard_package/apps/eartrainer/eartrainer
	cp icon.png sdcard_package/apps/eartrainer/icon.png
	cp eartrainer.lnk sdcard_package/gmenu2x/sections/applications/eartrainer

clean:
	rm -f $(TARGET) $(LINUX_TARGET)
	rm -rf sdcard_package
