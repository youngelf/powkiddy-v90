CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TARGET  := patch_boot_logo
SRC     := patch_boot_logo.c
IMG     ?= powkiddy-q90-v90-cfw-v1.3.3.img
BMP     ?= tux_linux_inside.bmp

.PHONY: all patch clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

patch: $(TARGET)
	./$(TARGET) $(IMG) $(BMP)

clean:
	rm -f $(TARGET)
