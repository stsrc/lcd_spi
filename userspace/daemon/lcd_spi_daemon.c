/*
 * Based a little on spidev_test.c
 *	Copyright (c) 2007 MontaVista Software, Inc.
 *	Copyright (c) 2007 Anton Voronstov <avorontsov@ru.mvista.com>
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the temrs of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.	
 */

#include "lcd_spi.h"
#include "ipc_server.h"
#include "fonts.h"
#include <math.h>


static const char *device_lcd = "/dev/lcd_spi";
static const char *device_touch = "/dev/touchpad_spi";

static int transfer(int fd, uint8_t *tx, uint8_t *rx, uint32_t n, 
		    unsigned int cmd)
{
	int ret;
	struct lcdd_transfer tr = {
		.byte_cnt = n,
		.tx_buf = tx,
		.rx_buf = rx
	};
	ret = ioctl(fd, cmd, &tr);
	if (ret < 1)
		return ret;
	return 0;
}

void print_buf(uint8_t *buf, int size)
{
	for (int i = 0; i < size; i++) 
		printf("%d: %x\n", i, buf[i]);
}

static uint8_t check_clean_mem(void *tx, void *rx, int check_rx) 
{
	if (tx == NULL || (check_rx && rx == NULL)) {
		if (tx != NULL)
			free(tx);
		else if (rx != NULL)
			free(rx);
		return 1;		
	}
	return 0;
}

static inline int transfer_wr_data(int fd, uint8_t *mem, uint32_t size) 
{
	transfer(fd, mem, NULL, size, SPI_IO_WR_DATA);
	return 0;
}

static inline int transfer_wr_cmd(int fd, uint8_t cmd)
{
	transfer(fd, &cmd, NULL, 1, SPI_IO_WR_CMD);
	return 0;
}

static int transfer_wr_cmd_data(int fd, int arg_cnt, ... )
{
	va_list arg_list;
	uint8_t cmd;
	uint8_t *tx = NULL;
	if (arg_cnt < 1) 
		return 1;	
	va_start(arg_list, arg_cnt);
	cmd = va_arg(arg_list, int);
	transfer(fd, &cmd, NULL, 1, SPI_IO_WR_CMD);
	if (arg_cnt == 1) {
		va_end(arg_list);
		return 0;
	}
	tx = malloc(sizeof(uint8_t)*(arg_cnt - 1));
	if (check_clean_mem(tx, NULL, 0))
		return -1;
	for (int i = 0; i < arg_cnt - 1; i++)
		tx[i] = va_arg(arg_list, int);
	va_end(arg_list);
	transfer(fd, tx, NULL, arg_cnt - 1, SPI_IO_WR_DATA);
	free(tx);
	return 0;
}

static int transfer_rd_d(int fd, int n, uint8_t cmd, uint8_t *rx)
{
	transfer(fd, &cmd, rx, n + 2, SPI_IO_RD_CMD);
	return 0;
}

static int lcd_set_LUT(int fd)
{
	const uint8_t LUT_size = 128;
	uint8_t *LUT = malloc(LUT_size);
	if (LUT == NULL)
		return 1;
	for (uint8_t i = 0; i < 32; i++) 
		LUT[i] = 2*i;
	for (uint8_t i = 32; i < 96; i++) 
		LUT[i] = i - 32;
	for (uint8_t i = 96; i < 128; i++) 
		LUT[i] = 2 * (i - 96);
	transfer_wr_cmd(fd, 0x2D);
	transfer_wr_data(fd, LUT, LUT_size);
	free(LUT);
	return 0;
}

struct cmd_input {
	uint8_t START;
	uint8_t A2;
	uint8_t A1;
	uint8_t A0; 
	uint8_t MODE; 
	uint8_t SER_DFR;
	uint8_t PID1;
	uint8_t PID0;
};

static uint8_t touch_generate_command(struct cmd_input *in)
{
	uint8_t ret = 0;
	if (in->START)
		ret = 1 << 7;
	if (in->A2)
		ret |= 1 << 6;
	if (in->A1)
		ret |= 1 << 5;
	if (in->A0)
		ret |= 1 << 4;
	if (in->MODE)
		ret |= 1 << 3;
	if (in->SER_DFR)
		ret |= 1 << 2;
	if (in->PID1)
		ret |= 1 << 1;
	if (in->PID0)
		ret |= 1;
	return ret;
}

