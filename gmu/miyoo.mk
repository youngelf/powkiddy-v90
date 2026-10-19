#
# Gmu Music Player
#
# File: miyoo.mk
#
# Description: Makefile configuration for MiyooCFW (Powkiddy V90/Q90)
#

TARGET=miyoo
DECODERS_TO_BUILD=decoders/vorbis.so decoders/musepack.so decoders/flac.so decoders/wavpack.so decoders/mpg123.so decoders/mikmod.so
FRONTENDS_TO_BUILD=frontends/sdl.so frontends/log.so
SDL_LIB=-lSDL -lpthread -lpng -ljpeg
SDL_CFLAGS=-I/opt/miyoo/arm-miyoo-linux-uclibcgnueabi/sysroot/usr/include/SDL -I/opt/miyoo/arm-miyoo-linux-uclibcgnueabi/sysroot/usr/include -D_GNU_SOURCE=1 -D_REENTRANT
CXX=arm-miyoo-linux-uclibcgnueabi-g++
CC=arm-miyoo-linux-uclibcgnueabi-gcc
STRIP=arm-miyoo-linux-uclibcgnueabi-strip
COPTS?=-O3 -ffast-math -mcpu=arm926ej-s -mfloat-abi=soft
CFLAGS=-fomit-frame-pointer $(SDL_CFLAGS) -DFILE_HW_H="\"hw_$(TARGET).h\""
LFLAGS=-s -L/opt/miyoo/arm-miyoo-linux-uclibcgnueabi/sysroot/usr/lib -lpthread -lm -ldl -lz -lgcc -Wl,-export-dynamic
DISTFILES=$(BINARY) frontends decoders themes gmu.png README.txt gmu-miyoo.dge COPYING gmu.miyoo.conf gmuinput.miyoo.conf miyoo.keymap gmu.bmp
TOOLS_TO_BUILD=
