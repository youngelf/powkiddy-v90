/*
 * patch_boot_logo.c
 *
 * Self-contained, zero-dependency C99 utility to replace the embedded
 * "Powkiddy" splash screen inside MiyooCFW v1.3.3
 * (powkiddy-q90-v90-cfw-v1.3.3.img) with a custom BMP image (e.g. 145x84
 * tux_linux_inside.bmp).
 *
 * Build:
 *   gcc -O2 -Wall -Wextra patch_boot_logo.c -o patch_boot_logo
 *
 * Usage:
 *   ./patch_boot_logo powkiddy-q90-v90-cfw-v1.3.3.img tux_linux_inside.bmp
 */

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Offset of /boot-logo inside powkiddy-q90-v90-cfw-v1.3.3.img:
 * Partition 4 (FAT32) starts at sector 1574912 (0x30100000).
 * Reserved sectors = 36, 2 FATs * 3010 sectors = 6020 sectors.
 * Data region starts at sector 1574912 + 6056 = 1580968 (0x306ea000).
 * /boot-logo is stored contiguously starting at cluster 368653 (8 sectors/cluster):
 *   0x306ea000 + (368653 - 2) * 4096 = 0x8a400000.
 */
#define IMG_BOOT_LOGO_OFFSET 0x8a400000ULL
#define BOOT_LOGO_ELF_SIZE   1501372U

/* Unused page-alignment padding at the end of LOAD segment 1 in /boot-logo:
 * File offset 0x1533cc .. 0x153f38 (2,924 bytes available), mapped at
 * virtual address 0x001633cc.
 */
#define ELF_GAP_FILE_OFFSET  0x001533ccU
#define ELF_GAP_VIRT_ADDR    0x001633ccU
#define ELF_GAP_MAX_SIZE     (0x00153f38U - ELF_GAP_FILE_OFFSET)

/* GameBoy olive-green background color used by /boot-logo: RGB(96, 148, 8) */
#define BG_R 96
#define BG_G 148
#define BG_B 8

static void write_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)((v >> 16) & 0xff);
  p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void write_u32_be(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 24) & 0xff);
  p[1] = (uint8_t)((v >> 16) & 0xff);
  p[2] = (uint8_t)((v >> 8) & 0xff);
  p[3] = (uint8_t)(v & 0xff);
}

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* Standard IEEE 802.3 CRC-32 for PNG chunks */
static uint32_t crc32_calc(const uint8_t *buf, size_t len) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xedb88320U & (-(crc & 1U)));
    }
  }
  return ~crc;
}

/* Standard Adler-32 for zlib stream */
static uint32_t adler32_calc(const uint8_t *buf, size_t len) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    a = (a + buf[i]) % 65521U;
    b = (b + a) % 65521U;
  }
  return (b << 16) | a;
}

/* Bit-writer for RFC 1951 Fixed-Huffman Deflate */
typedef struct {
  uint8_t *data;
  size_t size;
  size_t cap;
  uint32_t bitbuf;
  int bitcnt;
} BitWriter;

static void bw_put_bits(BitWriter *bw, uint32_t bits, int count) {
  bw->bitbuf |= (bits << bw->bitcnt);
  bw->bitcnt += count;
  while (bw->bitcnt >= 8) {
    if (bw->size >= bw->cap) {
      bw->cap = bw->cap ? bw->cap * 2 : 4096;
      bw->data = realloc(bw->data, bw->cap);
    }
    bw->data[bw->size++] = (uint8_t)(bw->bitbuf & 0xff);
    bw->bitbuf >>= 8;
    bw->bitcnt -= 8;
  }
}

/* Reverse bit order for Huffman codes (transmitted MSB-first in RFC 1951) */
static uint32_t rev_bits(uint32_t v, int n) {
  uint32_t r = 0;
  for (int i = 0; i < n; i++) {
    r = (r << 1) | (v & 1U);
    v >>= 1;
  }
  return r;
}

static void bw_put_fixed_sym(BitWriter *bw, int sym) {
  if (sym <= 143) {
    bw_put_bits(bw, rev_bits(0x30 + sym, 8), 8);
  } else if (sym <= 255) {
    bw_put_bits(bw, rev_bits(0x190 + (sym - 144), 9), 9);
  } else if (sym <= 279) {
    bw_put_bits(bw, rev_bits(sym - 256, 7), 7);
  } else {
    bw_put_bits(bw, rev_bits(0xc0 + (sym - 280), 8), 8);
  }
}