static uint16_t touch_generate_short(uint8_t byte_1, uint16_t byte_2)
{
	uint16_t ret = byte_1 << 4;
	ret |= byte_2 >> 4;
	return ret;
}

static void touch_calculate_pos(uint16_t input, uint16_t *val, uint16_t max)
{
	float temp = ((float)input)/2000.0f;
	temp = ((float)max)*temp;
	*val = floor(temp + 0.5f); 
}

static void lcd_init_touchscreen(int fd)
{
	struct cmd_input inp = {
		.START = 1,
		.A2 = 1,
		.A1 = 0,
		.A0 = 1,
		.MODE = 0,
		.SER_DFR = 0,
		.PID1 = 0,
		.PID0 = 0
	};
	uint8_t rx[3];
	uint8_t cmd;
	cmd = touch_generate_command(&inp);
	transfer_rd_d(fd, 3, cmd, rx);	
}

int lcd_read_touchscreen(int fd, uint16_t *x, uint16_t *y, uint16_t *z) 
{
	struct cmd_input inp = {
		.START = 1,
		.A2 = 1,
		.A1 = 0,
		.A0 = 1,
		.MODE = 0,
		.SER_DFR = 0,
		.PID1 = 0,
		.PID0 = 0
	};
	uint16_t value;
	uint8_t rx[3];
	uint8_t cmd;
	cmd = touch_generate_command(&inp);
	transfer_rd_d(fd, 3, cmd, rx);	
	value = touch_generate_short(rx[1], rx[2]);
	touch_calculate_pos(value, x, LENGTH_MAX);
	inp.A2 = 0;
	cmd = touch_generate_command(&inp);
	transfer_rd_d(fd, 3, cmd, rx);
	value = touch_generate_short(rx[1], rx[2]);
	touch_calculate_pos(value, y, HEIGHT_MAX);
	inp.A1 = 1;
	cmd = touch_generate_command(&inp);
	transfer_rd_d(fd, 3, cmd, rx);
	value = touch_generate_short(rx[1], rx[2]);
	*z = value;
	inp.A2 = 1;
	inp.A1 = 0;
	inp.A0 = 0;
	cmd = touch_generate_command(&inp);
	transfer_rd_d(fd, 3, cmd, rx);
	value = touch_generate_short(rx[1], rx[2]);
	value = 2050 - value;
	*z = (*z + value)/2;
	touch_calculate_pos(*z, z, LENGTH_MAX);
	return 0;
}

static void lcd_init(int fd, int fd_touch)
{
	struct timespec req;
	/*Reset*/
	transfer_wr_cmd(fd, 0x01);
	req.tv_sec = 0;
	req.tv_nsec = 120000000; 
	nanosleep(&req, NULL);
	/*Display ON*/
	transfer_wr_cmd(fd, 0x29);
	/*Display sleep out*/
	transfer_wr_cmd(fd, 0x11);
	req.tv_nsec = 5000000; 
	nanosleep(&req, NULL);
	/*Pixel format set - 16bits/pixel*/
	transfer_wr_cmd_data(fd, 2, 0x3A, 0x55);
	/*RGB-BGR Order*/
	transfer_wr_cmd_data(fd, 2, 0x36, 0b00001000);
	req.tv_nsec = 120000000;
	nanosleep(&req, NULL);
	/*Brightness control block on*/
	transfer_wr_cmd_data(fd, 2, 0x53, 0x2C);
	nanosleep(&req, NULL); 
	/*Display brightness - 0xff*/
	transfer_wr_cmd_data(fd, 2, 0x51, 0x12);
	nanosleep(&req, NULL);
	lcd_set_LUT(fd);	
	lcd_init_touchscreen(fd_touch);
}

static inline void lcd_create_bytes(uint16_t value, uint8_t *older, uint8_t *younger)
{
	*older = value >> 8;
	value = value << 8;
	*younger = value >> 8;	
}

