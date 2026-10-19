/* 
 * Gmu Music Player
 *
 * File: hw_miyoo.h
 *
 * Description: MiyooCFW (Powkiddy V90/Q90, BittBoy, PocketGo) header
 */
#ifndef _HW_MIYOO_H
#define _HW_MIYOO_H

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <unistd.h>

#define SAMPLE_BUFFER_SIZE 4096

int         hw_open_mixer(int mixer_channel);
void        hw_close_mixer(void);
void        hw_set_volume(int volume);
void        hw_display_off(void);
void        hw_display_on(void);
void        hw_detect_device_model(void);
const char *hw_get_device_model_name(void);
#endif
