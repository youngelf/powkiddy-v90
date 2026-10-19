/* 
 * Gmu Music Player
 *
 * File: hw_miyoo.c
 *
 * Description: MiyooCFW (Powkiddy V90/Q90) hardware routines (reconstructed
 * from apps/gmu/gmu.bin disassembly)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "hw_miyoo.h"
#include "oss_mixer.h"
#include "debug.h"

static int selected_mixer = -1;

int hw_open_mixer(int mixer_channel)
{
	int res = oss_mixer_open();
	selected_mixer = mixer_channel;
	wdprintf(V_INFO, "hw_miyoo", "Selected mixer: %d\n", selected_mixer);
	return res;
}

void hw_close_mixer(void)
{
	oss_mixer_close();
}

void hw_set_volume(int volume)
{
	if (selected_mixer >= 0) {
		if (volume >= 0) oss_mixer_set_volume(selected_mixer, volume);
	} else {
		wdprintf(V_INFO, "hw_miyoo", "No suitable mixer available.\n");
	}
}

void hw_display_off(void)
{
	FILE *f;

	wdprintf(V_DEBUG, "hw_miyoo", "Display off requested.\n");
	if ((f = fopen("/sys/devices/platform/backlight/backlight/backlight/brightness", "w"))) {
		fwrite("0\n", 1, 2, f);
		fclose(f);
	}
}

void hw_display_on(void)
{
	char  buf[32] = {0};
	char  val_str[10] = {0};
	int   fd, val = -1;
	size_t i, len;
	FILE *f;

	fd = open("/mnt/.backlight.conf", O_RDWR);
	if (fd >= 0) {
		if (read(fd, val_str, 10) > 0) {
			len = strlen(val_str);
			for (i = 0; i < len; i++) {
				if (val_str[i] == '\r' || val_str[i] == '\n' || val_str[i] == ' ')
					val_str[i] = '\0';
			}
			val = atoi(val_str);
		}
		close(fd);
	}

	wdprintf(V_DEBUG, "hw_miyoo", "Display on requested.\n");
	if ((f = fopen("/sys/devices/platform/backlight/backlight/backlight/brightness", "w"))) {
		sprintf(buf, "%d\n", val);
		fputs(buf, f);
		fclose(f);
	}
}

void hw_detect_device_model(void)
{
}

const char *hw_get_device_model_name(void)
{
	return "Miyoo";
}