static void lcd_draw(int fd, uint8_t *tx, uint8_t *rx, uint32_t mem_size)
{
	const uint32_t single_wr_max = TOT_MEM_SIZE;
	uint32_t written = 0;
	transfer_wr_cmd(fd, 0x2C);
	while (mem_size >= single_wr_max) {
		transfer_wr_data(fd, &tx[written], single_wr_max);
		written += single_wr_max;
		mem_size -= single_wr_max;
	}
	if (mem_size != 0)
	transfer_wr_data(fd, &tx[written], mem_size);
}

static void lcd_set_rectangle(int fd, uint16_t x, uint16_t y, uint16_t length,
			      uint16_t height)
{
	uint8_t byte[4];
	lcd_create_bytes(x, &byte[0], &byte[1]);
	lcd_create_bytes(x + length - 1, &byte[2], &byte[3]);
	transfer_wr_cmd_data(fd, 5, 0x2A, byte[0], byte[1], byte[2], byte[3]);
	lcd_create_bytes(y, &byte[0], &byte[1]);
	lcd_create_bytes(y + height - 1, &byte[2], &byte[3]);
	transfer_wr_cmd_data(fd, 5, 0x2B, byte[0], byte[1], byte[2], byte[3]);
}

static int lcd_colour_test(const uint8_t red, const uint8_t green, const 
			   uint8_t blue)
{
	const uint8_t rb_max = 31;//BAD STYLE - MAGIC
	const uint8_t g_max = 63;

	if ((red > rb_max) || (blue > rb_max))
		return 1;
	else if (green > g_max)
		return 1;
	else
		return 0;
}

static inline void lcd_color_prepare(const uint8_t red, const uint8_t green, 
				     const uint8_t blue, uint8_t *tx)
{
	uint8_t first = 0;
	uint8_t second = 0;
	first = red << 3;
	first |= ((green & 0b00111000) >> 3);
	second = ((green & 0b00000111) << 5);
	second |= blue;
	*tx = first;
	*(tx + 1) = second;
}

static int lcd_fill_rect_with_colour(uint8_t *tx, const uint32_t mem_size, 
				     const uint8_t red, const uint8_t green,
				     const uint8_t blue)
{
	if (lcd_colour_test(red, green, blue))
		return 1;
	for (uint32_t i = 0; i < mem_size; i += BY_PER_PIX) 
		lcd_color_prepare(red, green, blue, &tx[i]);
	return 0;
}

int lcd_draw_rectangle(int fd, uint16_t x, uint16_t y, uint16_t length, 
		       uint16_t height, uint8_t red, uint8_t green, 
		       uint8_t blue)
{
	uint8_t *tx;
	uint32_t mem_size;
	if (x + length > LENGTH_MAX) {
		errno = EINVAL;
		return -1;
	} else if (y + height > HEIGHT_MAX) {
		errno = EINVAL;
		return -1;
	}
	mem_size = BY_PER_PIX * height * length;
	tx = malloc(mem_size);
	if (check_clean_mem(tx, NULL, 0)) {
		errno = ENOMEM;
		return -1;
	}
	lcd_set_rectangle(fd, x, y, length, height);
	if (lcd_fill_rect_with_colour(tx, mem_size, red, green, blue)) {
		free(tx);
		errno = EINVAL;
		return -1;
	}
	lcd_draw(fd, tx, NULL, mem_size);
	free(tx);
	return 0;
}

static inline void lcd_set_pos(uint8_t **mem, struct ipc_buffer *buf, 
			       uint8_t line_cnt, uint8_t mode)
{
	if (!mode) {
		*mem += buf->x * BY_PER_PIX * FONT_X_LEN;
		if (line_cnt)
			*mem += FONT_Y_LEN * BY_PER_PIX * LENGTH_MAX * (line_cnt - 1);
		*mem += (FONT_Y_LEN - 1) * BY_PER_PIX * LENGTH_MAX;
	} else {
		*mem += buf->x * BY_PER_PIX * FONT_X_LEN;
		*mem += (HEIGHT_MAX - 1 - buf->y * FONT_Y_LEN) * BY_PER_PIX * LENGTH_MAX;
	}
}

