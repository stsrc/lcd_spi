#ifndef _IPC_H_
#define _IPC_H_

#include <stdint.h>

#define WRITE_TEXT	1
#define WRITE_BITMAP	2
#define WRITE_RECTANGLE 3
#define READ_TOUCHSCREEN 4

struct ipc_buffer {
	int cmd;
	uint8_t *mem;
	uint16_t x;
	uint16_t y;
	uint16_t dx;
	uint16_t dy;
};

#include "lcd_spi.h"
#endif