/* Compress raw buffer into a zlib stream using RFC 1951 Fixed-Huffman + LZ77 */
static uint8_t *zlib_compress_fixed(const uint8_t *src, size_t src_len,
                                    size_t *out_len) {
  static const int len_base[29] = {
      3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const int len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                    1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                    4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const int dist_base[30] = {
      1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const int dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                     4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                     9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  BitWriter bw = {0};
  /* Zlib header: CMF=0x78 (deflate, 32K window), FLG=0x9c */
  bw_put_bits(&bw, 0x78, 8);
  bw_put_bits(&bw, 0x9c, 8);

  /* Deflate block header: BFINAL=1, BTYPE=01 (Fixed Huffman) */
  bw_put_bits(&bw, 1, 1);
  bw_put_bits(&bw, 1, 2);

  size_t pos = 0;
  while (pos < src_len) {
    size_t best_len = 0;
    size_t best_dist = 0;
    size_t max_len = src_len - pos;
    if (max_len > 258) max_len = 258;
    size_t start = (pos > 32768) ? (pos - 32768) : 0;

    for (size_t j = start; j < pos; j++) {
      size_t l = 0;
      while (l < max_len && src[j + l] == src[pos + l]) l++;
      if (l >= 3 && l >= best_len) {
        best_len = l;
        best_dist = pos - j;
        if (l == max_len) break;
      }
    }

    if (best_len >= 3) {
      int lc = 28;
      for (int k = 0; k < 29; k++) {
        if (k == 28 || (int)best_len < len_base[k + 1]) {
          lc = k;
          break;
        }
      }
      bw_put_fixed_sym(&bw, 257 + lc);
      if (len_extra[lc] > 0) {
        bw_put_bits(&bw, (uint32_t)(best_len - len_base[lc]), len_extra[lc]);
      }

      int dc = 29;
      for (int k = 0; k < 30; k++) {
        if (k == 29 || (int)best_dist < dist_base[k + 1]) {
          dc = k;
          break;
        }
      }
      bw_put_bits(&bw, rev_bits(dc, 5), 5);
      if (dist_extra[dc] > 0) {
        bw_put_bits(&bw, (uint32_t)(best_dist - dist_base[dc]), dist_extra[dc]);
      }
      pos += best_len;
    } else {
      bw_put_fixed_sym(&bw, src[pos]);
      pos++;
    }
  }

  /* End-of-block symbol 256 */
  bw_put_fixed_sym(&bw, 256);
  if (bw.bitcnt > 0) {
    bw_put_bits(&bw, 0, 8 - bw.bitcnt);
  }

  uint32_t adler = adler32_calc(src, src_len);
  bw_put_bits(&bw, (adler >> 24) & 0xff, 8);
  bw_put_bits(&bw, (adler >> 16) & 0xff, 8);
  bw_put_bits(&bw, (adler >> 8) & 0xff, 8);
  bw_put_bits(&bw, adler & 0xff, 8);

  *out_len = bw.size;
  return bw.data;
}

static void png_append_chunk(uint8_t **png, size_t *png_len, size_t *png_cap,
                             const char type[4], const uint8_t *data,
                             size_t len) {
  if (*png_len + 12 + len > *png_cap) {
    *png_cap = (*png_cap + 12 + len) * 2;
    *png = realloc(*png, *png_cap);
  }
  uint8_t *p = *png + *png_len;
  write_u32_be(p, (uint32_t)len);
  memcpy(p + 4, type, 4);
  if (len > 0) memcpy(p + 8, data, len);
  uint32_t crc = crc32_calc(p + 4, 4 + len);
  write_u32_be(p + 8 + len, crc);
  *png_len += 12 + len;
}

/* Read a 24-bit or 32-bit uncompressed BMP and encode as 8-bit RGBA PNG */
static uint8_t *bmp_to_png_rgba(const char *bmp_path, uint32_t *out_w,
                                uint32_t *out_h, size_t *out_png_len) {
  FILE *fp = fopen(bmp_path, "rb");
  if (!fp) {
    perror("fopen bmp");
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  long fsz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (fsz < 54) {
    fprintf(stderr, "Error: BMP file too small.\n");
    fclose(fp);
    return NULL;
  }
  uint8_t *bmp = malloc((size_t)fsz);
  if (fread(bmp, 1, (size_t)fsz, fp) != (size_t)fsz) {
    perror("fread bmp");
    free(bmp);
    fclose(fp);
    return NULL;
  }
  fclose(fp);

  if (bmp[0] != 'B' || bmp[1] != 'M') {
    fprintf(stderr, "Error: %s is not a valid BMP file.\n", bmp_path);
    free(bmp);
    return NULL;
  }

  uint32_t data_off = read_u32_le(bmp + 10);
  uint32_t w = read_u32_le(bmp + 18);
  int32_t h_signed = (int32_t)read_u32_le(bmp + 22);
  int top_down = (h_signed < 0);
  uint32_t h = (uint32_t)(top_down ? -h_signed : h_signed);
  uint16_t bpp = read_u16_le(bmp + 28);
  uint32_t comp = read_u32_le(bmp + 30);

  if ((bpp != 24 && bpp != 32) || comp != 0) {
    fprintf(stderr,
            "Error: BMP must be uncompressed 24-bit or 32-bit (got %u bpp, "
            "comp=%u).\n",
            bpp, comp);
    free(bmp);
    return NULL;
  }

  size_t row_bytes = ((w * (bpp / 8) + 3) & ~3U);
  if (data_off + row_bytes * h > (size_t)fsz) {
    fprintf(stderr, "Error: Truncated BMP pixel data.\n");
    free(bmp);
    return NULL;
  }

  /* Build PNG scanlines with filter=1 (Sub) or filter=2 (Up), picking whichever
   * yields more zeros per row for maximum LZ77 compression. */
  size_t stride = (size_t)w * 4;
  uint8_t *rgba = calloc((size_t)h, stride);
  for (uint32_t y = 0; y < h; y++) {
    uint32_t src_y = top_down ? y : (h - 1 - y);
    const uint8_t *src_row = bmp + data_off + src_y * row_bytes;
    uint8_t *dst_row = rgba + y * stride;
    for (uint32_t x = 0; x < w; x++) {
      uint8_t b = src_row[x * (bpp / 8) + 0];
      uint8_t g = src_row[x * (bpp / 8) + 1];
      uint8_t r = src_row[x * (bpp / 8) + 2];
      uint8_t a = (bpp == 32) ? src_row[x * 4 + 3] : 255;
      if (a == 0 || (r == BG_R && g == BG_G && b == BG_B)) {
        dst_row[x * 4 + 0] = 0;
        dst_row[x * 4 + 1] = 0;
        dst_row[x * 4 + 2] = 0;
        dst_row[x * 4 + 3] = 0;
      } else {
        dst_row[x * 4 + 0] = r;
        dst_row[x * 4 + 1] = g;
        dst_row[x * 4 + 2] = b;
        dst_row[x * 4 + 3] = a;
      }
    }
  }
  free(bmp);

  size_t raw_len = (size_t)h * (1 + stride);
  uint8_t *raw = malloc(raw_len);
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t *cur = rgba + y * stride;
    uint8_t *out = raw + y * (1 + stride);
    out[0] = 0; /* Filter 0 (None) works great with 4-byte LZ77 repeats */
    memcpy(out + 1, cur, stride);
  }
  free(rgba);

  size_t idat_len = 0;
  uint8_t *idat = zlib_compress_fixed(raw, raw_len, &idat_len);
  free(raw);

  static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  size_t png_cap = 256 + idat_len;
  size_t png_len = 8;
  uint8_t *png = malloc(png_cap);
  memcpy(png, png_sig, 8);

  uint8_t ihdr[13];
  write_u32_be(ihdr + 0, w);
  write_u32_be(ihdr + 4, h);
  ihdr[8] = 8;  /* bit depth: 8 */
  ihdr[9] = 6;  /* color type: 6 (RGBA) */
  ihdr[10] = 0; /* compression: deflate */
  ihdr[11] = 0; /* filter: adaptive */
  ihdr[12] = 0; /* interlace: none */
  png_append_chunk(&png, &png_len, &png_cap, "IHDR", ihdr, 13);
  png_append_chunk(&png, &png_len, &png_cap, "IDAT", idat, idat_len);
  png_append_chunk(&png, &png_len, &png_cap, "IEND", NULL, 0);
  free(idat);

  *out_w = w;
  *out_h = h;
  *out_png_len = png_len;
  return png;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <powkiddy-q90-v90-cfw-v1.3.3.img> <splash.bmp>\n",
            argv[0]);
    return 1;
  }

  const char *img_path = argv[1];
  const char *bmp_path = argv[2];

  uint32_t w = 0, h = 0;
  size_t png_len = 0;
  uint8_t *png = bmp_to_png_rgba(bmp_path, &w, &h, &png_len);
  if (!png) return 1;

  printf("Encoded %s (%ux%u BMP) -> %zu-byte RGBA PNG (max gap %u bytes).\n",
         bmp_path, w, h, png_len, ELF_GAP_MAX_SIZE);
  if (png_len > ELF_GAP_MAX_SIZE) {
    fprintf(stderr,
            "Error: Encoded PNG (%zu bytes) exceeds available ELF gap (%u "
            "bytes).\n",
            png_len, ELF_GAP_MAX_SIZE);
    free(png);
    return 1;
  }

  FILE *fp = fopen(img_path, "r+b");
  if (!fp) {
    perror("fopen firmware img");
    free(png);
    return 1;
  }

  if (fseeko(fp, (off_t)IMG_BOOT_LOGO_OFFSET, SEEK_SET) != 0) {
    perror("fseeko to /boot-logo");
    fclose(fp);
    free(png);
    return 1;
  }

  uint8_t *elf = malloc(BOOT_LOGO_ELF_SIZE);
  if (fread(elf, 1, BOOT_LOGO_ELF_SIZE, fp) != BOOT_LOGO_ELF_SIZE) {
    fprintf(stderr, "Error: Failed to read /boot-logo from %s.\n", img_path);
    free(elf);
    fclose(fp);
    free(png);
    return 1;
  }

  /* Verify ELF magic and expected ARM instructions in /boot-logo */
  if (memcmp(elf, "\x7f" "ELF\x01\x01\x01", 7) != 0 ||
      read_u32_le(elf + 0x198) != 0xeb00be1cU) {
    fprintf(stderr,
            "Error: /boot-logo signature mismatch at offset 0x%llx in %s.\n",
            (unsigned long long)IMG_BOOT_LOGO_OFFSET, img_path);
    free(elf);
    fclose(fp);
    free(png);
    return 1;
  }

  /* 1. Copy new PNG into the LOAD[1] segment gap at 0x1533cc (vaddr 0x1633cc) */
  memset(elf + ELF_GAP_FILE_OFFSET, 0, ELF_GAP_MAX_SIZE);
  memcpy(elf + ELF_GAP_FILE_OFFSET, png, png_len);

  /* 2. Extend PHDR[1] (offset 84 = 52 + 32) p_filesz and p_memsz */
  uint32_t new_seg_sz = (ELF_GAP_FILE_OFFSET + (uint32_t)png_len + 3U) & ~3U;
  write_u32_le(elf + 84 + 16, new_seg_sz); /* p_filesz */
  write_u32_le(elf + 84 + 20, new_seg_sz); /* p_memsz  */

  /* 3. Update SDL_RWFromMem(buf, size) literal pool in main() */
  write_u32_le(elf + 0x374, (uint32_t)png_len);
  write_u32_le(elf + 0x378, ELF_GAP_VIRT_ADDR);

  /* 4. Adjust scroll start offset (`mvn r7, #imm` at 0x101ec) so total scroll
   *    distance remains ~202 frames regardless of image height `h`:
   *    center_y = (240 - h) / 2, start_y = -(imm + 1) - h.
   *    For center_y - start_y = 202 => imm = 81 - h / 2. (For h=84, imm=39). */
  int imm = 81 - (int)(h / 2);
  if (imm < 0) imm = 0;
  if (imm > 255) imm = 255;
  write_u32_le(elf + 0x1ec, 0xe3e07000U | (uint32_t)imm);

  /* Write patched /boot-logo back into the disk image */
  if (fseeko(fp, (off_t)IMG_BOOT_LOGO_OFFSET, SEEK_SET) != 0 ||
      fwrite(elf, 1, BOOT_LOGO_ELF_SIZE, fp) != BOOT_LOGO_ELF_SIZE) {
    perror("fwrite patched /boot-logo");
    free(elf);
    fclose(fp);
    free(png);
    return 1;
  }

  fclose(fp);
  free(elf);
  free(png);

  printf("Successfully patched /boot-logo in %s at offset 0x%llx!\n", img_path,
         (unsigned long long)IMG_BOOT_LOGO_OFFSET);
  return 0;
}