static int lcd_colorize_text(uint8_t *mem, enum colors color)
{
	switch(color) {
	case black:
		lcd_color_prepare(0, 0, 0, mem);
		return 0;
	case white:
		lcd_color_prepare(31, 63, 31, mem);
		return 0;
	case red:
	       lcd_color_prepare(31, 0, 0, mem);
		return 0;
	case blue:
		lcd_color_prepare(0, 0, 31, mem);
		return 0;
	case yellow:
		lcd_color_prepare(31, 63, 0, mem);
		return 0;
	case green:
		lcd_color_prepare(0, 63, 0, mem);
		return 0;
	case brown:
		lcd_color_prepare(31, 31, 0, mem);
		return 0;
	case background:
		return 0;
	default:
		return -1;
	}		
	return -1;	
}

int lcd_return_colors(enum colors color, uint8_t *red_p, uint8_t *green_p,
		      uint8_t *blue_p)
{
	switch(color) {
	case black:
		*red_p = 0;
		*green_p = 0;
		*blue_p = 0;
		return 0;
	case white:
		*red_p = 31;
		*green_p = 63;
		*blue_p = 31;
		return 0;
	case red:
		*red_p = 31;
		*green_p = 0;
		*blue_p = 0;
		return 0;
	case blue:
		*red_p = 0;
		*green_p = 0;
		*blue_p = 31;
		return 0;
	case yellow:
		*red_p = 31;
		*green_p = 63;
		*blue_p = 0;
		return 0;
	case green:
		*red_p = 0;
		*green_p = 63;
		*blue_p = 0;
		return 0;
	case brown:
		*red_p = 31;
		*green_p = 31;
		*blue_p = 0;
		return 0;
	case background:
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}		
	return -1;	
}

static int lcd_put_text(uint8_t const *mem, struct ipc_buffer *buf, 
			uint8_t line_cnt, const uint8_t mode)
{
	unsigned char *temp;
	char sign;
	uint8_t *temp_mem = (uint8_t *)mem;
	lcd_set_pos(&temp_mem, buf, line_cnt, mode);
	for (uint16_t i = 0; i < buf->dx; i++) {
		sign = buf->mem[i];
		temp = &Font5x7[(sign - 32)*5];
		for (uint8_t it = 0; it < 5; it++) {
			if (*temp) {
				for (uint8_t itt = 0; itt < 8; itt++) {
					if (*temp & (1 << itt)) {
						lcd_colorize_text(temp_mem, buf->dy >> 8);
					} else {
						lcd_colorize_text(temp_mem, buf->dy & 0xff);
					}
					temp_mem -= LENGTH_MAX * BY_PER_PIX;
				}
			} else {
				for (uint8_t itt = 0; itt < 8; itt++) {
					lcd_colorize_text(temp_mem, buf->dy & 0xff);
					temp_mem -= LENGTH_MAX * BY_PER_PIX;
				}
			}
			temp_mem += FONT_Y_LEN * LENGTH_MAX * BY_PER_PIX;
			temp_mem += BY_PER_PIX;
			temp++;
		}
		buf->x++;
		if (buf->x >= LENGTH_MAX / FONT_X_LEN) {
			temp_mem = (uint8_t *)mem;
			buf->x = 0;
			buf->y++;
			if (buf->y == HEIGHT_MAX / FONT_Y_LEN)
				buf->y = 0;
			line_cnt--;
			lcd_set_pos(&temp_mem, buf, line_cnt, mode);
		}
	}
	return 0;
}

void lcd_converse_colors(uint8_t *mem, uint8_t *mem_out, uint32_t size)
{
	uint8_t red, green, blue;
	uint32_t i_out = 0;
	for (uint32_t i = 0; i < size; i += 3) {
		red = mem[i] >> 3;
		green = mem[i + 1] >> 2;
		blue = mem[i + 2] >> 3;
		lcd_color_prepare(red, green, blue, &mem_out[i_out]);	
		i_out += 2;
	}
}

