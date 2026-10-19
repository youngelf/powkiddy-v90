# Generated Gmu config
CC=cc
SDL_CFLAGS=-I/usr/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
SDL_LIB=-lSDL
CFLAGS= -I/usr/bin/../include  $(SDL_CFLAGS) -fsigned-char -D_REENTRANT -DUSE_MEMORY_H 
LFLAGS=   -Wl,-export-dynamic
LFLAGS_CORE=
LIBS_CORE=
DECODERS_TO_BUILD= decoders/mpg123.so decoders/vorbis.so decoders/flac.so decoders/wavpack.so decoders/mikmod.so
FRONTENDS_TO_BUILD= frontends/sdl.so frontends/gmuhttp.so frontends/log.so
TARGET=miyoo
TOOLS_TO_BUILD= gmu.bin

DEC_vorbis_CFLAGS=-DUSE_VORBISFILE
DEC_vorbis_LIBS=-lvorbisfile -lvorbis -logg
