# Powkiddy Q90/V90 (MiyooCFW v1.3.3) Boot Splash Patcher

A standalone, zero-dependency C99 tool (`patch_boot_logo`) that replaces the stock `"Powkiddy®"` boot splash graphic inside a pristine `powkiddy-q90-v90-cfw-v1.3.3.img` SD card image with a custom `.bmp` image (default: `tux_linux_inside.bmp`, `145×84` showing the Tux penguin logo and `"Linux inside"`).

## Files

- `patch_boot_logo.c`: Self-contained C99 source that reads an uncompressed 24-bit/32-bit `.bmp` image, encodes it into an RGBA PNG in memory using an internal RFC 1951 Fixed-Huffman + LZ77 Deflate compressor, and patches `/boot-logo` inside the firmware `.img`.
- `tux_linux_inside.bmp`: `145×84` 24-bit Windows BMP featuring the Tux logo and `"Linux inside"` on the `RGB(96, 148, 8)` (`#609408`) GameBoy olive-green background.
- `Makefile`: Build and patch targets.

## Quick Start

### 1. Build the Patcher

```bash
make
```

### 2. Patch a Firmware Image

Place `powkiddy-q90-v90-cfw-v1.3.3.img` in this directory (or pass `IMG=/path/to/image.img`) and run:

```bash
make patch
```

Or invoke the binary directly:

```bash
./patch_boot_logo powkiddy-q90-v90-cfw-v1.3.3.img tux_linux_inside.bmp
```

Expected output:

```text
Encoded tux_linux_inside.bmp (145x84 BMP) -> 1267-byte RGBA PNG (max gap 2924 bytes).
Successfully patched /boot-logo in powkiddy-q90-v90-cfw-v1.3.3.img at offset 0x8a400000!
```

## Using a Custom `.bmp` Image

1. Create or export an uncompressed **24-bit RGB** or **32-bit RGBA** Windows `.bmp` image (up to `320×240`; `145×84` is recommended).
2. Any pixel with color `RGB(96, 148, 8)` (`#609408`, the screen background color) or `alpha == 0` is automatically made transparent when encoded into `/boot-logo`.
3. The compressed PNG must fit within the **2,924-byte** (`0xb6c`) ELF segment gap in `/boot-logo`.
4. Run:
   ```bash
   ./patch_boot_logo powkiddy-q90-v90-cfw-v1.3.3.img your_image.bmp
   ```

## How It Works Under the Hood

During boot, `/etc/main` on Partition 2 (`rootfs`) executes `/mnt/boot-logo` from Partition 4 (`main`, FAT32). In `powkiddy-q90-v90-cfw-v1.3.3.img`, `/boot-logo` (`1,501,372` bytes) is stored contiguously across 367 FAT32 clusters starting at cluster `368653` (byte offset `0x8a400000`).

Inside `/boot-logo`:
1. `main()` (`0x1014c`) passes an embedded buffer to `SDL_RWFromMem(buf, size)` (`0x10188`–`0x10190`) and decodes it via `IMG_LoadPNG_RW` (`0x3fa10`).
2. `patch_boot_logo` writes the newly encoded RGBA PNG into the unused page-alignment gap at the end of `LOAD` segment 1 (file offset `0x1533cc`, virtual address `0x001633cc`, max `2,924` bytes) and extends `PHDR[1].p_filesz` and `p_memsz`.
3. It updates the literal pool at `0x10374` (`size`) and `0x10378` (`0x001633cc`), and adjusts the `mvn r7, #imm` instruction at `0x101ec` (`imm = 81 - height / 2`) so the vertical scroll animation duration remains ~202 frames (`3.23` seconds).