static inline int lcd_check_input(struct ipc_buffer *buf)
{
	if (buf->x >= LENGTH_MAX / FONT_X_LEN) {
		errno = EINVAL;
		return -1;
	} else if (buf->y >= HEIGHT_MAX / FONT_Y_LEN) {
		errno = EINVAL;
		return -1;
	} else if (buf->dx > (LENGTH_MAX / FONT_X_LEN) *
		   (HEIGHT_MAX / FONT_Y_LEN)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static uint32_t lcd_set_text_area(int fd, struct ipc_buffer *buf, 
				  uint8_t *line_cnt, uint8_t *mode)
{
	uint16_t x = 0, y = 0, dx = 0, dy = 0;
	uint32_t pix_cnt;
	*mode = 0;
	if (buf->x + buf->dx > LENGTH_MAX / FONT_X_LEN) {
		x = 0;
		*line_cnt = (buf->x + buf->dx) / (LENGTH_MAX / FONT_X_LEN) + 1;
		if (buf->y + *line_cnt > HEIGHT_MAX / FONT_Y_LEN) {
			x = 0;
			y = 0;
			dx = LENGTH_MAX;
			dy = HEIGHT_MAX;
			pix_cnt = dx * dy;
			*mode = 1;
		} else {
			dx = LENGTH_MAX;
			dy = FONT_Y_LEN * *line_cnt;
			y = HEIGHT_MAX - FONT_Y_LEN * (buf->y + *line_cnt);
			pix_cnt = dy * LENGTH_MAX;
		}
	} else {
		x = 0;	
		y = HEIGHT_MAX - FONT_Y_LEN * (buf->y + 1);
		dx = LENGTH_MAX;
		dy = FONT_Y_LEN;
		pix_cnt = dx * dy; 
		*line_cnt = 0;
	}
	lcd_set_rectangle(fd, x, y, dx, dy);
	return pix_cnt;
}

int lcd_draw_text(int fd, struct ipc_buffer *buf)
{
	uint8_t *mem, *mem_out, line_cnt, mode;
	uint32_t pix_cnt;
	if (lcd_check_input(buf))
		return -1;
	pix_cnt = lcd_set_text_area(fd, buf, &line_cnt, &mode);
	mem = malloc(pix_cnt * 3);
	mem_out = malloc(pix_cnt * 2);
	if ((!mem) || (!mem_out)) {
		errno = ENOMEM;
		return -1;
	}
	transfer_rd_d(fd, pix_cnt * 3, 0x2E, mem);
	lcd_converse_colors(mem, mem_out, pix_cnt * 3);
	lcd_put_text(mem_out, buf, line_cnt, mode);
	lcd_draw(fd, mem_out, NULL, pix_cnt * 2);
	free(mem);
	free(mem_out);
	return 0;
}

int lcd_draw_bitmap(int fd, struct ipc_buffer *buf)
{
	if (buf->x + buf->dx > LENGTH_MAX) {
		errno = EINVAL;
		return -1;
	} else if (buf->y + buf->dy > HEIGHT_MAX) {
		errno = EINVAL;
		return -1;
	}
	lcd_set_rectangle(fd, buf->x, buf->y, buf->dx, buf->dy);
	lcd_draw(fd, buf->mem, NULL, BY_PER_PIX*buf->dx*buf->dy);
	return 0;
}

int lcd_clear_background(int fd) 
{
	return lcd_draw_rectangle(fd, 0, 0, LENGTH_MAX, HEIGHT_MAX, 0, 
				  0, 0);
}


int switch_to_daemon(int fd_lcd, int fd_touch)
{
	pid_t pid;
	pid = fork();
	if (pid < 0)
		exit(1);
	switch (pid) {
	case 0:
		break;
	default:
		close(fd_lcd);
		close(fd_touch);
		exit(0);
	}
	umask(0);
	pid = setsid();
	if (pid < 0) 
		exit(1);
	if ((chdir("/")) < 0)
		exit(1);
	close(0);
	close(1);
	close(2);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	return 0;
}

int main(int argc, char *argv[])
{
	int fd_lcd, fd_touch;
	fd_lcd = open(device_lcd, O_RDWR);
	if (fd_lcd < 0) 
		return -1;
	fd_touch = open(device_touch, O_RDWR);
	if (fd_touch < 0) {
		close(fd_lcd);
		return -1;
	}
	switch_to_daemon(fd_lcd, fd_touch);
	lcd_init(fd_lcd, fd_touch);
	lcd_clear_background(fd_lcd);
	ipc_main(fd_lcd, fd_touch);
	close(fd_lcd);
	close(fd_touch);
	return 0;
}
